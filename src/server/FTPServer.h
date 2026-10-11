#pragma once
#include <Arduino.h>
#include <Client.h>
#include <IPAddress.h>

#include <functional>
#include <utility>
#include <vector>

#if defined(ESP32) && defined(FS_H)
// on the ESP32 all file systems (SD, LittleFS, SPIFFS...) are a fs::FS: we do
// not include FS.h ourself, so that its FILE_READ/FILE_WRITE do not conflict
// with other file systems (e.g. TinyFATFS)
using SDClass = fs::FS;
#endif

// Default port for FTP
#ifndef FTP_PORT
#define FTP_PORT 21
#endif
// Default port used for passive data connections
#ifndef FTP_DATA_PORT
#define FTP_DATA_PORT 50009
#endif
// Default timeout in ms for establishing and idling on data connections
#ifndef FTP_DATA_TIMEOUT
#define FTP_DATA_TIMEOUT 5000
#endif
// Max number of FTP clients which can be connected at the same time
#ifndef FTP_SERVER_MAX_SESSIONS
#define FTP_SERVER_MAX_SESSIONS 4
#endif
// Size of the transfer buffer of each connection
#ifndef FTP_SERVER_BUFFER_SIZE
#define FTP_SERVER_BUFFER_SIZE 512
#endif
// Max length of a command line
#ifndef FTP_SERVER_MAX_LINE
#define FTP_SERVER_MAX_LINE 512
#endif

// File modes of storages with the SD API (e.g. SD.h, TinyFATFS): their header
// must be included before FTPServer.h.
//
// FTP_FILE_APPEND is only defined if FILE_READ and FILE_WRITE are available.
// This is not always the case: e.g. a sketch which only uses CallbackFS or
// LittleFS (with mode strings) does not include any header which defines
// them. We can't provide default values, because the modes are platform
// specific (e.g. uint8_t flags for SD and TinyFATFS, "r"/"w" strings for the
// ESP32 FS.h). FTP_FILE_APPEND is a private name, so that it does not conflict
// with a FILE_APPEND which is defined by a header that is included later.
#if defined(FILE_READ) && defined(FILE_WRITE)
#ifdef FILE_APPEND
#define FTP_FILE_APPEND FILE_APPEND
#else
#define FTP_FILE_APPEND FILE_WRITE  // FILE_WRITE appends with the SD library
#endif
#endif

namespace tinyftp {

/**
 * @brief Simple, extensible FTP server for Arduino using a templated server and
 * storage backend.
 *
 * @tparam TServer Server type (e.g., WiFiServer)
 * @tparam TStorage Storage type (e.g., SDClass, fs::FS, CallbackFSClass). The
 * header of the storage (e.g. SD.h) must be included before FTPServer.h
 *
 * This class implements a basic FTP server supporting commands like USER, PASS,
 * LIST, PWD, CWD, RETR, STOR, etc. File data is transferred over a separate
 * data connection which is opened with PASV, EPSV (passive) or PORT (active).
 * The server uses a pluggable storage backend for file and directory
 * operations.
 *
 * Up to FTP_SERVER_MAX_SESSIONS clients can be connected at the same time:
 * doLoop() does not block, it processes one step (a command or a part of a
 * transfer) of each connection. Each connection uses its own passive data
 * port: dataPort + connection index.
 */

template <typename TServer, typename TStorage>
class FTPServer {
 public:
  /// Client type returned by the server
  using ClientType = decltype(std::declval<TServer&>().accept());

