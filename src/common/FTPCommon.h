#pragma once

#include "Arduino.h"
#include "Client.h"
#include "IPAddress.h"
#include "Stream.h"

#ifndef FTP_USE_NAMESPACE
#define FTP_USE_NAMESPACE true
#endif

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

namespace ftp_client {

/// @brief File Mode
enum FileMode { READ_MODE, WRITE_MODE, WRITE_APPEND_MODE };
enum CurrentOperation { READ_OP, WRITE_OP, LS_OP, NOP, IS_EOF };
enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
enum ObjectType { TypeFile, TypeDirectory, TypeUndefined };

/**
 * @brief CStringFunctions
 * We implemented some missing C based string functions for character arrays
 * @author Phil Schatzmann
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
    // For Windows we remove the \r at the end
    if (str[len - 1] == '\r') {
      len--; // remove \r
    }
    memset(str + len, 0, maxLen - len);
    return len;
  }
};

}

#if FTP_USE_NAMESPACE
using namespace ftp_client;
#endif
