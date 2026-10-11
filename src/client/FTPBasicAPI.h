#pragma once

#include "Arduino.h"
#include "../common/FTPCommon.h"
#include "../common/FTPLogger.h"

namespace tinyftp {

/**
 * @brief FTPBasicAPI
 * Implementation of Low Level FTP protocol. In order to simplify the logic we
 * always use Passive FTP where it is our responsibility to open the data
 * conection.
 * @author Phil Schatzmann
 */

class FTPBasicAPI {
 public:
  FTPBasicAPI() { FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI"); }

  ~FTPBasicAPI() { FTPLogger.writeLog(LOG_DEBUG, "~FTPBasicAPI"); }

  bool begin(Client *cmdPar, Client *dataPar, IPAddress &address, int port,
             const char *username, const char *password) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI ", "open");
    command_ptr = cmdPar;
    data_ptr = dataPar;
    remote_address = address;

    if (!connect(address, port, command_ptr, true)) return false;
    if (username != nullptr) {
      const char *ok_result[] = {"331", "230", nullptr};
      if (!cmd("USER", username, ok_result)) return false;
      // 331: password required; 230: already logged in
      if (strncmp(result_reply, "331", 3) == 0) {
        const char *ok_pass[] = {"230", "202", nullptr};
        if (!cmd("PASS", password == nullptr ? "" : password, ok_pass))
          return false;
      }
    }

    is_open = true;
    const char *ok_result[] = {"200", nullptr};
    cmd("OPTS", "UTF8", ok_result);

    return true;
  }

  bool quit() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "quit");
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

  operator bool() { return connected(); }