  /// Constructor: initializes server and storage backend
  FTPServer(uint16_t port = FTP_PORT, TStorage* storage = nullptr,
            uint16_t dataPort = FTP_DATA_PORT)
      : server(port), dataPort(dataPort), storage(storage) {
    commands = {{"USER", &FTPServer::onUSER, false},
                {"PASS", &FTPServer::onPASS, false},
                {"LIST", &FTPServer::onLIST},
                {"NLST", &FTPServer::onNLST},
                {"PWD", &FTPServer::onPWD},
                {"CWD", &FTPServer::onCWD},
                {"CD", &FTPServer::onCWD},
                {"CDUP", &FTPServer::onCDUP},
                {"SYST", &FTPServer::onSYST},
                {"TYPE", &FTPServer::onTYPE},
                {"BIN", &FTPServer::onBIN},
                {"ASC", &FTPServer::onASC},
                {"NOOP", &FTPServer::onNOOP},
                {"PASV", &FTPServer::onPASV},
                {"EPSV", &FTPServer::onEPSV},
                {"PORT", &FTPServer::onPORT},
                {"HELP", &FTPServer::onHELP, false},
                {"RETR", &FTPServer::onRETR},
                {"STOR", &FTPServer::onSTOR},
                {"APPE", &FTPServer::onAPPE},
                {"ABOR", &FTPServer::onABOR},
                {"DELE", &FTPServer::onDELE},
                {"MKD", &FTPServer::onMKD},
                {"RMD", &FTPServer::onRMD},
                {"RNFR", &FTPServer::onRNFR},
                {"RNTO", &FTPServer::onRNTO},
                {"SIZE", &FTPServer::onSIZE},
                {"FEAT", &FTPServer::onFEAT, false},
                {"OPTS", &FTPServer::onOPTS, false},
                {"QUIT", &FTPServer::onQUIT, false}};
    for (int j = 0; j < FTP_SERVER_MAX_SESSIONS; j++) sessions[j].index = j;
  }

  ~FTPServer() {
    for (auto& s : sessions) {
      if (s.active) closeSession(s);
      delete s.dataServer;
      s.dataServer = nullptr;
    }
  }

  FTPServer(const FTPServer&) = delete;
  FTPServer& operator=(const FTPServer&) = delete;

  /// Adds a user which can log in (or updates the password of an existing
  /// user). Without any users the server is open to everybody.
  void addUser(const char* user, const char* password) {
    for (auto& entry : users) {
      if (entry.name == user) {
        entry.password = password;
        return;
      }
    }
    users.push_back({user, password});
  }

  /// Defines the IP address which is reported by PASV. If not set, the local
  /// IP of the control connection is used (when the client type supports it)
  void setPassiveIP(IPAddress ip) {
    passiveIP = ip;
    hasPassiveIP = true;
  }

  /// Defines the timeout in ms for opening and idling on data connections
  void setDataTimeout(uint32_t timeoutMs) { dataTimeout = timeoutMs; }

  /// Starts the FTP server
  void begin() {
    server.begin();
    if (!hasUsers()) {
      Serial.println("Warning: No FTP users defined. Server is open to all.");
    }
  }

  /// Main loop: accepts new FTP client connections and processes one step of
  /// each connection. Call it repeatedly from your loop(): it does not block.
  void doLoop() {
    acceptClient();
    for (auto& s : sessions) {
      if (s.active) step(s);
    }
  }

  /// Number of connected FTP clients
  int connectionCount() {
    int result = 0;
    for (auto& s : sessions) {
      if (s.active) result++;
    }
    return result;
  }

  /// Adds a custom command handler or replaces the handler of an existing
  /// command (in which case the login requirement is kept)
  void addCommand(const char* name,
                  std::function<bool(Client&, const String&)> handler,
                  bool needsAuth = true) {
    for (auto& entry : commands) {
      if (strcasecmp(entry.name, name) == 0) {
        entry.handler = handler;
        entry.builtin = nullptr;
        return;
      }
    }
    CommandEntry entry{name, nullptr, needsAuth};
    entry.handler = handler;
    commands.push_back(entry);
  }

 protected:
  enum class DataMode { None, Passive, Active };
  enum class Transfer { None, Retr, Stor, List };
  enum class OpenMode { Read, Write, Append };

  // The storage is opened with one of the following openIn() overloads, which
  // are selected with SFINAE: the 0 argument prefers the int overload (mode
  // strings) if the storage supports open(path, "r"); otherwise the long
  // overload (SD API modes) is used. If neither matches, the compiler reports
  // that there is no matching openIn(): then include the header of your
  // storage (e.g. SD.h) before FTPServer.h.

