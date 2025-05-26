#pragma once

#include "Arduino.h"
#include "Stream.h"
#include "FTPBasicAPI.h"  // You'll need to create this or include the API definitions

namespace ftp_client {

/**
 * @brief FTPFile
 * A single file which supports read and write operations. This class is implemented
 * as an Arduino Stream and therefore provides all corresponding functionality
 * @author Phil Schatzmann
 */
class FTPFile : public Stream {
 public:
  FTPFile() {
    is_open = false;
  }
  
  FTPFile(FTPBasicAPI *api_ptr, const char* name, FileMode mode, bool autoClose = true) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", name);
    auto_close = autoClose;
    if (name != nullptr)
      file_name = name;
    this->mode = mode;
    this->api_ptr = api_ptr;
  }
  
  ~FTPFile() {
    if (auto_close)
      close();
  }
  
  size_t write(uint8_t data) {
    if (!is_open)
      return 0;
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "write");
    if (mode == READ_MODE) {
      FTPLogger::writeLog(LOG_ERROR, "FTPFile", "Cannot write in READ_MODE");
      return 0;
    }
    Stream *result_ptr = api_ptr->write(file_name.c_str(), mode);
    return result_ptr->write(data);
  }
  
  size_t write(char* data, int len) {
    if (!is_open)
      return 0;
    int count = 0;
    for (int j = 0; j < len; j++) {
      if (write(data[j]) == 1) {
        count++;
      }
    }
    return count;
  }
  
  int read() {
    if (!is_open)
      return -1;
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "read");
    Stream *result_ptr = api_ptr->read(file_name.c_str());
    return result_ptr->read();
  }
  
  size_t readBytes(char *buf, size_t nbyte) {
    return readBytes((uint8_t *)buf, nbyte);
  }
  
  size_t readBytes(uint8_t *buf, size_t nbyte) {
    if (!is_open)
      return 0;
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "readBytes");
    memset(buf, 0, nbyte);
    Stream *result_ptr = api_ptr->read(file_name.c_str());
    return result_ptr->readBytes((char*)buf, nbyte);
  }
  
  size_t readln(char *buf, size_t nbyte) {
    if (!is_open)
      return 0;
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "readln");
    memset(buf, 0, nbyte);
    Stream *result_ptr = api_ptr->read(file_name.c_str());
    return result_ptr->readBytesUntil(eol[0], (char*)buf, nbyte);
  }
  
  int peek() {
    if (!is_open)
      return -1;
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "peek");
    Stream *result_ptr = api_ptr->read(file_name.c_str());
    return result_ptr->peek();
  }
  
  int available() {
    if (api_ptr->currentOperation() == IS_EOF)
      return 0;
    if (!is_open)
      return 0;

    char msg[80];
    Stream *result_ptr = api_ptr->read(file_name.c_str());
    int len = result_ptr->available();
    sprintf(msg, "available: %d", len);
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", msg);
    return len;
  }
  
  void flush() {
    if (!is_open)
      return;
    if (api_ptr->currentOperation() == WRITE_OP) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "flush");
      api_ptr->flush();
    }
  }
  
  void reopen() { 
    is_open = true; 
  }
  
  void close() {
    if (is_open) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "close");
      if (api_ptr->currentOperation() == WRITE_OP) {
        api_ptr->data_ptr->stop();
        const char* ok[] = {"226", "250", nullptr};
        api_ptr->checkResult(ok, "close-write", false);
      } else if (api_ptr->currentOperation() == READ_OP) {
        api_ptr->data_ptr->stop();
        const char* ok[] = {"226", "250", nullptr};
        api_ptr->checkResult(ok, "close-read", false);
      }
      api_ptr->setCurrentOperation(NOP);
      is_open = false;
    }
  }
  
  bool cancel() {
    if (is_open) {
      FTPLogger::writeLog(LOG_INFO, "FTPFile", "cancel");
      bool result = api_ptr->abort();
      is_open = false;
      return result;
    }
    return true;
  }
  
  const char* name() const {
    return file_name.c_str();
  }
  
  size_t size() const {
    if (!is_open)
      return 0;
    char msg[80];
    size_t size = api_ptr->size(file_name.c_str());
    sprintf(msg, "size: %ld", size);
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", msg);
    return size;
  }
  
  void setEOL(char* eol) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "setEOL");
    this->eol = eol;
  }
  
  bool isDirectory() const {
    if (!is_open)
      return false;
    FTPLogger::writeLog(LOG_DEBUG, "FTPFile", "isDirectory");
    return api_ptr->objectType(file_name.c_str()) == TypeDirectory;
  }
  
  operator bool() { 
    return is_open && file_name.length() > 0; 
  }

 protected:
  String file_name;
  const char *eol = "\n";
  FileMode mode;
  FTPBasicAPI *api_ptr;
  ObjectType object_type = TypeUndefined;
  bool is_open = true;
  bool auto_close = false;
};

} // namespace ftp_client