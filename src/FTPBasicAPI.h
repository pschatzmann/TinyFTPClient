#pragma once

#include "Arduino.h"
#include "FTPCommon.h"
#include "FTPLogger.h"

namespace ftp_client {

/**
 * @brief FTPBasicAPI
 * Implementation of Low Level FTP protocol. In order to simplify the logic we
 * always use Passive FTP where it is our responsibility to open the data
 * conection.
 * @author Phil Schatzmann
 */

class FTPBasicAPI {
  friend class FTPFile;

 public:
  FTPBasicAPI() { FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI"); }

  ~FTPBasicAPI() { FTPLogger::writeLog(LOG_DEBUG, "~FTPBasicAPI"); }

  bool begin(Client *cmdPar, Client *dataPar, IPAddress &address, int port,
              const char *username, const char *password) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI ", "open");
    command_ptr = cmdPar;
    data_ptr = dataPar;
    remote_address = address;

    if (!connect(address, port, command_ptr, true)) return false;
    if (username != nullptr) {
      const char *ok_result[] = {"331", "230", "530", nullptr};
      if (!cmd("USER", username, ok_result)) return false;
    }
    if (password != nullptr) {
      const char *ok_result[] = {"230", "202", nullptr};
      if (!cmd("PASS", password, ok_result)) return false;
    }

    is_open = true;
    ;
    return true;
  }

