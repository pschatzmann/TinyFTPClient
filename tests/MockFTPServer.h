#pragma once
/**
 * A scripted in-memory FTP server and a matching Client implementation, so
 * that FTPClient<MockClient> can be tested on the desktop without network.
 *
 * Port FTP_COMMAND_PORT is the command connection; every PASV hands out a new
 * port which the next data connection connects to.
 */
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Arduino.h"
#include "Client.h"

class MockClient;

/// Registry of data clients by port, so that RETR/NLST can push the payload
/// to the already connected data connection
inline std::map<int, MockClient *> &dataClients() {
  static std::map<int, MockClient *> clients;
  return clients;
}

struct MockFTPServer {
  // server content
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;

  // behaviour
  bool refuse_connections = false;
  bool user_needs_password = true;
  bool reject_user = false;
  bool multi_line_login = false;
  bool nlst_full_path = false;  // NLST returns full paths instead of names
  // slow network: data payloads arrive in chunks of chunk_size bytes every
  // chunk_delay_ms (0 = everything at once)
  size_t chunk_size = 0;
  unsigned long chunk_delay_ms = 0;
  std::string welcome = "220 Welcome\r\n";

  // observations
  std::vector<std::string> commands;  // all received commands
  int command_connections = 0;

  /// Data connection state, keyed by the PASV port
  struct DataChannel {
    std::string to_client;    // RETR / NLST payload
    std::string from_client;  // STOR / APPE payload
    std::string store_target;
    bool append = false;
    MockClient *control = nullptr;
  };
  std::map<int, DataChannel> channels;
  int next_port = 5000;
  std::set<MockClient *> control_clients;

  void reset() {
    *this = MockFTPServer();
    dataClients().clear();
  }

  int count(const std::string &prefix) const {
    int result = 0;
    for (auto &c : commands)
      if (c.compare(0, prefix.size(), prefix) == 0) result++;
    return result;
  }

  /// Simulates the server closing all idle command connections
  void dropControlConnections();
};

inline MockFTPServer &server() {
  static MockFTPServer instance;
  return instance;
}

class MockClient : public Client {
 public:
  ~MockClient() { stop(); }

  int connect(IPAddress ip, uint16_t port) override {
    MockFTPServer &srv = server();
    if (srv.refuse_connections) return 0;
    stop();
    if (port == FTP_COMMAND_PORT) {
      is_control = true;
      srv.command_connections++;
      srv.control_clients.insert(this);
      rx = srv.welcome;
    } else {
      if (srv.channels.count(port) == 0) return 0;
      is_control = false;
      data_port = port;
      dataClients()[port] = this;
      rx = srv.channels[port].to_client;
    }
    is_connected = true;
    return 1;
  }

