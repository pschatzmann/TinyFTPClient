#pragma once
#include "FTPSession.h"

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
  void begin(IPAddress &address, int port, int dataPort, const char *username,
             const char *password) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSessionMgr", "createSession");
    this->address = address;
    this->port = port;
    this->dataPort = dataPort;
    this->username = username;
    this->password = password;
  }

  void end() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSessionMgr", "end");
    for (auto &session : sessions) {
      if (session != nullptr) {
        session->end();
        delete s;
        session = nullptr;
      }
    }
  }

  /// Provides a session for the FTP operations
  FTPSession<ClientType> &session() {
    for (int i = 0; i < FTP_MAX_SESSIONS; i++) {
      if (session[i] == nullptr) {
        session[i] = new FTPSession();
        if (session[i]->begin(address, port, dataPort, username, password)) {
          return *session[i];
        } else {
          delete session[i];
          session[i] = nullptr;
        }
      } else if (session[i]->currentOperation() == NOP) {
        // Reuse existing session if it is not currently in use
        return *session[i];
      }
    }
    FTPLogger::writeLog(LOG_ERROR, "FTPSessionMgr", "No available sessions");
    static FTPSession<ClientType> empty_session;
    empty_session.setValid(false);
    return empty_session;  // No available session
  }

 protected:
  FTPSession<ClientType> *sessions[FTP_MAX_SESSIONS] = {nullptr};
  IPAddress &address;
  int port;
  int dataPort;
  const char *username;
  const char *password;
}