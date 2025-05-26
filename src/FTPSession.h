#include "FTPBasicAPI.h"

namespace ftp_client {
/**
 * @brief FTPSession
 * This class manages the FTP session, including command and data connections.
 * It provides methods to initialize the session, open data connections, and
 * manage the session lifecycle.
 * @note The class is designed to be used in an Arduino environment, where
 *       the Client interface is typically implemented by network libraries such
 *       as WiFi or Ethernet.
 * @tparam ClientType The type of client to use for command and data
 * connections. It should be a class that implements the Client interface.
 * @author Phil Schatzmann
 */
template <class ClientType>
class FTPSession {
 public:
  FTPSession() { FTPLogger::writeLog(LOG_DEBUG, "FTPSession"); }

  ~FTPSession() {
    FTPLogger::writeLog(LOG_DEBUG, "~FTPSession");
    end();
  }

  /// Initializes the session with the command client
  bool begin(IPAddress &address, int port, int dataPort, const char *username,
             const char *password) {
    if (!is_valid) return false;
    return basic_api.begin(&command_client, &data_client, address, port,
                           dataPort, username, password);
  }

  /// Opens the data connection
  bool passv() {
    if (!is_valid) return false;
    return basic_api.passv();
  }

  void end() {
    if (!is_valid) return;
    FTPLogger::writeLog(LOG_DEBUG, "FTPSession", "end");
    closeCommand();
    closeData();
  }

  void closeCommand() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSession", "endCommand");
    command_client.stop();
  }

  void closeData() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPSession", "endData");
    data_client.stop();
  }

  /// Returns the access to the basic API
  FTPBasicAPI &api() { return basic_api; }

  /// Returns true if the command client is connected
  operator bool() { return is_valid && command_client.connected(); }

  void setValid(bool valid) { is_valid = valid; }

 protected:
  ClientType command_client;
  ClientType data_client;
  FTPBasicAPI basic_api;
  bool is_valid = true;
};

}  // namespace ftp_client