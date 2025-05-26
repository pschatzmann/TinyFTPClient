#pragma once

#include "Arduino.h"
#include "Client.h"
#include "IPAddress.h"
#include "Stream.h"
#include "FTPBasicAPI.h"
#include "FTPFile.h"
#include "FTPFileIterator.h"

namespace ftp_client {

/**
 * @brief FTPClient
 * Basic FTP access class which supports directory operations and the opening
 * of files
 * @author Phil Schatzmann
 */
class FTPClient {
 public:
  /// Default constructor
  FTPClient(Client &command, Client &data, int port = FTP_COMMAND_PORT,
            int dataPort = FTP_DATA_PORT) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPClient");
    init(&command, &data, port, dataPort);
  }
  
  
  /// Opens the FTP connection
  bool begin(IPAddress remote_addr, const char* user = "anonymous", const char* password = nullptr) {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "begin");
    this->userid = user;
    this->password = password;
    this->remote_addr = remote_addr;

    return api.open(this->command_ptr, this->data_ptr, remote_addr, this->port, this->data_port, user, password);
  }
  
  /// Close the sessions by calling QUIT or BYE
  bool end() {
    FTPLogger::writeLog( LOG_INFO, "FTPClient", "end");
    bool result = api.quit();

    api.setCurrentOperation(NOP);

    if (command_ptr) command_ptr->stop();
    if (data_ptr) data_ptr->stop();
    return result;
  }
  
  /// Get the file
  FTPFile open(const char *filename, FileMode mode = READ_MODE, bool autoClose = false) {
    char msg[200];
    snprintf(msg, sizeof(msg), "open: %s", filename);
    FTPLogger::writeLog(LOG_INFO, "FTPClient", msg);
    
    // Abort any open processing
    api.abort();
    
    // Open new data connection
    api.passv();

    api.setCurrentOperation(NOP);

    return FTPFile(&api, filename, mode, autoClose);
  }
  
  /// Create the requested directory hierarchy--if intermediate directories
  /// do not exist they will be created.
  bool mkdir(const char *filepath) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "mkdir");
    return api.mkdir(filepath);
  }
  
  /// Delete the file
  bool remove(const char *filepath) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "remove");
    return api.del(filepath);
  }
  
  /// Removes a directory
  bool rmdir(const char *filepath) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "rmdir");
    return api.rmd(filepath);
  }
  
  /// Lists all file names in the specified directory
  FTPFileIterator ls(const char* path, FileMode mode = WRITE_MODE) {
    FTPLogger::writeLog(LOG_INFO, "FTPClient", "ls");
    // Abort any open processing
    api.abort();
    
    // Open new data connection
    api.passv();

    FTPFileIterator it(&api, path, mode);
    return it;
  }
  
  /// Switch to binary mode
  bool binary() { 
    return api.binary(); 
  }
  
  /// Switch to ascii mode
  bool ascii() { 
    return api.ascii(); 
  }
  
  /// Binary or ascii with type command
  bool type(const char *str) { 
    return api.type(str); 
  }
  
  /// Abort a current file transfer
  bool abort() { 
    return api.abort(); 
  }
  

 protected:
  FTPBasicAPI api;
  Client *command_ptr = nullptr;
  Client *data_ptr = nullptr;
  IPAddress remote_addr;
  const char *userid = nullptr;
  const char *password = nullptr;
  int port;
  int data_port;
  bool cleanup_clients;
  bool auto_close = true;

  /// Initialize the client
  void init(Client *command, Client *data, int port = FTP_COMMAND_PORT,
            int dataPort = FTP_DATA_PORT) {
    FTPLogger::writeLog( LOG_DEBUG, "FTPClient");
    this->command_ptr = command;
    this->data_ptr = data;
    this->port = port;
    this->data_port = data_port;
  }

};

} // namespace ftp_client