  int connect(const char *host, uint16_t port) override {
    return connect(IPAddress(127, 0, 0, 1), port);
  }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t *buf, size_t size) override {
    if (!is_connected) return 0;
    if (is_control) {
      for (size_t j = 0; j < size; j++) {
        char c = (char)buf[j];
        if (c == '\n') {
          if (!line.empty() && line.back() == '\r') line.pop_back();
          handleCommand(line);
          line.clear();
        } else {
          line += c;
        }
      }
    } else {
      server().channels[data_port].from_client.append((const char *)buf, size);
    }
    return size;
  }

  int available() override { return (int)(arrived() - pos); }

  int read() override {
    if (pos >= arrived()) return -1;
    return (uint8_t)rx[pos++];
  }

  int read(uint8_t *buf, size_t size) override {
    size_t n = 0;
    while (n < size && pos < arrived()) buf[n++] = (uint8_t)rx[pos++];
    return (int)n;
  }

  int peek() override { return pos < arrived() ? (uint8_t)rx[pos] : -1; }

  void flush() override {}

  void stop() override {
    if (!is_connected) return;
    is_connected = false;
    MockFTPServer &srv = server();
    if (is_control) {
      srv.control_clients.erase(this);
    } else {
      dataClients().erase(data_port);
      // end of the data transfer: store uploads and confirm on the control
      // connection
      auto it = srv.channels.find(data_port);
      if (it != srv.channels.end()) {
        MockFTPServer::DataChannel &ch = it->second;
        if (!ch.store_target.empty()) {
          if (ch.append)
            srv.files[ch.store_target] += ch.from_client;
          else
            srv.files[ch.store_target] = ch.from_client;
          if (ch.control != nullptr) ch.control->reply("226 Transfer complete");
        }
        srv.channels.erase(it);
      }
    }
    rx.clear();
    pos = 0;
    payload_complete = false;
  }

  // like a WiFiClient: a data connection which was closed by the server stays
  // connected until all data has been read
  uint8_t connected() override {
    if (!is_connected) return false;
    if (payload_complete) return pos < rx.size();
    return true;
  }

  operator bool() override { return connected(); }

  void reply(const std::string &txt) { rx += txt + "\r\n"; }

  void serverClose() {
    is_connected = false;
    rx.clear();
    pos = 0;
    payload_complete = false;
  }

 protected:
  bool is_control = false;
  bool is_connected = false;
  int data_port = 0;
  int pasv_port = 0;
  std::string rx;
  size_t pos = 0;
  std::string line;
  bool payload_complete = false;  // server closes the data connection after rx
  unsigned long payload_start = 0;

  /// Number of bytes of rx which have arrived
  size_t arrived() {
    MockFTPServer &srv = server();
    if (is_control || srv.chunk_size == 0) return rx.size();
    size_t chunks =
        1 + (srv.chunk_delay_ms == 0
                 ? rx.size()
                 : (millis() - payload_start) / srv.chunk_delay_ms);
    size_t result = chunks * srv.chunk_size;
    return result < rx.size() ? result : rx.size();
  }

  void handleCommand(const std::string &command) {
    MockFTPServer &srv = server();
    srv.commands.push_back(command);
    size_t sp = command.find(' ');
    std::string verb = command.substr(0, sp);
    std::string arg = sp == std::string::npos ? "" : command.substr(sp + 1);

    if (verb == "USER") {
      if (srv.reject_user) {
        reply("530 Not logged in");
      } else if (srv.user_needs_password) {
        reply("331 Password required");
      } else if (srv.multi_line_login) {
        reply("230-Welcome");
        reply("230-to the mock server");
        reply("230 Logged in");
      } else {
        reply("230 Logged in");
      }
    } else if (verb == "PASS") {
      reply(srv.user_needs_password ? "230 Logged in" : "503 Bad sequence");
    } else if (verb == "OPTS" || verb == "TYPE" || verb == "BIN" ||
               verb == "ASC") {
      reply("200 OK");
    } else if (verb == "PASV") {
      pasv_port = srv.next_port++;
      srv.channels[pasv_port].control = this;
      char buffer[80];
      snprintf(buffer, sizeof(buffer),
               "227 Entering Passive Mode (127,0,0,1,%d,%d)", pasv_port / 256,
               pasv_port % 256);
      reply(buffer);
    } else if (verb == "RETR") {
      if (srv.files.count(arg) == 0) {
        reply("550 Not found");
      } else {
        // the data connection was already opened by the client
        setDataPayload(srv.files[arg]);
        reply("150 Opening");
        reply("226 Transfer complete");
      }
    } else if (verb == "STOR" || verb == "APPE") {
      MockFTPServer::DataChannel &ch = srv.channels[pasv_port];
      ch.store_target = arg;
      ch.append = verb == "APPE";
      reply("150 Opening");
    } else if (verb == "NLST") {
      std::string list;
      std::string prefix = arg == "/" ? "/" : arg + "/";
      for (auto &f : srv.files) {
        if (f.first.compare(0, prefix.size(), prefix) == 0)
          list += (srv.nlst_full_path ? f.first : f.first.substr(prefix.size())) +
                  "\r\n";
      }
      setDataPayload(list);
      reply("150 Opening");
      reply("226 Transfer complete");
    } else if (verb == "SIZE") {
      if (srv.files.count(arg))
        reply("213 " + std::to_string(srv.files[arg].size()));
      else
        reply("550 Not a file");
    } else if (verb == "MKD") {
      srv.dirs.insert(arg);
      reply("257 Created");
    } else if (verb == "RMD") {
      reply(srv.dirs.erase(arg) ? "250 Removed" : "550 Not found");
    } else if (verb == "DELE") {
      reply(srv.files.erase(arg) ? "250 Deleted" : "550 Not found");
    } else if (verb == "ABOR") {
      reply("426 Aborted");
      reply("226 Abort successful");
    } else if (verb == "QUIT") {
      reply("221 Bye");
    } else {
      reply("502 Not implemented");
    }
  }

  /// Provides the payload to the data client which belongs to the current
  /// PASV port
  void setDataPayload(const std::string &data);
};

inline void MockClient::setDataPayload(const std::string &data) {
  server().channels[pasv_port].to_client = data;
  auto it = dataClients().find(pasv_port);
  if (it != dataClients().end()) {
    it->second->rx = data;
    it->second->pos = 0;
    it->second->payload_complete = true;
    it->second->payload_start = millis();
  }
}

inline void MockFTPServer::dropControlConnections() {
  std::set<MockClient *> clients = control_clients;
  for (auto c : clients) c->serverClose();
  control_clients.clear();
}
