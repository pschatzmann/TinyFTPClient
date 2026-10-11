#pragma once

#include "Arduino.h"
#include "Client.h"
#include "IPAddress.h"
#include "Stream.h"

#ifndef FTP_ABORT_DELAY_MS 
#define FTP_ABORT_DELAY_MS 300
#endif

#ifndef FTP_COMMAND_BUFFER_SIZE 
#define FTP_COMMAND_BUFFER_SIZE 300
#endif

#ifndef FTP_RESULT_BUFFER_SIZE 
#define FTP_RESULT_BUFFER_SIZE 300
#endif

#ifndef FTP_COMMAND_PORT 
#define FTP_COMMAND_PORT 21
#endif

#ifndef FTP_MAX_SESSIONS
#define FTP_MAX_SESSIONS 10
#endif

#ifndef FTP_REPLY_TIMEOUT_MS
#define FTP_REPLY_TIMEOUT_MS 10000
#endif

#ifndef FTP_CONNECT_RETRIES
#define FTP_CONNECT_RETRIES 10
#endif

#ifndef FTP_CONNECT_RETRY_DELAY_MS
#define FTP_CONNECT_RETRY_DELAY_MS 500
#endif

/// FTP client and server functionality
namespace tinyftp {

/// @brief File Mode
enum FileMode { READ_MODE, WRITE_MODE, WRITE_APPEND_MODE };
/// @brief State of a session: RESERVED_OP means a data connection was opened
/// (PASV) but no transfer command was sent yet
enum CurrentOperation { READ_OP, WRITE_OP, LS_OP, NOP, RESERVED_OP };
enum FTPLogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
enum ObjectType { TypeFile, TypeDirectory, TypeUndefined };

/// Provides the position of the n-th occurrence of ch in str (or -1)
inline int findNthInStr(const char *str, char ch, int n) {
  int occur = 0;
  for (unsigned int i = 0; i < strlen(str); i++) {
    if (str[i] == ch) {
      occur += 1;
    }
    if (occur == n) return i;
  }
  return -1;
}

}  // namespace tinyftp
