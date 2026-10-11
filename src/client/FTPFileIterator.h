#pragma once

#include "Arduino.h"
#include "FTPBasicAPI.h"
#include "FTPFile.h"
#include "Stream.h"

namespace tinyftp {

/**
 * @brief FTPFileIterator
 * The file name iterator can be used to list all available files and
 * directories. We open a separate session for the ls operation so that we do
 * not need to keep the result in memory and we don't lose the data when we mix
 * it with read and write operations. The listed files use their own sessions.
 *
 * Copies of an iterator share the same listing: it is ended when all entries
 * have been read or when the last copy is destroyed.
 * @author Phil Schatzmann
 */
class FTPFileIterator {
 public:
  FTPFileIterator() = default;

  FTPFileIterator(FTPSessionProvider *provider, FTPBasicAPI *api,
                  const char *dir, FileMode mode) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator()");
    this->provider = provider;
    if (dir != nullptr) this->directory_name = dir;
    this->file_mode = mode;
    transfer.attach(api);
  }

  FTPFileIterator &begin() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator", "begin");
    FTPBasicAPI *api = transfer.api();
    if (api != nullptr) {
      if (api->currentOperation() == RESERVED_OP) {
        stream_ptr = api->ls(directory_name.c_str());
      }
      readLine();
    } else {
      FTPLogger.writeLog(LOG_ERROR, "FTPFileIterator", "no session");
      buffer = "";
    }
    return *this;
  }

  FTPFileIterator &end() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator", "end");
    static FTPFileIterator end;
    end.buffer = "";
    return end;
  }

  FTPFileIterator &operator++() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator", "++");
    readLine();
    return *this;
  }

  FTPFileIterator operator++(int) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator", "++(int)");
    FTPFileIterator result = *this;
    readLine();
    return result;
  }

  /// Provides the listed file: it uses its own session when it is accessed
  FTPFile operator*() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator", "*");
    return FTPFile(provider, nullptr, buffer.c_str(), file_mode);
  }

  bool operator!=(const FTPFileIterator &comp) { return buffer != comp.buffer; }

  bool operator==(const FTPFileIterator &comp) { return buffer == comp.buffer; }

  bool operator>(const FTPFileIterator &comp) { return buffer > comp.buffer; }

  bool operator<(const FTPFileIterator &comp) { return buffer < comp.buffer; }

  bool operator>=(const FTPFileIterator &comp) { return buffer >= comp.buffer; }

  bool operator<=(const FTPFileIterator &comp) { return buffer <= comp.buffer; }

  const char *fileName() { return buffer.c_str(); }

 protected:
  FTPSessionProvider *provider = nullptr;
  FTPTransferHandle transfer;
  Stream *stream_ptr = nullptr;
  FileMode file_mode = READ_MODE;
  String directory_name;  // copy: the callers string might be temporary
  String buffer = "";

  /// Reads the next (non empty) entry: buffer is empty at the end of the list
  void readLine() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFileIterator", "readLine");
    buffer = "";
    FTPBasicAPI *api = transfer.api();
    if (api == nullptr || stream_ptr == nullptr) return;
    while (buffer.length() == 0) {
      if (!readDataLine(api)) {
        // End of ls: the server has closed the data connection
        api->endTransfer("ls-end");
        transfer.release();
        return;
      }
    }
    FTPLogger.writeLog(LOG_DEBUG, "line", buffer.c_str());
  }

  /// Reads a line from the data connection into buffer: returns false at the
  /// end of the data
  bool readDataLine(FTPBasicAPI *api) {
    String line;
    unsigned long start = millis();
    while (true) {
      if (stream_ptr->available() > 0) {
        int c = stream_ptr->read();
        if (c == '\n') break;
        if (c >= 0) line += (char)c;
        start = millis();
      } else if (!api->dataConnected()) {
        if (line.length() == 0) return false;
        break;  // last line without line feed
      } else if (millis() - start >= FTP_REPLY_TIMEOUT_MS) {
        FTPLogger.writeLog(LOG_ERROR, "FTPFileIterator", "timeout");
        if (line.length() == 0) return false;
        break;
      } else {
        delay(1);
      }
    }
    // For Windows compatibility, remove trailing \r
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    buffer = line;
    return true;
  }
};

}  // namespace tinyftp