  bool passv() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "passv");
    bool ok = cmd("PASV", nullptr, "227");
    if (ok) {
      char buffer[80];
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::passv", result_reply);
      // determine data port
      int start1 = findNthInStr(result_reply, ',', 4) + 1;
      int p1 = atoi(result_reply + start1);

      snprintf(buffer, sizeof(buffer), "*** port1 -> %d ", p1);
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

      int start2 = findNthInStr(result_reply, ',', 5) + 1;
      int p2 = atoi(result_reply + start2);

      snprintf(buffer, sizeof(buffer), "*** port2 -> %d ", p2);
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

      int dataPort = (p1 * 256) + p2;
      snprintf(buffer, sizeof(buffer), "*** data port: %d", dataPort);
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::passv", buffer);

      ok = connect(remote_address, dataPort, data_ptr);
    }
    return ok;
  }

  bool del(const char *file) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "del");
    return cmd("DELE", file, "250");
  }

  bool mkdir(const char *dir) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "mkdir");
    return cmd("MKD", dir, "257");
  }

  bool rmd(const char *dir) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "rd");
    return cmd("RMD", dir, "250");
  }

  size_t size(const char *file) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "size");
    if (cmd("SIZE", file, "213")) {
      return atol(result_reply + 4);
    }
    return 0;
  }

  ObjectType objectType(const char *file) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "objectType");
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
    if (current_operation == RESERVED_OP) {
      // no transfer was started: just release the data connection
      endTransfer("abort");
    } else if (current_operation == READ_OP || current_operation == WRITE_OP ||
               current_operation == LS_OP) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "abort");
      closeData();
      setCurrentOperation(NOP);

      const char *ok[] = {"426", "226", "225", nullptr};
      rc = cmd("ABOR", nullptr, ok);
      if (rc && strncmp(result_reply, "426", 3) == 0) {
        // 426 (transfer aborted) is followed by the reply to the ABOR
        const char *ok_end[] = {"226", "225", nullptr};
        rc = checkResult(ok_end, "abort-end");
      } else {
        // the transfer might have completed before the ABOR: then the reply
        // to the ABOR is still coming
        skipReplies(FTP_ABORT_DELAY_MS);
      }
    }
    return rc;
  }

  bool binary() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "binary");
    const char *type_cmd = use_type ? "TYPE I" : "BIN";
    return cmd(type_cmd, nullptr, "200");
  }

  bool ascii() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "ascii");
    const char *type_cmd = use_type ? "TYPE A" : "ASC";
    return cmd(type_cmd, nullptr, "200");
  }

  bool type(const char *txt) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "type");
    return cmd("TYPE", txt, "200");
  }

  /// Starts the download (if necessary): returns nullptr if it failed
  Client *read(const char *file_name) {
    if (current_operation != READ_OP) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "read");
      const char *ok[] = {"150", "125", nullptr};
      if (!cmd("RETR", file_name, ok)) {
        endTransfer("read failed");
        return nullptr;
      }
      setCurrentOperation(READ_OP);
    }
    return data_ptr;
  }

  /// Starts the upload (if necessary): returns nullptr if it failed
  Client *write(const char *file_name, FileMode mode) {
    if (current_operation != WRITE_OP) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "write");
      const char *ok_write[] = {"125", "150", nullptr};
      if (!cmd(mode == WRITE_APPEND_MODE ? "APPE" : "STOR", file_name,
               ok_write)) {
        endTransfer("write failed");
        return nullptr;
      }
      setCurrentOperation(WRITE_OP);
    }
    return data_ptr;
  }

  /// Starts the directory listing: returns nullptr if it failed
  Client *ls(const char *file_name) {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "ls");
    const char *ok[] = {"125", "150", nullptr};
    if (!cmd("NLST", file_name, ok)) {
      endTransfer("ls failed");
      return nullptr;
    }
    setCurrentOperation(LS_OP);
    return data_ptr;
  }

  /// Returns true while the server keeps the data connection open (or there
  /// is still unread data)
  bool dataConnected() { return data_ptr != nullptr && data_ptr->connected(); }

  void closeData() {
    FTPLogger.writeLog(LOG_INFO, "FTPBasicAPI", "closeData");
    data_ptr->stop();
  }

  /// Ends the current transfer: closes the data connection, consumes the
  /// final status of the transfer (if one was started) and releases the
  /// session
  bool endTransfer(const char *tag) {
    bool ok = true;
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::endTransfer", tag);
    if (current_operation == READ_OP || current_operation == LS_OP) {
      closeData();
      // 426: we closed the data connection before all data was received
      const char *ok_result[] = {"226", "250", "426", nullptr};
      ok = checkResult(ok_result, tag, true);
    } else if (current_operation == WRITE_OP) {
      closeData();
      const char *ok_result[] = {"226", "250", nullptr};
      ok = checkResult(ok_result, tag, true);
    } else if (current_operation == RESERVED_OP) {
      closeData();
    }
    setCurrentOperation(NOP);
    return ok;
  }

  /// Reserves the session for a new transfer: returns its id
  uint32_t reserve() {
    if (++transfer_id == 0) transfer_id = 1;
    handles = 0;
    setCurrentOperation(RESERVED_OP);
    return transfer_id;
  }

  /// Id of the current (or last) transfer
  uint32_t transferId() { return transfer_id; }

  /// Returns true if the transfer with the indicated id is still active
  bool isTransfer(uint32_t id) {
    return id != 0 && id == transfer_id && current_operation != NOP;
  }

  /// Registers an additional handle (FTPFile or FTPFileIterator copy) of the
  /// transfer
  void addHandle(uint32_t id) {
    if (isTransfer(id)) handles++;
  }

  /// Unregisters a handle: the transfer ends when the last handle is gone
  void releaseHandle(uint32_t id) {
    if (isTransfer(id) && --handles <= 0) endTransfer("release");
  }

  void setCurrentOperation(CurrentOperation op) {
    char msg[80];
    snprintf(msg, sizeof(msg), "setCurrentOperation: %d", (int)op);
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", msg);
    current_operation = op;
  }

  CurrentOperation currentOperation() { return current_operation; }

  void flush() {
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI", "flush");
    data_ptr->flush();
  }

  bool checkResult(const char *expected[], const char *command,
                   bool wait_for_data = true) {
    result_reply[0] = '\0';
    // nothing to consume and we do not need to wait
    if (!wait_for_data && command_ptr->available() == 0) return true;

    char result_str[FTP_RESULT_BUFFER_SIZE];
    bool ok = readReply(result_str, sizeof(result_str));
    if (!ok) {
      // if we got no reply and we dont need to wait we are still ok
      ok = !wait_for_data;
    } else {
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult", result_str);
      strncpy(result_reply, result_str, sizeof(result_reply) - 1);
      result_reply[sizeof(result_reply) - 1] = '\0';
      ok = isExpected(result_str, expected);
    }

    // log error
    if (!ok) {
      FTPLogger.writeLog(LOG_ERROR, "FTPBasicAPI::checkResult", command);
      FTPLogger.writeLog(LOG_ERROR, "FTPBasicAPI::checkResult", result_reply);
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
    if (par == nullptr) {
      snprintf(command_buffer, FTP_COMMAND_BUFFER_SIZE, "%s", command_str);
    } else {
      snprintf(command_buffer, FTP_COMMAND_BUFFER_SIZE, "%s %s", command_str,
               par);
    }
    command_ptr->println(command_buffer);
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::cmd", command_buffer);

    return checkResult(expected, command_buffer, wait_for_data);
  }

  void setUseTypeCommand(bool useType) { use_type = useType; }

 protected:
  // currently running op -> do we need to cancel ?
  CurrentOperation current_operation = NOP;
  Client *command_ptr = nullptr;  // Client for commands
  Client *data_ptr = nullptr;     // Client for upload and download of files
  IPAddress remote_address;
  bool is_open = false;
  bool use_type = false;
  char result_reply[100] = {0};
  uint32_t transfer_id = 0;  // id of the current (or last) transfer
  int handles = 0;           // number of handles of the current transfer

  /// Forgets the current transfer: e.g. when the connection was closed
  void resetTransfer() {
    current_operation = NOP;
    handles = 0;
    if (++transfer_id == 0) transfer_id = 1;
  }

  /// Consumes all replies which arrive within the indicated time
  void skipReplies(unsigned long ms) {
    char reply[FTP_RESULT_BUFFER_SIZE];
    unsigned long start = millis();
    while (millis() - start < ms) {
      if (command_ptr->available() > 0) {
        if (readReply(reply, sizeof(reply))) {
          FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::skipReplies", reply);
        }
      } else {
        delay(1);
      }
    }
  }

  bool connect(IPAddress adr, int port, Client *client_ptr,
               bool doCheckResult = false) {
    char buffer[80];
    snprintf(buffer, sizeof(buffer), "connect %d.%d.%d.%d:%d", adr[0], adr[1],
             adr[2], adr[3], port);
    FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::connect", buffer);
    // make sure we start with a clean state
    if (client_ptr->connected()) client_ptr->stop();
    for (int j = 0; j < FTP_CONNECT_RETRIES; j++) {
      if (j > 0) delay(FTP_CONNECT_RETRY_DELAY_MS);
      if (client_ptr->connect(adr, port)) break;
    }
    bool ok = client_ptr->connected();
    if (ok && doCheckResult) {
      const char *ok_result[] = {"220", "200", nullptr};
      ok = checkResult(ok_result, "connect");
    }
    // log result
    if (ok) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::connected", buffer);
    } else {
      FTPLogger.writeLog(LOG_ERROR, "FTPBasicAPI::connected", buffer);
    }
    return ok;
  }

  /// Returns true if the reply starts with one of the expected codes (or if
  /// no codes are expected)
  bool isExpected(const char *reply, const char *expected[]) {
    if (expected[0] == nullptr) {
      FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult",
                          "success because of not expected result codes");
      return true;
    }
    for (int j = 0; expected[j] != nullptr; j++) {
      if (strncmp(reply, expected[j], 3) == 0) {
        char msg[80];
        snprintf(msg, sizeof(msg), " -> success with %s", expected[j]);
        FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::checkResult", msg);
        return true;
      }
    }
    return false;
  }

  /// Reads a complete (potentially multi-line) reply from the command
  /// connection. The first line (which contains the reply code) is returned.
  bool readReply(char *reply, int maxLen) {
    // skip empty lines
    do {
      if (!readReplyLine(reply, maxLen)) return false;
    } while (strlen(reply) < 3);

    // multi-line reply "123-..." ends with a line "123 ..."
    if (reply[3] == '-') {
      char line[FTP_RESULT_BUFFER_SIZE];
      do {
        if (!readReplyLine(line, sizeof(line))) return false;
        FTPLogger.writeLog(LOG_DEBUG, "FTPBasicAPI::readReply", line);
      } while (!(strncmp(line, reply, 3) == 0 && line[3] == ' '));
    }
    return true;
  }

  /// Reads a single line (w/o trailing \r\n) from the command connection
  /// with timeout. Lines longer then the buffer are truncated.
  bool readReplyLine(char *str, int maxLen) {
    int len = 0;
    unsigned long start = millis();
    while (true) {
      if (command_ptr->available() > 0) {
        int c = command_ptr->read();
        if (c < 0) continue;
        if (c == '\n') break;
        if (len < maxLen - 1) str[len++] = (char)c;
      } else if (!command_ptr->connected() ||
                 millis() - start >= FTP_REPLY_TIMEOUT_MS) {
        str[len] = '\0';
        FTPLogger.writeLog(LOG_ERROR, "FTPBasicAPI::readReplyLine",
                            "timeout or connection closed");
        return false;
      } else {
        delay(1);
      }
    }
    // For Windows we remove the \r at the end
    if (len > 0 && str[len - 1] == '\r') len--;
    str[len] = '\0';
    return true;
  }
};

