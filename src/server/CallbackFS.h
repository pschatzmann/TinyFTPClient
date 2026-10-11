#pragma once
#include <Arduino.h>

#include <functional>
#include <memory>
#include <vector>

#include "../common/FTPLogger.h"

namespace tinyftp {

class CallbackFSClass;

/// Directory entry which is provided by the list callback of CallbackFS
struct CallbackFileInfo {
  String name;  // file name (without directory)
  size_t size = 0;
  bool isDirectory = false;
};

/**
 * @brief CallbackFile
 * The file which is returned by CallbackFSClass::open(): a directory (with the
 * entries provided by the list callback), a directory entry, a file which is
 * read with the read callback, a file which is written with the write callback
 * or an invalid file.
 */
class CallbackFile {
 public:
  CallbackFile() = default;

  /// Directory entry
  CallbackFile(const CallbackFileInfo &info) : kind(Entry), info(info) {}

  /// Directory with its entries
  CallbackFile(const char *path, std::vector<CallbackFileInfo> entries)
      : kind(Directory),
        entries(std::make_shared<std::vector<CallbackFileInfo>>(entries)) {
    info.name = path;
    info.isDirectory = true;
  }

  /// File which is read or written via the callbacks of fs
  CallbackFile(CallbackFSClass *fs, const char *path, const char *mode,
               size_t size)
      : kind(mode[0] == 'r' ? Reader : Writer), fs(fs), mode(mode) {
    info.name = path;
    info.size = size;
  }

  operator bool() { return kind != Invalid; }
  bool isDirectory() { return info.isDirectory; }

  /// Provides the next entry of a directory (or an invalid file at the end)
  CallbackFile openNextFile() {
    if (!entries || pos >= entries->size()) return CallbackFile();
    return CallbackFile((*entries)[pos++]);
  }

  size_t size() { return info.size; }
  const char *name() { return info.name.c_str(); }

  /// Returns 1 while the end of the file has not been reached
  int available() { return kind == Reader && !eof ? 1 : 0; }

  /// Reads the next data via the read callback
  int read(uint8_t *data, size_t len);

  /// Provides the data to the write callback
  size_t write(const uint8_t *data, size_t len);

  /// Closes the file: reported as Close operation for read and written files
  void close();

 protected:
  enum Kind { Invalid, Directory, Entry, Reader, Writer };
  Kind kind = Invalid;
  CallbackFileInfo info;
  std::shared_ptr<std::vector<CallbackFileInfo>> entries;
  size_t pos = 0;  // next directory entry or read position
  CallbackFSClass *fs = nullptr;
  const char *mode = "r";
  bool eof = false;
};

/**
 * @brief CallbackFSClass
 * File system which can be used as storage of the FTPServer: it does not
 * store any files but logs each file operation and reports it to callbacks.
 * Use the CallbackFS object, e.g.
 *
 *   FTPServer<WiFiServer, CallbackFSClass> ftpServer(21, &CallbackFS);
 *
 * - The operation callback is called for all operations: its result is the
 *   result of exists(), remove(), mkdir(), rmdir() and rename().
 * - The optional list callback defines the directories and their entries.
 * - The optional read callback provides the content of the files (get).
 * - The optional write callback receives the uploaded files (put).
 * @author Phil Schatzmann
 */
class CallbackFSClass {
 public:
  /// File operations which are reported to the callback
  enum Operation { Open, Close, Exists, Remove, Mkdir, Rmdir, Rename };

  /// Operation callback: path is the file name (rename: the old name); param
  /// is the mode ("r", "w" or "a") for open and close, the new name for
  /// rename or nullptr
  using Callback =
      std::function<bool(Operation op, const char *path, const char *param)>;

  /// List callback: returns true if the path is a directory and adds its
  /// entries
  using ListCallback = std::function<bool(
      const char *path, std::vector<CallbackFileInfo> &entries)>;

  /// Read callback: copies up to len bytes of the file starting at pos into
  /// data and returns the number of bytes (0 at the end of the file). If data
  /// is nullptr, it returns the file size (or 0 if unknown) or -1 if the file
  /// does not exist.
  using ReadCallback = std::function<int(const char *path, size_t pos,
                                         uint8_t *data, size_t len)>;

