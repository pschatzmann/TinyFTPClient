#pragma once

#include "FTPCommon.h"

namespace ftp_client {

// initialize static variables
static LogLevel ftp_min_log_level = LOG_ERROR;
static Stream *ftp_logger_out_ptr = nullptr; 

/**
 * @brief FTPLogger
 * To activate logging define the output stream e.g. with
 * FTPLogger.setOutput(Serial); and (optionally) set the log level
 * @author Phil Schatzmann
 */
class FTPLogger {
 public:
  static void setLogLevel(LogLevel level) {
    ftp_min_log_level = level;
  }
  
  static LogLevel getLogLevel() {
    return ftp_min_log_level;
  }
  
  static void setOutput(Stream &out) {
    ftp_logger_out_ptr = &out;
  }
  
  static void writeLog(LogLevel level, const char *module, const char *msg = nullptr) {
    if (ftp_logger_out_ptr != nullptr && level >= ftp_min_log_level) {
      ftp_logger_out_ptr->print("FTP ");
      switch (level) {
        case LOG_DEBUG:
          ftp_logger_out_ptr->print("DEBUG - ");
          break;
        case LOG_INFO:
          ftp_logger_out_ptr->print("INFO - ");
          break;
        case LOG_WARN:
          ftp_logger_out_ptr->print("WARN - ");
          break;
        case LOG_ERROR:
          ftp_logger_out_ptr->print("ERROR - ");
          break;
      }
      ftp_logger_out_ptr->print(module);
      if (msg != nullptr) {
        ftp_logger_out_ptr->print(": ");
        ftp_logger_out_ptr->print(msg);
      }
      ftp_logger_out_ptr->println();
    }
  }
};

}