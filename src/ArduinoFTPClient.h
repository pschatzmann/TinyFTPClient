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
#  define FTP_USE_NAMESPACE true
#endif

namespace ftp_client {

// Common Constants
static const int MAXFILE_NAME_LENGTH = 512;
static const int COMMAND_PORT = 21;
static const int DATA_PORT = 1000;

/// @brief File Mode
enum FileMode { READ_MODE, WRITE_MODE, WRITE_APPEND_MODE };
enum CurrentOperation { READ_OP, WRITE_OP, LS_OP, NOP };
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
      if (occur == n)
        return i;
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
  virtual bool open(Client *cmd, Client *dat, IPAddress &address, int port,
                    int data_port, const char *username, const char *password);
  virtual bool quit();
  virtual bool connected();
  virtual bool passv();
  virtual bool binary();
  virtual bool ascii();
  virtual bool type(const char *type);
  virtual bool del(const char *file);
  virtual bool mkdir(const char *dir);
  virtual bool rmd(const char *dir);
  virtual bool abort();
  virtual Stream *read(const char *file_name);
  virtual Stream *write(const char *file_name, FileMode mode);
  virtual Stream *ls(const char *file_name);
  virtual void closeData();
  virtual void setCurrentOperation(CurrentOperation op);
  virtual CurrentOperation currentOperation() { return current_operation; }
  virtual void flush();
  virtual size_t size(const char *dir);
  virtual ObjectType objectType(const char *file);

protected:
  virtual bool connect(IPAddress adr, int port, Client *client,
                       bool doCheckResult = false);
  virtual bool cmd(const char *command, const char *par, const char *expected,
                   bool wait_for_data = true);
  virtual bool cmd(const char *command_str, const char *par,
                   const char *expected[], bool wait_for_data = true);
  virtual void checkClosed(Client *stream);
  virtual bool checkResult(const char *expected[], const char *command_for_log,
                           bool wait_for_data = true);
// currently running op -> do we need to cancel ?
  CurrentOperation current_operation = NOP; 
  Client *command_ptr = nullptr; // Client for commands
  Client *data_ptr = nullptr;    // Client for upload and download of files
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
  FTPFile() = default;
  FTPFile(FTPBasicAPI *api_ptr, const char *name, FileMode mode, bool autoClose=true);
  FTPFile(FTPFile &cpy);
  FTPFile(FTPFile &&move);
  ~FTPFile();
  FTPFile &operator=(const FTPFile &);
  virtual size_t write(uint8_t data);
  virtual size_t write(char *data, int len);
  virtual int read();
  virtual int read(void *buf, size_t nbyte);
  virtual int readln(void *buf, size_t nbyte);

  virtual int peek();
  virtual int available();
  virtual void flush();
  virtual void close();
  virtual const char *name() const;
  virtual size_t size();
  virtual void setEOL(char *eol);
  virtual bool isDirectory();

  operator bool() { return is_open; }

protected:
  const char *file_name = nullptr;
  const char *eol = "\n";
  FileMode mode;
  FTPBasicAPI *api_ptr;
  ObjectType object_type = TypeUndefined;
  bool is_open = true;
  bool auto_close = true;
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
  FTPFileIterator(FTPBasicAPI *api, const char *dir, FileMode mode);
  FTPFileIterator(FTPFileIterator &copy);
  FTPFileIterator(FTPFileIterator &&copy);
  ~FTPFileIterator();
  virtual FTPFileIterator &begin();
  virtual FTPFileIterator end();
  virtual FTPFileIterator &operator++();
  virtual FTPFileIterator &operator++(int _na);
  virtual FTPFile operator* ();
  virtual bool operator!=(const FTPFileIterator &comp) {
    return strcmp(this->buffer.c_str(), comp.buffer.c_str()) != 0;
  }
  virtual bool operator>(const FTPFileIterator &comp) {
    return strcmp(this->buffer.c_str(), comp.buffer.c_str()) > 0;
  }
  virtual bool operator<(const FTPFileIterator &comp) {
    return strcmp(this->buffer.c_str(), comp.buffer.c_str()) < 0;
  }
  virtual bool operator>=(const FTPFileIterator &comp) {
    return strcmp(this->buffer.c_str(), comp.buffer.c_str()) >= 0;
  }
  virtual bool operator<=(const FTPFileIterator &comp) {
    return strcmp(this->buffer.c_str(), comp.buffer.c_str()) <= 0;
  }

  const char* fileName() {return buffer.c_str();}

protected:
  virtual void readLine();

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
  virtual bool begin(IPAddress remote_addr, const char *user = "anonymous",
                     const char *password = nullptr);
  /// Close the sessions by calling QUIT or BYE
  virtual bool end();
  /// get the file
  virtual FTPFile open(const char *filename, FileMode mode = READ_MODE);
  /// Create the requested directory heirarchy--if intermediate directories
  /// do not exist they will be created.
  virtual bool mkdir(const char *filepath);
  /// Delete the file.
  virtual bool remove(const char *filepath);
  /// Removes a directory
  virtual bool rmdir(const char *filepath);
  /// lists all file names in the specified directory
  virtual FTPFileIterator ls(const char *path, FileMode mode = WRITE_MODE);
  /// Switch to binary mode
  virtual bool binary() { return api.binary();}
  /// Switch to ascii mode
  virtual bool ascii() { return api.ascii();}
  /// Binary or ascii with type command
  virtual bool type(const char *str) { return api.type(str);}

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

} // ns

#if FTP_USE_NAMESPACE
using namespace ftp_client;
#endif