  /// Write callback: receives the next len bytes of the uploaded file. It is
  /// called with data == nullptr when the file is opened: return false to
  /// reject the upload.
  using WriteCallback =
      std::function<bool(const char *path, const uint8_t *data, size_t len)>;

  /// Provides the single instance which is available as CallbackFS
  static CallbackFSClass &instance() {
    static CallbackFSClass fs;
    return fs;
  }

  /// Defines the callback which is called for all file operations
  void setCallback(Callback callback) { this->callback = callback; }

  /// Defines the callback which provides the directory entries
  void setListCallback(ListCallback callback) { listCallback = callback; }

  /// Defines the callback which provides the content of the files
  void setReadCallback(ReadCallback callback) { readCallback = callback; }

  /// Defines the callback which receives the content of uploaded files
  void setWriteCallback(WriteCallback callback) { writeCallback = callback; }

  /// Provides the name of the operation
  static const char *toString(Operation op) {
    switch (op) {
      case Open:
        return "open";
      case Close:
        return "close";
      case Exists:
        return "exists";
      case Remove:
        return "remove";
      case Mkdir:
        return "mkdir";
      case Rmdir:
        return "rmdir";
      case Rename:
        return "rename";
    }
    return "?";
  }

  CallbackFile open(const char *path) { return openFile(path, "r"); }

  /// Open with a mode string ("r", "w" or "a")
  CallbackFile open(const char *path, const char *mode) {
    const char *m = mode == nullptr || mode[0] == 'r' ? "r"
                    : mode[0] == 'a'                  ? "a"
                                                      : "w";
    return openFile(path, m);
  }

  bool exists(const char *path) { return report(Exists, path); }

  bool remove(const char *path) { return report(Remove, path); }

  bool mkdir(const char *path) { return report(Mkdir, path); }

  bool rmdir(const char *path) { return report(Rmdir, path); }

  bool rename(const char *from, const char *to) {
    return report(Rename, from, to);
  }

 protected:
  friend class CallbackFile;
  Callback callback;
  ListCallback listCallback;
  ReadCallback readCallback;
  WriteCallback writeCallback;

  CallbackFSClass() = default;

  CallbackFile openFile(const char *path, const char *mode) {
    report(Open, path, mode);
    if (mode[0] == 'r') {
      if (listCallback) {
        std::vector<CallbackFileInfo> entries;
        if (listCallback(path, entries)) return CallbackFile(path, entries);
      }
      if (readCallback) {
        int size = readCallback(path, 0, nullptr, 0);
        if (size >= 0) return CallbackFile(this, path, mode, size);
      }
    } else if (writeCallback && writeCallback(path, nullptr, 0)) {
      return CallbackFile(this, path, mode, 0);
    }
    return CallbackFile();
  }

  /// Logs the operation and calls the callback: returns its result
  bool report(Operation op, const char *path, const char *param = nullptr) {
    char msg[160];
    snprintf(msg, sizeof(msg), "%s %s%s%s", toString(op),
             path == nullptr ? "" : path, param == nullptr ? "" : " ",
             param == nullptr ? "" : param);
    FTPLogger.writeLog(LOG_INFO, "CallbackFS", msg);
    return callback ? callback(op, path, param) : false;
  }
};

inline int CallbackFile::read(uint8_t *data, size_t len) {
  if (kind != Reader || eof || !fs->readCallback) return 0;
  int result = fs->readCallback(info.name.c_str(), pos, data, len);
  if (result <= 0) {
    eof = true;
    return 0;
  }
  pos += result;
  return result;
}

inline size_t CallbackFile::write(const uint8_t *data, size_t len) {
  if (kind != Writer || !fs->writeCallback) return 0;
  return fs->writeCallback(info.name.c_str(), data, len) ? len : 0;
}

inline void CallbackFile::close() {
  if (kind == Reader || kind == Writer) {
    fs->report(CallbackFSClass::Close, info.name.c_str(), mode);
  }
  kind = Invalid;
}

/// The CallbackFS object: all translation units share the same instance
static CallbackFSClass &CallbackFS = CallbackFSClass::instance();

}  // namespace tinyftp