/**
 * @brief FTPSessionProvider
 * Provides sessions on demand (implemented by the FTPClient)
 */
class FTPSessionProvider {
 public:
  virtual ~FTPSessionProvider() = default;
  /// Provides an idle, connected session (or nullptr)
  virtual FTPBasicAPI *idleSession() = 0;
  /// Provides a session with an open data connection which is reserved for a
  /// transfer (or nullptr)
  virtual FTPBasicAPI *openDataSession() = 0;
};

/**
 * @brief FTPTransferHandle
 * Counted reference to the transfer of a session: the transfer is ended and
 * the session is released when the last copy of the handle is destroyed.
 * Handles of an ended transfer are inactive, so they can not affect a later
 * transfer of the same session.
 */
class FTPTransferHandle {
 public:
  FTPTransferHandle() = default;

  FTPTransferHandle(const FTPTransferHandle &other)
      : api_ptr(other.api_ptr), id(other.id) {
    if (api_ptr != nullptr) api_ptr->addHandle(id);
  }

  FTPTransferHandle &operator=(const FTPTransferHandle &other) {
    if (this != &other) {
      if (other.api_ptr != nullptr) other.api_ptr->addHandle(other.id);
      release();
      api_ptr = other.api_ptr;
      id = other.id;
    }
    return *this;
  }

  ~FTPTransferHandle() { release(); }

  /// Takes part in the current transfer of the session
  void attach(FTPBasicAPI *api) {
    release();
    api_ptr = api;
    id = api != nullptr ? api->transferId() : 0;
    if (api_ptr != nullptr) api_ptr->addHandle(id);
  }

  /// Gives up this reference
  void release() {
    if (api_ptr != nullptr) api_ptr->releaseHandle(id);
    api_ptr = nullptr;
    id = 0;
  }

  /// Provides the session while the transfer is active (or nullptr)
  FTPBasicAPI *api() const {
    return api_ptr != nullptr && api_ptr->isTransfer(id) ? api_ptr : nullptr;
  }

 protected:
  FTPBasicAPI *api_ptr = nullptr;
  uint32_t id = 0;
};

}  // namespace tinyftp
