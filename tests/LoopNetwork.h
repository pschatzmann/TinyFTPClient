#pragma once
/**
 * Thread-safe in-memory network: LoopClient connects to the LoopServer which
 * listens on the same port. This allows to run the FTPServer in a separate
 * thread and to access it with the FTPClient.
 */
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#include "Arduino.h"
#include "Client.h"

/// One direction of a connection
struct LoopPipe {
  std::deque<uint8_t> data;
  bool closed = false;
};

class LoopClient;

/// Registry of the listening servers
struct LoopNetwork {
  std::mutex mutex;
  std::map<uint16_t, std::deque<LoopClient> *> listeners;

  static LoopNetwork &instance() {
    static LoopNetwork network;
    return network;
  }
};

class LoopClient : public Client {
 public:
  LoopClient() = default;
  LoopClient(std::shared_ptr<LoopPipe> in, std::shared_ptr<LoopPipe> out)
      : in(in), out(out) {}

  int connect(IPAddress, uint16_t port) override;
  int connect(const char *, uint16_t port) override {
    return connect(IPAddress(127, 0, 0, 1), port);
  }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t *buf, size_t size) override {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    if (!out || out->closed) return 0;
    out->data.insert(out->data.end(), buf, buf + size);
    return size;
  }

  int available() override {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    return in ? (int)in->data.size() : 0;
  }

  int read() override {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
  }

  int read(uint8_t *buf, size_t size) override {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    if (!in) return 0;
    size_t n = 0;
    while (n < size && !in->data.empty()) {
      buf[n++] = in->data.front();
      in->data.pop_front();
    }
    return (int)n;
  }

  int peek() override {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    return in && !in->data.empty() ? in->data.front() : -1;
  }

  void flush() override {}

  void stop() override {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    if (in) in->closed = true;
    if (out) out->closed = true;
    in.reset();
    out.reset();
  }

  /// like a WiFiClient: connected while the peer did not close or while
  /// there is still data
  uint8_t connected() override {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    if (!in) return false;
    return !in->closed || !in->data.empty();
  }

  operator bool() override { return connected(); }

 protected:
  std::shared_ptr<LoopPipe> in;
  std::shared_ptr<LoopPipe> out;
};

class LoopServer {
 public:
  LoopServer(uint16_t port) : port(port) {}
  ~LoopServer() {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    LoopNetwork::instance().listeners.erase(port);
  }

  void begin() {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    LoopNetwork::instance().listeners[port] = &pending;
  }

  LoopClient accept() {
    std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
    if (pending.empty()) return LoopClient();
    LoopClient result = pending.front();
    pending.pop_front();
    return result;
  }

 protected:
  uint16_t port;
  std::deque<LoopClient> pending;
};

inline int LoopClient::connect(IPAddress, uint16_t port) {
  stop();
  std::lock_guard<std::mutex> lock(LoopNetwork::instance().mutex);
  auto &listeners = LoopNetwork::instance().listeners;
  auto it = listeners.find(port);
  if (it == listeners.end()) return 0;
  auto toServer = std::make_shared<LoopPipe>();
  auto toClient = std::make_shared<LoopPipe>();
  in = toClient;
  out = toServer;
  it->second->push_back(LoopClient(toServer, toClient));
  return 1;
}