  /// Opens a file with a storage which supports mode strings (e.g. fs::FS)
  template <typename S>
  static auto openIn(S* fs, const char* path, OpenMode mode, int)
      -> decltype(fs->open(path, "r")) {
    return fs->open(path, mode == OpenMode::Read    ? "r"
                          : mode == OpenMode::Write ? "w"
                                                    : "a");
  }
  // Only available with the SD API modes: we can't drop the #ifdef, because
  // FILE_READ does not depend on the template parameter, so the compiler
  // checks it when it reads the template, even if this overload is never
  // used. Without the SD API modes the sketch would not compile.
#ifdef FTP_FILE_APPEND
  /// Opens a file with the SD API modes (FILE_READ, FILE_WRITE...)
  template <typename S>
  static auto openIn(S* fs, const char* path, OpenMode mode, long)
      -> decltype(fs->open(path, FILE_READ)) {
    switch (mode) {
      case OpenMode::Read:
        return fs->open(path, FILE_READ);
      case OpenMode::Append:
        return fs->open(path, FTP_FILE_APPEND);
      default:
        // FILE_WRITE might append: make sure that we replace the file
        if (fs->exists(path)) fs->remove(path);
        return fs->open(path, FILE_WRITE);
    }
  }
#endif

  /// File type of the storage
  using FileType =
      decltype(openIn(std::declval<TStorage*>(), "", OpenMode::Read, 0));

  /// State of a client connection
  struct Session {
    int index = 0;
    bool active = false;
    ClientType control;
    String line;  // command line which is being received
    bool lineReady = false;
    bool isLoggedIn = false;
    String userName;  // name provided by USER
    String currentDir = "/";
    String renameFrom;
    DataMode dataMode = DataMode::None;
    IPAddress activeIP;
    uint16_t activePort = 0;
    TServer* dataServer = nullptr;  // passive data port of this connection
    bool dataServerStarted = false;
    // running transfer
    Transfer transfer = Transfer::None;
    bool waitingForData = false;  // data connection not open yet
    DataMode transferMode = DataMode::None;
    const char* transferMsg = "";  // 150 reply when the data connection opens
    unsigned long lastActivity = 0;
    ClientType data;
    FileType file;
    bool listDetails = false;
    bool listSingle = false;  // list a single file instead of a directory
    uint8_t buffer[FTP_SERVER_BUFFER_SIZE];
    size_t bufferLen = 0;
    size_t bufferPos = 0;
  };

  using Handler = bool (FTPServer::*)(Session&, const String&);

  struct CommandEntry {
    CommandEntry(const char* name, Handler builtin, bool needsAuth = true)
        : name(name), builtin(builtin), needsAuth(needsAuth) {}
    const char* name;
    Handler builtin;  // or custom handler
    bool needsAuth;
    std::function<bool(Client&, const String&)> handler;
  };

  struct User {
    String name;
    String password;
  };

  std::vector<CommandEntry> commands;
  TServer server;
  uint16_t dataPort;
  TStorage* storage;
  Session sessions[FTP_SERVER_MAX_SESSIONS];
  std::vector<User> users;
  IPAddress passiveIP;
  bool hasPassiveIP = false;
  uint32_t dataTimeout = FTP_DATA_TIMEOUT;

  bool hasUsers() { return !users.empty(); }

  /// Sends a reply on the control connection (via Client: some client types
  /// hide the String overloads)
  void reply(Session& s, const String& msg) {
    static_cast<Client&>(s.control).println(msg);
  }

  bool isValidLogin(const String& name, const String& pwd) {
    for (auto& entry : users) {
      if (entry.name == name && entry.password == pwd) return true;
    }
    return false;
  }

  /// Provides the passive data server of the connection
  TServer& dataServer(int index) {
    Session& s = sessions[index];
    if (s.dataServer == nullptr) s.dataServer = new TServer(dataPort + index);
    return *s.dataServer;
  }

