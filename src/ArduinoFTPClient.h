/**
 * @file ArduinoFTPClient.h
 * @author Phil Schatzmann (phil.schatzmann@gmail.com)
 * @brief A simple FTP client for Arduino using Streams
 *
 * Sometimes you might have the need to write your sensor data into a remote
 * file or for your local processing logic you need more data than you can store
 * on your local Arduino device.
 *
 * If your device is connected to the Internet, this library is coming to your
 * rescue: The FTP protocol is one of oldest communication protocols. It is easy
 * to implement and therefore rather efficient and you can find free server
 * implementations on all platforms.
 *
 * Here is a simple but powerful FTP client library that uses a Stream based
 * API.
 *
 * We support
 *
 * - The reading of Remote Files (File Download)
 * - The writing to Remote Files (File Upload)
 * - The listing of Remote Files and Directories
 * - The creation and deletion of remote directories
 * - The deletion of remote files
 *
 * @version 1.0
 * @date 2020-Nov-07
 *
 * @copyright Copyright (c) 2020 Phil Schatzmann
 *
 */

#pragma once

#include "Arduino.h"
#include "Client.h"
#include "IPAddress.h"
#include "Stream.h"

#ifndef FTP_USE_NAMESPACE
#define FTP_USE_NAMESPACE true
#endif

#ifndef FTP_ABPRT_DELAY_MS 
#define FTP_ABORT_DELAY_MS 300
#endif

#ifndef FTP_COMMAND_BUFFER_SIZE 
#define FTP_COMMAND_BUFFER_SIZE 300
#endif

#ifndef FTP_RESULT_BUFFER_SIZE 
#define FTP_RESULT_BUFFER_SIZE 300
#endif


namespace ftp_client {

// Common Constants
static const int MAXFILE_NAME_LENGTH = 512;
static const int COMMAND_PORT = 21;
static const int DATA_PORT = 1000;

/// @brief File Mode
enum FileMode { READ_MODE, WRITE_MODE, WRITE_APPEND_MODE };
enum CurrentOperation { READ_OP, WRITE_OP, LS_OP, NOP, IS_EOF };
enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
enum ObjectType { TypeFile, TypeDirectory, TypeUndefined };

/**
 * @brief CStringFunctions
 * We implemented some missing C based string functions for character arrays
 */

class CStringFunctions {
 public:
  static int findNthInStr(char *str, char ch, int n) {
    int occur = 0;
    // Loop to find the Nth
    // occurence of the character
    for (unsigned int i = 0; i < strlen(str); i++) {
      if (str[i] == ch) {
        occur += 1;
      }
      if (occur == n) return i;
    }
    return -1;
  }

  static int readln(Stream &stream, char *str, int maxLen) {
    int len = 0;
    if (maxLen > stream.available()) {
      maxLen = stream.available();
    }
    for (int j = 0; j < maxLen; j++) {
      len = j;
      char c = stream.read();
      if (c == 0 || c == '\n') {
        break;
      }
      str[j] = c;
    }
    memset(str + len, 0, maxLen - len);
    return len;
  }
};

/**
 * @brief FTPLogger
 * To activate logging define the output stream e.g. with
 * FTPLogger.setOutput(Serial); and (optionally) set the log level
 */
class FTPLogger {
 public:
  static void setLogLevel(LogLevel level);
  static LogLevel getLogLevel();
  static void setOutput(Stream &out);
  static void writeLog(LogLevel level, const char *module,
                       const char *msg = nullptr);

 protected:
  static LogLevel min_log_level;
  static Stream *out_ptr;
};

/**
 * @brief FTPBasicAPI
 * Implementation of Low Level FTP protocol. In order to simplify the logic we
 * always use Passive FTP where it is our responsibility to open the data
 * conection.
 */

class FTPBasicAPI {
  friend class FTPFile;

 public:
  FTPBasicAPI();
  ~FTPBasicAPI();
  bool open(Client *cmd, Client *dat, IPAddress &address, int port,
            int data_port, const char *username, const char *password);
  bool quit();
  bool connected();
  bool passv();
  bool binary();
  bool ascii();
  bool type(const char *type);
  bool del(const char *file);
  bool mkdir(const char *dir);
  bool rmd(const char *dir);
  bool abort();
  Stream *read(const char *file_name);
  Stream *write(const char *file_name, FileMode mode);
  Stream *ls(const char *file_name);
  void closeData();
  void setCurrentOperation(CurrentOperation op);
  CurrentOperation currentOperation() { return current_operation; }
  void flush();
  size_t size(const char *dir);
  ObjectType objectType(const char *file);
  bool checkResult(const char *expected[], const char *command_for_log,
                   bool wait_for_data = true);

