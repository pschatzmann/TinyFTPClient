#pragma once

#include "FTPCommon.h"

namespace tinyftp {

/**
 * @brief FTPLoggerClass
 * To activate logging start the FTPLogger object with the output stream and
 * (optionally) the log level e.g. with FTPLogger.begin(Serial, LOG_INFO);
 * @author Phil Schatzmann
 */
class FTPLoggerClass {
 public:
  /// Provides the single instance which is available as FTPLogger
  static FTPLoggerClass &instance() {
    static FTPLoggerClass logger;
    return logger;
  }

  FTPLoggerClass(const FTPLoggerClass &) = delete;
  FTPLoggerClass &operator=(const FTPLoggerClass &) = delete;

  /// Starts the logging to the indicated output
  void begin(Stream &out, FTPLogLevel level = LOG_ERROR) {
    setOutput(out);
    setLogLevel(level);
  }

  void setLogLevel(FTPLogLevel level) { min_log_level = level; }

  FTPLogLevel getLogLevel() { return min_log_level; }

  void setOutput(Stream &out) { out_ptr = &out; }

  void writeLog(FTPLogLevel level, const char *module,
                const char *msg = nullptr) {
    if (out_ptr != nullptr && level >= min_log_level) {
      out_ptr->print("FTP ");
      switch (level) {
        case LOG_DEBUG:
          out_ptr->print("DEBUG - ");
          break;
        case LOG_INFO:
          out_ptr->print("INFO - ");
          break;
        case LOG_WARN:
          out_ptr->print("WARN - ");
          break;
        case LOG_ERROR:
          out_ptr->print("ERROR - ");
          break;
      }
      out_ptr->print(module);
      if (msg != nullptr) {
        out_ptr->print(": ");
        out_ptr->print(msg);
      }
      out_ptr->println();
    }
  }

 protected:
  FTPLogLevel min_log_level = LOG_ERROR;
  Stream *out_ptr = nullptr;

  FTPLoggerClass() = default;
};

/// The FTPLogger object: all translation units share the same instance
static FTPLoggerClass &FTPLogger = FTPLoggerClass::instance();

}  // namespace tinyftp