  /// Accepts a new client connection
  void acceptClient() {
    auto client = server.accept();
    if (!client) return;
    for (auto& s : sessions) {
      if (!s.active) {
        startSession(s, client);
        reply(s, "220 Welcome to TinyFTP");
        return;
      }
    }
    static_cast<Client&>(client).println(
        "421 Too many connections, try again later");
    client.stop();
  }

  /// Resets the per connection state
  void startSession(Session& s, ClientType& client) {
    s.active = true;
    s.control = client;
    s.line = "";
    s.lineReady = false;
    s.isLoggedIn = !hasUsers();
    s.userName = "";
    s.currentDir = "/";
    s.renameFrom = "";
    s.dataMode = DataMode::None;
    s.transfer = Transfer::None;
  }

  void closeSession(Session& s) {
    if (s.transfer != Transfer::None) closeTransfer(s);
    s.control.stop();
    s.active = false;
    s.isLoggedIn = false;
    s.userName = "";
    s.dataMode = DataMode::None;
  }

  /// Processes one step of a connection
  void step(Session& s) {
    if (s.transfer != Transfer::None) stepTransfer(s);
    if (!s.lineReady) readLine(s);
    // during a transfer we only process ABOR: the other commands wait
    if (s.lineReady && (s.transfer == Transfer::None || isAbort(s.line))) {
      String cmd = s.line;
      s.line = "";
      s.lineReady = false;
      cmd.trim();
      if (cmd.length() > 0 && !processCommand(s, cmd)) {
        closeSession(s);
        return;
      }
    }
    if (s.transfer == Transfer::None && !s.lineReady &&
        s.control.available() == 0 && !s.control.connected()) {
      closeSession(s);
    }
  }

  /// Collects the characters of the next command line
  void readLine(Session& s) {
    while (s.control.available() > 0) {
      int c = s.control.read();
      if (c < 0) break;
      if (c == '\n') {
        s.lineReady = true;
        return;
      }
      // ignore telnet control characters (e.g. sent before ABOR)
      if (c >= 0x80) continue;
      if (s.line.length() < FTP_SERVER_MAX_LINE) s.line += (char)c;
    }
    // last line without line feed
    if (s.line.length() > 0 && !s.control.connected()) s.lineReady = true;
  }

  static bool isAbort(String line) {
    line.trim();
    return line.equalsIgnoreCase("ABOR");
  }

  /// Dispatches a command line: returns false to disconnect
  bool processCommand(Session& s, const String& cmd) {
    int end = cmd.indexOf(' ');
    String verb = end < 0 ? cmd : cmd.substring(0, end);
    // RNTO must directly follow RNFR
    if (!verb.equalsIgnoreCase("RNTO")) s.renameFrom = "";
    for (auto& entry : commands) {
      if (verb.equalsIgnoreCase(entry.name)) {
        if (entry.needsAuth && !s.isLoggedIn) {
          reply(s, "530 Not logged in");
          return true;  // stay connected
        }
        if (entry.builtin != nullptr) return (this->*entry.builtin)(s, cmd);
        return entry.handler(s.control, cmd);
      }
    }
    return onUnknown(s, cmd);
  }

  /// Provides the parameter of a command line
  static String argument(const String& cmd) {
    int start = cmd.indexOf(' ');
    if (start < 0) return "";
    String result = cmd.substring(start + 1);
    result.trim();
    return result;
  }

  /// Converts a path relative to the current directory into a normalized
  /// absolute path (resolving . and ..)
  String resolvePath(Session& s, const String& arg) {
    String path = arg.startsWith("/") ? arg : s.currentDir + "/" + arg;
    std::vector<String> parts;
    int start = 0;
    while (start <= (int)path.length()) {
      int end = path.indexOf('/', start);
      if (end < 0) end = path.length();
      String part = path.substring(start, end);
      if (part == "..") {
        if (!parts.empty()) parts.pop_back();
      } else if (part.length() > 0 && part != ".") {
        parts.push_back(part);
      }
      start = end + 1;
    }
    if (parts.empty()) return "/";
    String result;
    for (auto& part : parts) result += "/" + part;
    return result;
  }

