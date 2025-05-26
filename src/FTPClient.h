#pragma once

#include "Arduino.h"
#include "Client.h"
#include "FTPBasicAPI.h"
#include "FTPFile.h"
#include "FTPFileIterator.h"
#include "FTPSessionMgr.h"
#include "IPAddress.h"
#include "Stream.h"

namespace ftp_client {

/**
 * @brief FTPClient
 * Basic FTP access class which supports directory operations and the opening
 * of files
 * @author Phil Schatzmann
 */
template <class ClientType>
class FTPClient {
 public:
  /// Default constructor
  FTPClient(int port = FTP_COMMAND_PORT) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPClient");
    setPort(port);
  }

  /// Opens the FTP connection
  bool begin(IPAddress remote_addr, const char *user = "anonymous",
             const char *password = nullptr) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "begin");
    this->userid = user;
    this->password = password;
    this->remote_addr = remote_addr;
    return mgr.begin(remote_addr, this->port, user, password);
  }

  /// Close the sessions by calling QUIT or BYE
  void end() {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "end");
    mgr.end();
  }

  /// Open a file
  FTPFile open(const char *filename, FileMode mode = READ_MODE,
               bool autoClose = false) {
    char msg[200];
    snprintf(msg, sizeof(msg), "open: %s", filename);
    FTPLogger::writeLog(LOG_INFO, "FTPClient", msg);

    FTPBasicAPI &api = mgr.session().api();

    // Open new data connection
    api.passv();

    return FTPFile(&api, filename, mode, autoClose);
  }

  /// Create the requested directory hierarchy--if intermediate directories
  /// do not exist they will be created.
  bool mkdir(const char *filepath) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "mkdir");
    FTPBasicAPI &api = mgr.session().api();
    if (!api) return false;
    return api.mkdir(filepath);
  }

  /// Delete the file
  bool remove(const char *filepath) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "remove");
    FTPBasicAPI &api = mgr.session().api();
    if (!api) return false;
    return api.del(filepath);
  }

  /// Removes a directory
  bool rmdir(const char *filepath) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "rmdir");
    FTPBasicAPI &api = mgr.session().api();
    if (!api) return false;
    return api.rmd(filepath);
  }

  /// Lists all file names in the specified directory
  FTPFileIterator ls(const char *path, FileMode mode = WRITE_MODE) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "ls");
    FTPBasicAPI &api = mgr.session().api();

    // Open new data connection
    api.passv();

    FTPFileIterator it(&api, path, mode);
    return it;
  }

  /// Switch to binary mode
  bool binary() {
    FTPBasicAPI &api = mgr.session().api();
    if (!api) return false;
    return api.binary();
  }

  /// Switch to ascii mode
  bool ascii() {
    FTPBasicAPI &api = mgr.session().api();
    if (!api) return false;
    return api.ascii();
  }

  /// Binary or ascii with type command
  bool type(const char *str) {
    FTPBasicAPI &api = mgr.session().api();
    if (!api) return false;
    return api.type(str);
  }

  void setPort(int port) {
    this->port = port;
  }

 protected:
  FTPSessionMgr<ClientType> mgr;
  IPAddress remote_addr;
  const char *userid = nullptr;
  const char *password = nullptr;
  int port;
  bool cleanup_clients;
  bool auto_close = true;

};

}  // namespace ftp_client