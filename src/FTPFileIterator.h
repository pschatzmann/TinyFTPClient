#pragma once

#include "Arduino.h"
#include "FTPBasicAPI.h"  // Include for FTPBasicAPI class
#include "FTPFile.h"      // Include for FTPFile class
#include "Stream.h"

namespace ftp_client {

/**
 * @brief FTPFileIterator
 * The file name iterator can be used to list all available files and
 * directories. We open a separate session for the ls operation so that we do
 * not need to keep the result in memory and we don't lose the data when we mix
 * it with read and write operations.
 * @author Phil Schatzmann
 */
class FTPFileIterator {
 public:
  FTPFileIterator() = default;

  FTPFileIterator(FTPBasicAPI *api, const char *dir, FileMode mode) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator()");
    this->directory_name = dir;
    this->api_ptr = api;
    this->file_mode = mode;
  }

  FTPFileIterator &begin() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator", "begin");
    if (api_ptr != nullptr && directory_name != nullptr) {
      stream_ptr = api_ptr->ls(directory_name);
      readLine();
    } else {
      FTPLogger::writeLog(LOG_ERROR, "FTPFileIterator", "api_ptr is null");
      buffer = "";
    }
    return *this;
  }

  FTPFileIterator &end() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator", "end");
    static FTPFileIterator end;
    end.buffer = "";
    return end;
  }

  FTPFileIterator &operator++() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator", "++");
    readLine();
    return *this;
  }

  FTPFileIterator &operator++(int na) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator", "++(1)");
    for (int j = 0; j < na; j++) readLine();
    return *this;
  }

  FTPFile operator*() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator", "*");
    // return file that does not autoclose
    return FTPFile(api_ptr, buffer.c_str(), file_mode, false);
  }

  bool operator!=(const FTPFileIterator &comp) { return buffer != comp.buffer; }

  bool operator==(const FTPFileIterator &comp) { return buffer == comp.buffer; }

  bool operator>(const FTPFileIterator &comp) { return buffer > comp.buffer; }

  bool operator<(const FTPFileIterator &comp) { return buffer < comp.buffer; }

  bool operator>=(const FTPFileIterator &comp) { return buffer >= comp.buffer; }

  bool operator<=(const FTPFileIterator &comp) { return buffer <= comp.buffer; }

  const char *fileName() { return buffer.c_str(); }

 protected:
  void readLine() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPFileIterator", "readLine");
    buffer = "";
    if (stream_ptr != nullptr) {
      buffer = stream_ptr->readStringUntil('\n');
      if (buffer.endsWith("\r")) buffer.remove(buffer.length() - 1);
      FTPLogger::writeLog(LOG_DEBUG, "line", buffer.c_str());

      // End of ls !!!
      if (api_ptr->currentOperation() == LS_OP && buffer[0] == 0) {
        // Close data connection
        api_ptr->closeData();
        // Reset operation status
        api_ptr->setCurrentOperation(NOP);

        // Get final status
        const char *ok[] = {"226", "250", nullptr};
        api_ptr->checkResult(ok, "ls-end", true);
      }
    } else {
      FTPLogger::writeLog(LOG_ERROR, "FTPFileIterator", "stream_ptr is null");
    }
  }

  FTPBasicAPI *api_ptr = nullptr;
  Stream *stream_ptr = nullptr;
  FileMode file_mode;
  const char *directory_name = "";
  String buffer = "";
};

}  // namespace ftp_client