  /// Provides the file name without the directory
  static String baseName(const char* name) {
    String result = name;
    int pos = result.lastIndexOf('/');
    return pos < 0 ? result : result.substring(pos + 1);
  }

  /// Local IP of the control connection if the client type supports it
  template <typename C>
  static auto localIPOf(C& client, int)
      -> decltype(IPAddress(client.localIP())) {
    return client.localIP();
  }
  template <typename C>
  static IPAddress localIPOf(C&, long) {
    return IPAddress(0, 0, 0, 0);
  }

  FileType openFile(const char* path, OpenMode mode) {
    return openIn(storage, path, mode, 0);
  }

  /// Renames a file if the storage supports it
  template <typename S>
  static auto renameIn(S* fs, const char* from, const char* to, int)
      -> decltype(fs->rename(from, to), bool()) {
    return fs->rename(from, to);
  }
  template <typename S>
  static bool renameIn(S*, const char*, const char*, long) {
    return false;
  }
  template <typename S>
  static auto canRename(S* fs, int) -> decltype(fs->rename("", ""), bool()) {
    return true;
  }
  template <typename S>
  static bool canRename(S*, long) {
    return false;
  }

  /// Checks if the path is an existing directory
  bool isDirectory(const String& path) {
    auto file = openFile(path.c_str(), OpenMode::Read);
    bool result = file && file.isDirectory();
    if (file) file.close();
    return result;
  }

  /// Starts the passive data server of the connection and drops data
  /// connections which were opened for an earlier PASV but never used
  void startDataServer(Session& s) {
    TServer& ds = dataServer(s.index);
    if (!s.dataServerStarted) {
      ds.begin();
      s.dataServerStarted = true;
    }
    for (auto stale = ds.accept(); stale; stale = ds.accept()) stale.stop();
  }

  /// Starts a transfer which was requested by PASV, EPSV or PORT: the data is
  /// transferred by stepTransfer()
  bool beginTransfer(Session& s, Transfer transfer, const char* msg) {
    if (s.dataMode == DataMode::None) {
      reply(s, "425 Use PORT or PASV first");
      return false;
    }
    s.transferMode = s.dataMode;
    s.dataMode = DataMode::None;  // one transfer per PASV/PORT
    s.transfer = transfer;
    s.transferMsg = msg;
    s.waitingForData = true;
    s.lastActivity = millis();
    s.bufferLen = 0;
    s.bufferPos = 0;
    s.data = ClientType();
    if (s.transferMode == DataMode::Active) {
      if (!s.data.connect(s.activeIP, s.activePort)) {
        reply(s, "425 Can't open data connection");
        closeTransfer(s);
        return false;
      }
      dataConnected(s);
    }
    return true;
  }

  void dataConnected(Session& s) {
    s.waitingForData = false;
    s.lastActivity = millis();
    reply(s, s.transferMsg);
  }

  /// Processes one step of the running transfer
  void stepTransfer(Session& s) {
    if (s.waitingForData) {
      s.data = dataServer(s.index).accept();
      if (s.data) {
        dataConnected(s);
      } else if (millis() - s.lastActivity >= dataTimeout) {
        reply(s, "425 Can't open data connection");
        closeTransfer(s);
      }
      return;
    }
    switch (s.transfer) {
      case Transfer::Retr:
        stepRetr(s);
        break;
      case Transfer::Stor:
        stepStor(s);
        break;
      case Transfer::List:
        stepList(s);
        break;
      default:
        break;
    }
  }

  void stepRetr(Session& s) {
    if (s.bufferPos >= s.bufferLen) {
      int len = s.file.available() ? s.file.read(s.buffer, sizeof(s.buffer))
                                   : 0;
      if (len <= 0) {
        endTransfer(s, "226 Transfer complete");
        return;
      }
      s.bufferLen = len;
      s.bufferPos = 0;
    }
    size_t written =
        s.data.write(s.buffer + s.bufferPos, s.bufferLen - s.bufferPos);
    s.bufferPos += written;
    if (written > 0) {
      s.lastActivity = millis();
    } else if (!s.data.connected() ||
               millis() - s.lastActivity >= dataTimeout) {
      endTransfer(s, "426 Connection closed; transfer aborted");
    }
  }