  bool quit() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "quit");
    const char *ok_result[] = {"221", "226", nullptr};
    bool result = cmd("QUIT", nullptr, ok_result, false);
    if (!result) {
      result = cmd("BYE", nullptr, ok_result, false);
    }
    if (!result) {
      result = cmd("DISCONNECT", nullptr, ok_result, false);
    }
    return result;
  }

  bool connected() { return is_open; }

  operator bool() { return is_open; }

  bool passv() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "passv");
    bool ok = cmd("PASV", nullptr, "227");
    if (ok) {
      char buffer[80];
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::passv", result_reply);
      // determine data port
      int start1 = CStringFunctions::findNthInStr(result_reply, ',', 4) + 1;
      int p1 = atoi(result_reply + start1);

      sprintf(buffer, "*** port1 -> %d ", p1);
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

      int start2 = CStringFunctions::findNthInStr(result_reply, ',', 5) + 1;
      int p2 = atoi(result_reply + start2);

      sprintf(buffer, "*** port2 -> %d ", p2);
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

      int dataPort = (p1 * 256) + p2;
      sprintf(buffer, "*** data port: %d", dataPort);
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

      ok = connect(remote_address, dataPort, data_ptr) == 1;
    }
    return ok;
  }

  bool del(const char *file) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "del");
    return cmd("DELE", file, "250");
  }

  bool mkdir(const char *dir) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "mkdir");
    return cmd("MKD", dir, "257");
  }

  bool rmd(const char *dir) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "rd");
    return cmd("RMD", dir, "250");
  }

  size_t size(const char *file) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "size");
    if (cmd("SIZE", file, "213")) {
      return atol(result_reply + 4);
    }
    return 0;
  }

  ObjectType objectType(const char *file) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "objectType");
    const char *ok_result[] = {"213", "550", nullptr};
    ObjectType result = TypeDirectory;
    if (cmd("SIZE", file, ok_result)) {
      if (strncmp(result_reply, "213", 3) == 0) {
        result = TypeFile;
      }
    }
    return result;
  }

  bool abort() {
    bool rc = true;
    if (current_operation == READ_OP || current_operation == WRITE_OP ||
        current_operation == LS_OP) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "abort");
      data_ptr->stop();

      const char *ok[] = {"426", "226", "225", nullptr};
      setCurrentOperation(NOP);
      rc = cmd("ABOR", nullptr, ok);
      delay(FTP_ABORT_DELAY_MS);
      while (command_ptr->available() > 0) {
        command_ptr->read();
      }
    }
    return rc;
  }

  bool binary() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "binary");
    return cmd("BIN", nullptr, "200");
  }

  bool ascii() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "ascii");
    return cmd("ASC", nullptr, "200");
  }

  bool type(const char *txt) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "type");
    return cmd("TYPE", txt, "200");
  }

  Stream *read(const char *file_name) {
    if (current_operation != READ_OP) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "read");
      const char *ok[] = {"150", "125", nullptr};
      cmd("RETR", file_name, ok);
      setCurrentOperation(READ_OP);
    }
    checkClosed(data_ptr);
    return data_ptr;
  }

  Stream *write(const char *file_name, FileMode mode) {
    if (current_operation != WRITE_OP) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "write");
      const char *ok_write[] = {"125", "150", nullptr};
      cmd(mode == WRITE_APPEND_MODE ? "APPE" : "STOR", file_name, ok_write);
      setCurrentOperation(WRITE_OP);
    }
    checkClosed(data_ptr);
    return data_ptr;
  }

  Stream *ls(const char *file_name) {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "ls");
    const char *ok[] = {"125", "150", nullptr};
    cmd("NLST", file_name, ok);
    setCurrentOperation(LS_OP);
    return data_ptr;
  }

  void closeData() {
    FTPLogger::writeLog(LOG_INFO, "FTPBasicAPI", "closeData");
    data_ptr->stop();

    // Transfer Completed
    const char *ok[] = {"226", nullptr};
    if (currentOperation() == IS_EOF) {
      checkResult(ok, "closeData", false);
    } else {
     // abort();
    }

  }

  void setCurrentOperation(CurrentOperation op) {
    char msg[80];
    sprintf(msg, "setCurrentOperation: %d", (int)op);
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", msg);
    current_operation = op;
  }

  CurrentOperation currentOperation() { return current_operation; }

  void flush() {
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI", "flush");
    data_ptr->flush();
  }

  bool checkResult(const char *expected[], const char *command,
                   bool wait_for_data = true) {
    // consume all result lines
    bool ok = false;
    result_reply[0] = '\0';
    Stream *stream_ptr = command_ptr;

    char result_str[FTP_RESULT_BUFFER_SIZE];
    if (wait_for_data || stream_ptr->available()) {
      // wait for reply
      while (stream_ptr->available() == 0) {
        delay(100);
      }
      // read reply
      // result_str = stream_ptr->readStringUntil('\n').c_str();
      CStringFunctions::readln(*stream_ptr, result_str, FTP_RESULT_BUFFER_SIZE);

      if (strlen(result_str) > 3) {
        FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult", result_str);
        strncpy(result_reply, result_str, sizeof(result_reply) - 1);
        // if we did not expect anything
        if (expected[0] == nullptr) {
          ok = true;
          FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult",
                              "success because of not expected result codes");
        } else {
          // check for valid codes
          char msg[80];
          for (int j = 0; expected[j] != nullptr; j++) {
            sprintf(msg, "- checking with %s", expected[j]);
            FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult", msg);
            if (strncmp(result_str, expected[j], 3) == 0) {
              sprintf(msg, " -> success with %s", expected[j]);
              FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult", msg);
              ok = true;
              break;
            }
          }
        }
      } else {
        // if we got am empty line and we dont need to wait we are still ok
        if (!wait_for_data) ok = true;
      }
    } else {
      ok = true;
    }

    // log error
    if (!ok) {
      FTPLogger::writeLog(LOG_ERROR, "FTPBasicAPI::checkResult", command);
      FTPLogger::writeLog(LOG_ERROR, "FTPBasicAPI::checkResult", result_reply);
    }
    return ok;
  }

  bool cmd(const char *command, const char *par, const char *expected,
           bool wait_for_data = true) {
    const char *expected_array[] = {expected, nullptr};
    return cmd(command, par, expected_array, wait_for_data);
  }

  bool cmd(const char *command_str, const char *par, const char *expected[],
           bool wait_for_data = true) {
    char command_buffer[FTP_COMMAND_BUFFER_SIZE];
    Stream *stream_ptr = command_ptr;
    if (par == nullptr) {
      strncpy(command_buffer, command_str, FTP_COMMAND_BUFFER_SIZE);
      stream_ptr->println(command_buffer);
    } else {
      snprintf(command_buffer, FTP_COMMAND_BUFFER_SIZE, "%s %s", command_str,
               par);
      stream_ptr->println(command_buffer);
    }
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::cmd", command_buffer);

    return checkResult(expected, command_buffer, wait_for_data);
  }

  bool checkClosed(Client *client) {
    if (!client->connected()) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI",
                          "checkClosed -> client is closed");
      // mark the actual command as completed
      setCurrentOperation(IS_EOF);
      return true;
    }
    return false;
  }

 protected:
  // currently running op -> do we need to cancel ?
  CurrentOperation current_operation = NOP;
  Client *command_ptr = nullptr;  // Client for commands
  Client *data_ptr = nullptr;     // Client for upload and download of files
  IPAddress remote_address;
  bool is_open;
  char result_reply[100];

  bool connect(IPAddress adr, int port, Client *client_ptr,
               bool doCheckResult = false) {
    char buffer[80];
    bool ok = true;
#if defined(ESP32) || defined(ESP8266)
    sprintf(buffer, "connect %s:%d", adr.toString().c_str(), port);
    FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::connect", buffer);
#endif
    // try to connect 10 times
    if (client_ptr->connected()) client_ptr->stop();  // make sure we start with a clean state
    for (int j = 0; j < 10; j++) {
      ok = client_ptr->connect(adr, port);
      if (ok) break;
      delay(500);
    }
    // ok = client_ptr->connect(adr, port);
    ok = client_ptr->connected();
    if (ok && doCheckResult) {
      const char *ok_result[] = {"220", "200", nullptr};
      ok = checkResult(ok_result, "connect");

      // there might be some more messages: e.g. in FileZilla: we just ignore
      // them
      while (command_ptr->available() > 0) {
        command_ptr->read();
      }
    }
    // log result
    if (ok) {
      FTPLogger::writeLog(LOG_DEBUG, "FTPBasicAPI::connected", buffer);
    } else {
      FTPLogger::writeLog(LOG_ERROR, "FTPBasicAPI::connected", buffer);
    }
    return ok;
  }
};

}  // namespace ftp_client