 protected:
  bool connect(IPAddress adr, int port, Client *client,
               bool doCheckResult = false);
  bool cmd(const char *command, const char *par, const char *expected,
           bool wait_for_data = true);
  bool cmd(const char *command_str, const char *par, const char *expected[],
           bool wait_for_data = true);
  bool checkClosed(Client *stream);
  // currently running op -> do we need to cancel ?
  CurrentOperation current_operation = NOP;
  Client *command_ptr = nullptr;  // Client for commands
  Client *data_ptr = nullptr;     // Client for upload and download of files
  IPAddress remote_address;
  bool is_open;
  char result_reply[80];
};

/**
 * @brief FTPFile
 * A single file which supports read and write operations. This class is
 * implemented as an Arduino Stream and therfore provides all corresponding
 * functionality
 *
 */

class FTPFile : public Stream {
 public:
  FTPFile() {is_open = false;}
  FTPFile(FTPBasicAPI *api_ptr, const char *name, FileMode mode,
          bool autoClose = true);
  ~FTPFile();
  size_t write(uint8_t data);
  size_t write(char *data, int len);
  int read();
  size_t readBytes(char *buf, size_t nbyte) {
    return readBytes((uint8_t *)buf, nbyte);
  }
  size_t readBytes(uint8_t *buf, size_t nbyte);
  size_t readln(char *buf, size_t nbyte);

  int peek();
  int available();
  void flush();
  void reopen() { is_open = true; }
  void close();
  const char *name() const;
  size_t size() const;
  void setEOL(char *eol);
  bool isDirectory() const;

  operator bool() { return is_open && file_name.length() > 0; }

 protected:
  String file_name;
  const char *eol = "\n";
  FileMode mode;
  FTPBasicAPI *api_ptr;
  ObjectType object_type = TypeUndefined;
  bool is_open = true;
  bool auto_close = false;
};

/**
 * @brief FTPFileIterator
 * The file name iterator can be used to list all available files and
 * directories. We open a separate session for the ls operation so that we do
 * not need to keep the result in memory and we dont loose the data when we mix
 * it with read and write operations.
 */
class FTPFileIterator {
 public:
  FTPFileIterator() = default;
  FTPFileIterator(FTPBasicAPI *api, const char *dir, FileMode mode);
  FTPFileIterator &begin();
  FTPFileIterator &end();
  FTPFileIterator &operator++();
  FTPFileIterator &operator++(int _na);
  FTPFile operator*();
  bool operator!=(const FTPFileIterator &comp) {
    return buffer != comp.buffer;
  }
  bool operator==(const FTPFileIterator &comp) {
    return buffer == comp.buffer;
  }
  bool operator>(const FTPFileIterator &comp) {
    return buffer > comp.buffer;
  }
  bool operator<(const FTPFileIterator &comp) {
    return buffer < comp.buffer;
  }
  bool operator>=(const FTPFileIterator &comp) {
    return buffer >= comp.buffer;
  }
  bool operator<=(const FTPFileIterator &comp) {
    return buffer <= comp.buffer;
  }

  const char *fileName() { return buffer.c_str(); }

 protected:
  void readLine();

  FTPBasicAPI *api_ptr = nullptr;
  Stream *stream_ptr = nullptr;
  FileMode file_mode;
  const char *directory_name = "";
  String buffer = "";
};

/**
 * @brief FTPClient
 * Basic FTP access class which supports directory operatations and the opening
 * of a files
 *
 */
class FTPClient {
 public:
  /// default construcotr
  FTPClient(Client &command, Client &data, int port = COMMAND_PORT,
            int data_port = DATA_PORT);
  /// opens the ftp connection
  bool begin(IPAddress remote_addr, const char *user = "anonymous",
             const char *password = nullptr);
  /// Close the sessions by calling QUIT or BYE
  bool end();
  /// get the file
  FTPFile open(const char *filename, FileMode mode = READ_MODE,
               bool autoClose = false);
  /// Create the requested directory heirarchy--if intermediate directories
  /// do not exist they will be created.
  bool mkdir(const char *filepath);
  /// Delete the file.
  bool remove(const char *filepath);
  /// Removes a directory
  bool rmdir(const char *filepath);
  /// lists all file names in the specified directory
  FTPFileIterator ls(const char *path, FileMode mode = WRITE_MODE);
  /// Switch to binary mode
  bool binary() { return api.binary(); }
  /// Switch to ascii mode
  bool ascii() { return api.ascii(); }
  /// Binary or ascii with type command
  bool type(const char *str) { return api.type(str); }
  /// Abort a current file transfer
  bool abort() { 
    return api.abort(); 
}

 protected:
  void init(Client *command, Client *data, int port = COMMAND_PORT,
            int data_port = DATA_PORT);
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

};

}  // namespace ftp_client

#if FTP_USE_NAMESPACE
using namespace ftp_client;
#endif