  void stepStor(Session& s) {
    int len = s.data.available();
    if (len > 0) {
      // read via Client: some client types hide read(buf, len)
      len = static_cast<Client&>(s.data).read(
          s.buffer, min((size_t)len, sizeof(s.buffer)));
      if (len > 0) s.file.write(s.buffer, len);
      s.lastActivity = millis();
    } else if (!s.data.connected() ||
               millis() - s.lastActivity >= dataTimeout) {
      endTransfer(s, "226 Transfer complete");
    }
  }

  /// Sends one entry of the directory listing
  void stepList(Session& s) {
    if (s.listSingle) {
      printListEntry(s.data, s.file, s.listDetails);
      endTransfer(s, "226 Transfer complete");
      return;
    }
    auto entry = s.file.openNextFile();
    if (!entry) {
      endTransfer(s, "226 Transfer complete");
      return;
    }
    printListEntry(s.data, entry, s.listDetails);
    entry.close();
  }

  /// Ends the transfer and reports the result
  void endTransfer(Session& s, const char* msg) {
    // close the file first, so that it is complete when the client gets the
    // reply
    closeTransfer(s);
    reply(s, msg);
  }

  /// Closes the data connection and the file
  void closeTransfer(Session& s) {
    if (s.data) {
      s.data.flush();
      s.data.stop();
    }
    if (s.file) s.file.close();
    s.file = FileType();
    s.transfer = Transfer::None;
    s.waitingForData = false;
  }

  template <typename F>
  void printListEntry(Client& data, F& entry, bool details) {
    if (details) {
      data.print(entry.isDirectory() ? "drwxr-xr-x" : "-rw-r--r--");
      data.print(" 1 owner group ");
      data.print(entry.isDirectory() ? 0 : (unsigned long)entry.size());
      data.print(" Jan  1 00:00 ");
    }
    data.print(baseName(entry.name()));
    data.print("\r\n");
  }

  bool checkStorage(Session& s) {
    if (!storage) reply(s, "550 Storage not available");
    return storage != nullptr;
  }

  bool list(Session& s, const String& cmd, bool details) {
    if (!checkStorage(s)) return true;
    String arg = argument(cmd);
    if (arg.startsWith("-")) arg = "";  // ignore options like -a
    String path = resolvePath(s, arg);
    auto dir = openFile(path.c_str(), OpenMode::Read);
    if (!dir) {
      reply(s, "550 No such file or directory");
      return true;
    }
    s.listDetails = details;
    s.listSingle = !dir.isDirectory();
    s.file = std::move(dir);
    if (!beginTransfer(s, Transfer::List,
                       "150 Here comes the directory listing")) {
      closeTransfer(s);
    }
    return true;
  }

  /// Uploads a file (STOR or APPE)
  bool store(Session& s, const String& cmd, OpenMode mode) {
    if (!checkStorage(s)) return true;
    String path = resolvePath(s, argument(cmd));
    auto file = openFile(path.c_str(), mode);
    if (!file) {
      reply(s, "550 Cannot open file for writing");
      return true;
    }
    s.file = std::move(file);
    if (!beginTransfer(s, Transfer::Stor, "150 Ready to receive data")) {
      closeTransfer(s);
    }
    return true;
  }

