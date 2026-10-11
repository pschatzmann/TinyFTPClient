#pragma once

#include "Arduino.h"
#include "FTPBasicAPI.h"
#include "Stream.h"

namespace tinyftp {

/**
 * @brief FTPFile
 * A single file which supports read and write operations. This class is
 * implemented as an Arduino Stream and therefore provides all corresponding
 * functionality.
 *
 * Copies of a file share the same transfer: the transfer is ended when close()
 * is called or when the last copy is destroyed. A file must not be used after
 * the FTPClient which created it has been destroyed.
 * @author Phil Schatzmann
 */
class FTPFile : public Stream {
 public:
  FTPFile() = default;

  /// File which uses the provided session (if not null): otherwise a session
  /// is requested from the provider when the file is accessed
  FTPFile(FTPSessionProvider *provider, FTPBasicAPI *api, const char *name,
          FileMode mode) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", name);
    if (name != nullptr) file_name = name;
    this->mode = mode;
    this->provider = provider;
    transfer.attach(api);
    has_session = api != nullptr;
    is_open = provider != nullptr || api != nullptr;
  }

  size_t write(uint8_t data) override {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "write");
    Client *out = writeStream();
    return out == nullptr ? 0 : out->write(data);
  }

  size_t write(const uint8_t *data, size_t len) override {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "write");
    Client *out = writeStream();
    return out == nullptr ? 0 : out->write(data, len);
  }

  size_t write(const char *data, int len) {
    return write((const uint8_t *)data, len);
  }

  int read() override {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "read");
    Client *in = readStream();
    return in == nullptr ? -1 : in->read();
  }

  size_t readBytes(char *buf, size_t nbyte) {
    return readBytes((uint8_t *)buf, nbyte);
  }

  /// Reads up to nbyte bytes: returns less at the end of the file (or on
  /// timeout)
  size_t readBytes(uint8_t *buf, size_t nbyte) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "readBytes");
    Client *in = readStream();
    if (in == nullptr) return 0;
    memset(buf, 0, nbyte);
    return readData(in, buf, nbyte, -1);
  }

  /// Reads a line (w/o the end of line character)
  size_t readln(char *buf, size_t nbyte) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "readln");
    Client *in = readStream();
    if (in == nullptr) return 0;
    memset(buf, 0, nbyte);
    return readData(in, (uint8_t *)buf, nbyte, eol[0]);
  }

  int peek() override {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "peek");
    Client *in = readStream();
    return in == nullptr ? -1 : in->peek();
  }

  /// Provides the number of bytes which can be read. If no data has arrived
  /// yet, we wait up to the timeout (see setTimeout()), so 0 is only returned
  /// at the end of the file (or on timeout).
  int available() override {
    Client *in = readStream();
    if (in == nullptr) return 0;
    FTPBasicAPI *api = transfer.api();
    unsigned long start = millis();
    while (in->available() == 0 && api->dataConnected() &&
           millis() - start < getTimeout()) {
      delay(1);
    }
    int len = in->available();
    char msg[80];
    snprintf(msg, sizeof(msg), "available: %d", len);
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", msg);
    return len;
  }

  void flush() override {
    FTPBasicAPI *api = transfer.api();
    if (api != nullptr && api->currentOperation() == WRITE_OP) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "flush");
      api->flush();
    }
  }

  /// Opens the file again after a close(): the next access starts a new
  /// transfer
  void reopen() {
    if (provider == nullptr) return;
    close();
    is_open = true;
    has_session = false;
  }

  /// Ends the transfer (of all copies) and releases the session
  void close() {
    if (is_open) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "close");
      FTPBasicAPI *api = transfer.api();
      if (api != nullptr) api->endTransfer("close");
      transfer.release();
      is_open = false;
    }
  }

  /// Aborts the transfer (of all copies) and releases the session
  bool cancel() {
    bool result = true;
    if (is_open) {
      FTPLogger.writeLog(LOG_INFO, "FTPFile", "cancel");
      FTPBasicAPI *api = transfer.api();
      if (api != nullptr) result = api->abort();
      transfer.release();
      is_open = false;
    }
    return result;
  }

  const char *name() const { return file_name.c_str(); }

  size_t size() const {
    if (!is_open) return 0;
    FTPBasicAPI *api = commandSession();
    size_t size = api == nullptr ? 0 : api->size(file_name.c_str());
    char msg[80];
    snprintf(msg, sizeof(msg), "size: %lu", (unsigned long)size);
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", msg);
    return size;
  }

  void setEOL(char *eol) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "setEOL");
    this->eol = eol;
  }

  bool isDirectory() const {
    if (!is_open) return false;
    FTPLogger.writeLog(LOG_DEBUG, "FTPFile", "isDirectory");
    FTPBasicAPI *api = commandSession();
    return api != nullptr &&
           api->objectType(file_name.c_str()) == TypeDirectory;
  }

  operator bool() { return is_open && file_name.length() > 0; }

 protected:
  String file_name;
  const char *eol = "\n";
  FileMode mode = READ_MODE;
  FTPSessionProvider *provider = nullptr;
  FTPTransferHandle transfer;
  bool has_session = false;  // a session was assigned for the transfer
  bool is_open = false;

  /// Provides the session of the transfer: it is requested from the provider
  /// on the first access
  FTPBasicAPI *dataSession() {
    if (!has_session && provider != nullptr) {
      has_session = true;
      transfer.attach(provider->openDataSession());
    }
    return transfer.api();
  }

  /// Provides a session for commands (e.g. SIZE): we can not use the session
  /// of a running transfer
  FTPBasicAPI *commandSession() const {
    FTPBasicAPI *api = transfer.api();
    if (api != nullptr && api->currentOperation() == RESERVED_OP) return api;
    return provider != nullptr ? provider->idleSession() : nullptr;
  }

  /// Reads up to len bytes until the terminator (if >= 0, it is not stored):
  /// stops at the end of the data or on timeout
  size_t readData(Client *in, uint8_t *buf, size_t len, int terminator) {
    FTPBasicAPI *api = transfer.api();
    size_t n = 0;
    unsigned long start = millis();
    while (n < len) {
      int avail = in->available();
      if (avail > 0) {
        if (terminator < 0) {
          size_t chunk = len - n < (size_t)avail ? len - n : (size_t)avail;
          int got = in->read(buf + n, chunk);
          if (got > 0) n += got;
        } else {
          int c = in->read();
          if (c == terminator) break;
          if (c >= 0) buf[n++] = (uint8_t)c;
        }
        start = millis();
      } else if (!api->dataConnected() || millis() - start >= getTimeout()) {
        break;
      } else {
        delay(1);
      }
    }
    return n;
  }

  /// Starts the download if necessary and provides the data stream
  Client *readStream() {
    if (!is_open) return nullptr;
    if (mode != READ_MODE) {
      FTPLogger.writeLog(LOG_ERROR, "FTPFile", "Cannot read in WRITE_MODE");
      return nullptr;
    }
    FTPBasicAPI *api = dataSession();
    return api == nullptr ? nullptr : api->read(file_name.c_str());
  }

  /// Starts the upload if necessary and provides the data stream
  Client *writeStream() {
    if (!is_open) return nullptr;
    if (mode == READ_MODE) {
      FTPLogger.writeLog(LOG_ERROR, "FTPFile", "Cannot write in READ_MODE");
      return nullptr;
    }
    FTPBasicAPI *api = dataSession();
    return api == nullptr ? nullptr : api->write(file_name.c_str(), mode);
  }
};

}  // namespace tinyftp
