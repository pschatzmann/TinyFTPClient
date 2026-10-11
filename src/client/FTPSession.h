#pragma once

#include "FTPBasicAPI.h"

namespace tinyftp {
/**
 * @brief FTPSession
 * An FTPBasicAPI which owns its command and data connections.
 * @note The class is designed to be used in an Arduino environment, where
 *       the Client interface is typically implemented by network libraries such
 *       as WiFi or Ethernet.
 * @tparam ClientType The type of client to use for command and data
 * connections. It should be a class that implements the Client interface.
 * @author Phil Schatzmann
 */
template <class ClientType>
class FTPSession : public FTPBasicAPI {
 public:
  FTPSession() { FTPLogger.writeLog(LOG_DEBUG, "FTPSession"); }

  ~FTPSession() {
    FTPLogger.writeLog(LOG_DEBUG, "~FTPSession");
    end();
  }

  /// Opens the command connection and logs in
  bool begin(IPAddress &address, int port, const char *username,
             const char *password) {
    return FTPBasicAPI::begin(&command_client, &data_client, address, port,
                              username, password);
  }

  /// Closes the command and data connections
  void end() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPSession", "end");
    command_client.stop();
    data_client.stop();
    is_open = false;
    resetTransfer();
  }

  /// Returns true if logged in and the command connection is still alive
  bool connected() { return is_open && command_client.connected(); }

  operator bool() { return connected(); }

 protected:
  ClientType command_client;
  ClientType data_client;
};

}  // namespace tinyftp