  // Each command handler returns true to continue, false to disconnect
  bool onUSER(Session& s, const String& cmd) {
    // a new USER restarts the login
    s.userName = argument(cmd);
    s.isLoggedIn = !hasUsers();
    reply(s, "331 Username OK, enter password");
    return true;
  }
  bool onPASS(Session& s, const String& cmd) {
    if (hasUsers() && s.userName.length() == 0) {
      reply(s, "503 Login with USER first");
      return true;
    }
    String pwd = argument(cmd);
    if (!hasUsers() || isValidLogin(s.userName, pwd)) {
      reply(s, "230 User logged in");
      s.isLoggedIn = true;
      return true;
    } else {
      reply(s, "530 Login incorrect");
      s.isLoggedIn = false;
      return false;  // disconnect on wrong password
    }
  }
  bool onLIST(Session& s, const String& cmd) { return list(s, cmd, true); }
  bool onNLST(Session& s, const String& cmd) { return list(s, cmd, false); }
  bool onCWD(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    String path = resolvePath(s, argument(cmd));
    auto dir = openFile(path.c_str(), OpenMode::Read);
    if (dir && dir.isDirectory()) {
      s.currentDir = path;
      reply(s, "250 Directory changed to " + path);
    } else {
      reply(s, "550 Not a directory");
    }
    if (dir) dir.close();
    return true;
  }
  bool onCDUP(Session& s, const String& /*cmd*/) { return onCWD(s, "CWD .."); }
  bool onPWD(Session& s, const String& /*cmd*/) {
    reply(s, "257 \"" + s.currentDir + "\" is current directory");
    return true;
  }
  bool onSYST(Session& s, const String& /*cmd*/) {
    reply(s, "215 UNIX Type: L8");
    return true;
  }
  bool onTYPE(Session& s, const String& cmd) {
    reply(s, "200 Type set to " + argument(cmd));
    return true;
  }
  bool onBIN(Session& s, const String& /*cmd*/) {
    reply(s, "200 Type set to I");
    return true;
  }
  bool onASC(Session& s, const String& /*cmd*/) {
    reply(s, "200 Type set to A");
    return true;
  }
  bool onNOOP(Session& s, const String& /*cmd*/) {
    reply(s, "200 NOOP ok");
    return true;
  }
  bool onPASV(Session& s, const String& /*cmd*/) {
    startDataServer(s);
    IPAddress ip = hasPassiveIP ? passiveIP : localIPOf(s.control, 0);
    uint16_t port = dataPort + s.index;
    s.dataMode = DataMode::Passive;
    char msg[80];
    snprintf(msg, sizeof(msg),
             "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)", ip[0], ip[1],
             ip[2], ip[3], port >> 8, port & 0xFF);
    reply(s, msg);
    return true;
  }
  bool onEPSV(Session& s, const String& /*cmd*/) {
    startDataServer(s);
    s.dataMode = DataMode::Passive;
    char msg[80];
    snprintf(msg, sizeof(msg), "229 Entering Extended Passive Mode (|||%d|)",
             dataPort + s.index);
    reply(s, msg);
    return true;
  }
  bool onPORT(Session& s, const String& cmd) {
    int v[6];
    String arg = argument(cmd);
    if (sscanf(arg.c_str(), "%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3],
               &v[4], &v[5]) != 6) {
      reply(s, "501 Syntax error in parameters");
      return true;
    }
    for (int j = 0; j < 6; j++) {
      if (v[j] < 0 || v[j] > 255) {
        reply(s, "501 Syntax error in parameters");
        return true;
      }
    }
    s.activeIP = IPAddress(v[0], v[1], v[2], v[3]);
    s.activePort = (v[4] << 8) | v[5];
    s.dataMode = DataMode::Active;
    reply(s, "200 PORT command successful");
    return true;
  }
  bool onHELP(Session& s, const String& /*cmd*/) {
    reply(s, "214-The following commands are recognized:");
    String names;
    for (auto& entry : commands) {
      names += " ";
      names += entry.name;
    }
    reply(s, names);
    reply(s, "214 Help OK.");
    return true;
  }
  bool onRETR(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    String path = resolvePath(s, argument(cmd));
    auto file = openFile(path.c_str(), OpenMode::Read);
    if (!file || file.isDirectory()) {
      reply(s, "550 File not found");
      if (file) file.close();
      return true;
    }
    s.file = std::move(file);
    if (!beginTransfer(s, Transfer::Retr,
                       "150 Opening data connection for file transfer")) {
      closeTransfer(s);
    }
    return true;
  }
  bool onSTOR(Session& s, const String& cmd) {
    return store(s, cmd, OpenMode::Write);
  }
  bool onAPPE(Session& s, const String& cmd) {
    return store(s, cmd, OpenMode::Append);
  }
  bool onABOR(Session& s, const String& /*cmd*/) {
    if (s.transfer != Transfer::None) {
      closeTransfer(s);
      reply(s, "426 Transfer aborted");
      reply(s, "226 Abort successful");
    } else {
      reply(s, "225 No transfer to abort");
    }
    return true;
  }
  bool onDELE(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    String path = resolvePath(s, argument(cmd));
    if (!storage->exists(path.c_str()) || isDirectory(path)) {
      reply(s, "550 File not found");
    } else if (storage->remove(path.c_str())) {
      reply(s, "250 File deleted");
    } else {
      reply(s, "550 Delete failed");
    }
    return true;
  }
  bool onMKD(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    String arg = argument(cmd);
    String path = resolvePath(s, arg);
    if (arg.length() == 0) {
      reply(s, "501 Missing directory name");
    } else if (storage->exists(path.c_str())) {
      reply(s, "550 Already exists");
    } else if (storage->mkdir(path.c_str())) {
      reply(s, "257 \"" + path + "\" created");
    } else {
      reply(s, "550 Create directory failed");
    }
    return true;
  }
  bool onRMD(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    String path = resolvePath(s, argument(cmd));
    if (path == "/" || !isDirectory(path)) {
      reply(s, "550 Not a directory");
    } else if (storage->rmdir(path.c_str())) {
      reply(s, "250 Directory removed");
      // the directory might be the current directory of any connection
      for (auto& other : sessions) {
        if (other.currentDir == path ||
            other.currentDir.startsWith(path + "/")) {
          other.currentDir = "/";
        }
      }
    } else {
      reply(s, "550 Remove directory failed");
    }
    return true;
  }
  bool onRNFR(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    if (!canRename(storage, 0)) {
      reply(s, "502 Rename not supported by storage");
      return true;
    }
    String path = resolvePath(s, argument(cmd));
    if (path == "/" || !storage->exists(path.c_str())) {
      reply(s, "550 File not found");
    } else {
      s.renameFrom = path;
      reply(s, "350 Ready for destination name");
    }
    return true;
  }
  bool onRNTO(Session& s, const String& cmd) {
    if (s.renameFrom.length() == 0) {
      reply(s, "503 Bad sequence of commands");
      return true;
    }
    String from = s.renameFrom;
    s.renameFrom = "";
    String path = resolvePath(s, argument(cmd));
    if (storage->exists(path.c_str())) {
      reply(s, "553 Destination already exists");
    } else if (renameIn(storage, from.c_str(), path.c_str(), 0)) {
      reply(s, "250 Rename successful");
    } else {
      reply(s, "553 Rename failed");
    }
    return true;
  }
  bool onSIZE(Session& s, const String& cmd) {
    if (!checkStorage(s)) return true;
    String path = resolvePath(s, argument(cmd));
    auto file = openFile(path.c_str(), OpenMode::Read);
    if (!file || file.isDirectory()) {
      reply(s, "550 File not found");
    } else {
      reply(s, "213 " + String((unsigned long)file.size()));
    }
    if (file) file.close();
    return true;
  }
  bool onFEAT(Session& s, const String& /*cmd*/) {
    reply(s, "211-Features:");
    reply(s, " EPSV");
    reply(s, " PASV");
    reply(s, " SIZE");
    reply(s, " UTF8");
    reply(s, "211 End");
    return true;
  }
  bool onOPTS(Session& s, const String& cmd) {
    String arg = argument(cmd);
    if (arg.equalsIgnoreCase("UTF8 ON") || arg.equalsIgnoreCase("UTF8")) {
      reply(s, "200 UTF8 mode enabled");
    } else {
      reply(s, "501 Option not supported");
    }
    return true;
  }
  bool onQUIT(Session& s, const String& /*cmd*/) {
    reply(s, "221 Goodbye");
    s.isLoggedIn = false;
    return false;
  }
  bool onUnknown(Session& s, const String& /*cmd*/) {
    reply(s, "502 Command not implemented");
    return true;
  }
};

}  // namespace tinyftp
