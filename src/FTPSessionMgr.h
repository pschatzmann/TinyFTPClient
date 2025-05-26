#pragma once
#include "FTPSession.h"
#include "IPAddress.h"

namespace ftp_client {
/**
 * @brief FTPSessionMgr
 * This class manages multiple FTP sessions, allowing for concurrent operations
 * and session reuse. It provides methods to begin a session, end all sessions,
 * and retrieve an available session for FTP operations.
 * @tparam ClientType The type of client to use for command and data
 * connections. It should be a class that implements the Client interface.
 * @author Phil Schatzmann
 */

template <class ClientType>
class FTPSessionMgr {
 public:
  FTPSessionMgr() { FTPLogger::writeLog(LOG_DEBUG, "FTPSessionMgr"); }

  ~FTPSessionMgr() {
    FTPLogger::writeLog(LOG_DEBUG, "~FTPSessionMgr");
    end();
  }

  /// Initializes the session manager with the FTP server details
  bool begin(IPAddress &address, int port, const char *username,
             const char *password) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSessionMgr", "createSession");
    this->address = address;
    this->port = port;
    this->username = username;
    this->password = password;
    return true;
  }

  void end() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSessionMgr", "end");
    for (int i = 0; i < FTP_MAX_SESSIONS; i++) {
      if (sessions[i] != nullptr) {
        FTPSession<ClientType> &session = *sessions[i];
        session.api().quit();  // Send QUIT command to the server
        session.end();
        delete sessions[i];
        sessions[i] = nullptr;
      }
    }
  }

  /// Provides a session for the FTP operations
  FTPSession<ClientType> &session() {
    for (int i = 0; i < FTP_MAX_SESSIONS; i++) {
      if (sessions[i] == nullptr) {
        sessions[i] = new FTPSession<ClientType>();
        if (sessions[i]->begin(address, port, username, password)) {
          return *sessions[i];
        } else {
          delete sessions[i];
          sessions[i] = nullptr;
        }
      } else if (sessions[i]->api().currentOperation() == NOP) {
        // Reuse existing session if it is not currently in use
        return *sessions[i];
      }
    }
    FTPLogger::writeLog(LOG_ERROR, "FTPSessionMgr", "No available sessions");
    static FTPSession<ClientType> empty_session;
    empty_session.setValid(false);
    return empty_session;  // No available session
  }

  /// Aborts the current operation in all sessions
  bool abort(CurrentOperation op) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSessionMgr", "abort");
    for (int i = 0; i < FTP_MAX_SESSIONS; i++) {
      if (sessions[i] != nullptr &&
          sessions[i]->api().currentOperation() == op) {
        return sessions[i]->api().abort();
      }
    }
    return false;  // No session found with the specified operation
  }

  /// Count the sessions
  int count() {
    int result = 0;
    for (auto &session : sessions) {
      if (session != nullptr) {
        result++;
      }
    }
    return result;
  }
  
  /// Count the sessions with a specific current operation
  int count(CurrentOperation op) {
    int result = 0;
    for (auto &session : sessions) {
      if (session != nullptr && session->api().currentOperation() == op) {
        result++;
      }
    }
    return result;
  }

 protected:
  FTPSession<ClientType> *sessions[FTP_MAX_SESSIONS] = {nullptr};
  IPAddress address;
  int port;
  const char *username;
  const char *password;
};

}  // namespace ftp_client