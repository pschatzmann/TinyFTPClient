#pragma once

#include "Arduino.h"
#include "Client.h"
#include "FTPBasicAPI.h"
#include "FTPFile.h"
#include "FTPFileIterator.h"
#include "FTPSession.h"
#include "IPAddress.h"
#include "Stream.h"

namespace tinyftp {

/**
 * @brief FTPClient
 * Basic FTP access class which supports directory operations and the opening
 * of files. Each open file or directory listing uses its own FTP session
 * (command and data connection): idle sessions are reused. The files and
 * iterators must not be used after the FTPClient has been destroyed.
 * @tparam ClientType The type of client to use for command and data
 * connections. It should be a class that implements the Client interface.
 * @author Phil Schatzmann
 */
template <class ClientType>
class FTPClient : public FTPSessionProvider {
 public:
  /// Default constructor: Provide the client class as template argument e.g.
  /// FTPClient<WiFiClient> client;
  FTPClient(int port = FTP_COMMAND_PORT, bool useType = false) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPClient");
    setUseTypeCommand(useType);
    setPort(port);
  }

  FTPClient(const FTPClient &) = delete;
  FTPClient &operator=(const FTPClient &) = delete;

  ~FTPClient() {
    end();
    for (auto &session : sessions) {
      delete session;
      session = nullptr;
    }
  }

  /// if set to true the BIN and ASCII command are executed as type I or A
  void setUseTypeCommand(bool useType) { use_type_command = useType; }

  /// Defines the server details: the sessions are opened on demand
  bool begin(IPAddress remote_addr, const char *user = "anonymous",
             const char *password = nullptr) {
    FTPLogger.writeLog(LOG_INFO, "FTPClient", "begin");
    end();
    this->remote_addr = remote_addr;
    this->userid = user;
    this->password = password;
    return true;
  }

  /// Close the sessions by calling QUIT or BYE: open files and listings
  /// become invalid
  void end() {
    FTPLogger.writeLog(LOG_INFO, "FTPClient", "end");
    for (auto &session : sessions) {
      if (session != nullptr) {
        if (session->connected()) session->quit();
        session->end();  // closes the connections
      }
    }
  }

  /// Open a file: the file is closed with close() or when the last copy of
  /// it is destroyed. The autoClose parameter is not used any more.
  FTPFile open(const char *filename, FileMode mode = READ_MODE,
               bool /*autoClose*/ = true) {
    char msg[200];
    snprintf(msg, sizeof(msg), "open: %s", filename);
    FTPLogger.writeLog(LOG_INFO, "FTPClient", msg);

    FTPBasicAPI *api = openDataSession();
    if (api == nullptr) return FTPFile();
    return FTPFile(this, api, filename, mode);
  }

  /// Create the requested directory hierarchy--if intermediate directories
  /// do not exist they will be created.
  bool mkdir(const char *filepath) {
    FTPLogger.writeLog(LOG_INFO, "FTPClient", "mkdir");
    FTPBasicAPI *api = idleSession();
    return api != nullptr && api->mkdir(filepath);
  }

  /// Delete the file
  bool remove(const char *filepath) {
    FTPLogger.writeLog(LOG_INFO, "FTPClient", "remove");
    FTPBasicAPI *api = idleSession();
    return api != nullptr && api->del(filepath);
  }

  /// Removes a directory
  bool rmdir(const char *filepath) {
    FTPLogger.writeLog(LOG_INFO, "FTPClient", "rmdir");
    FTPBasicAPI *api = idleSession();
    return api != nullptr && api->rmd(filepath);
  }

  /// Lists all file names in the specified directory
  FTPFileIterator ls(const char *path, FileMode mode = READ_MODE) {
    FTPLogger.writeLog(LOG_INFO, "FTPClient", "ls");
    FTPBasicAPI *api = openDataSession();
    if (api == nullptr) return FTPFileIterator();
    return FTPFileIterator(this, api, path, mode);
  }

  /// Switch to binary mode
  bool binary() {
    FTPBasicAPI *api = idleSession();
    return api != nullptr && api->binary();
  }

  /// Switch to ascii mode
  bool ascii() {
    FTPBasicAPI *api = idleSession();
    return api != nullptr && api->ascii();
  }

  /// Binary or ascii with type command
  bool type(const char *str) {
    FTPBasicAPI *api = idleSession();
    return api != nullptr && api->type(str);
  }

  void setPort(int port) { this->port = port; }

  /// Abort the indicated operation (e.g., READ_OP, WRITE_OP, LS_OP.) in all
  /// sessions. Returns false if no session was found or an abort failed.
  bool abort(CurrentOperation op) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPClient", "abort");
    bool found = false;
    bool ok = true;
    for (auto &session : sessions) {
      if (session != nullptr && session->currentOperation() == op) {
        found = true;
        ok = session->abort() && ok;
      }
    }
    return found && ok;
  }

  /// Count the connected sessions
  int count() {
    int result = 0;
    for (auto &session : sessions) {
      if (session != nullptr && session->connected()) result++;
    }
    return result;
  }

  /// Count the connected sessions with a specific current operation
  int count(CurrentOperation op) {
    int result = 0;
    for (auto &session : sessions) {
      if (session != nullptr && session->connected() &&
          session->currentOperation() == op)
        result++;
    }
    return result;
  }

  /// Provides an idle, connected session: returns nullptr if none is available
  FTPSession<ClientType> *idleSession() override {
    for (auto &session : sessions) {
      if (session != nullptr && session->currentOperation() == NOP &&
          session->connected()) {
        session->setUseTypeCommand(use_type_command);
        return session;
      }
    }
    // (re)connect an unused session: idle sessions are not connected here
    for (auto &session : sessions) {
      if (session == nullptr) {
        session = new FTPSession<ClientType>();
      } else if (session->currentOperation() != NOP) {
        continue;
      }
      session->end();
      if (!session->begin(remote_addr, port, userid, password)) {
        FTPLogger.writeLog(LOG_ERROR, "FTPClient", "Connect failed");
        session->end();
        return nullptr;
      }
      session->setUseTypeCommand(use_type_command);
      return session;
    }
    FTPLogger.writeLog(LOG_ERROR, "FTPClient", "No available sessions");
    return nullptr;
  }

  /// Provides a session with an open data connection which is reserved until
  /// the transfer has ended
  FTPSession<ClientType> *openDataSession() override {
    FTPSession<ClientType> *result = idleSession();
    if (result == nullptr) return nullptr;
    if (!result->passv()) {
      result->endTransfer("passv failed");
      return nullptr;
    }
    result->reserve();
    return result;
  }

 protected:
  FTPSession<ClientType> *sessions[FTP_MAX_SESSIONS] = {nullptr};
  IPAddress remote_addr;
  const char *userid = nullptr;
  const char *password = nullptr;
  int port;
  bool use_type_command = false;
};

}  // namespace tinyftp
