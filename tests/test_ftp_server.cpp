// Automatic tests for the FTPServer running on the Arduino Emulator.
// A scripted MockClient feeds FTP commands to the server and captures its
// replies; storage is the emulator SdFat working on a temporary directory.

#include <Arduino.h>
#include <WiFi.h>

#include <deque>
#include <map>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <SD.h>
// client and server can be used together
#include "TinyFTP.h"

namespace fs_std = std::filesystem;

/// Client which replays a predefined input and records all output.
/// Copies share the same state because the server receives it by value.
class MockClient : public Client {
 public:
  struct State {
    std::string input;
    size_t pos = 0;
    std::string output;
    bool stopped = false;
  };

  /// States handed out to clients opened with connect() (active mode)
  static std::deque<std::shared_ptr<State>> connectQueue;
  static IPAddress lastConnectIP;
  static uint16_t lastConnectPort;

  MockClient() = default;
  explicit MockClient(std::shared_ptr<State> state) : state(state) {}

  int connect(IPAddress ip, uint16_t port) override {
    lastConnectIP = ip;
    lastConnectPort = port;
    if (connectQueue.empty()) return 0;
    state = connectQueue.front();
    connectQueue.pop_front();
    return 1;
  }
  int connect(const char*, uint16_t) override { return 0; }
  size_t write(uint8_t ch) override {
    state->output += (char)ch;
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    state->output.append((const char*)buf, size);
    return size;
  }
  int available() override { return state->input.size() - state->pos; }
  int read() override {
    return available() > 0 ? (uint8_t)state->input[state->pos++] : -1;
  }
  int read(uint8_t* buf, size_t size) override {
    size_t n = 0;
    while (n < size && available() > 0) buf[n++] = read();
    return n;
  }
  int peek() override {
    return available() > 0 ? (uint8_t)state->input[state->pos] : -1;
  }
  void flush() override {}
  void stop() override {
    if (state) state->stopped = true;
  }
  // the client "disconnects" once all scripted input has been consumed
  uint8_t connected() override { return !state->stopped && available() > 0; }
  operator bool() override { return state != nullptr; }
  IPAddress localIP() { return IPAddress(192, 168, 1, 10); }

 protected:
  std::shared_ptr<State> state;
};

std::deque<std::shared_ptr<MockClient::State>> MockClient::connectQueue;
IPAddress MockClient::lastConnectIP;
uint16_t MockClient::lastConnectPort = 0;

/// Server which hands out queued MockClients
class MockServer {
 public:
  MockServer(uint16_t port) : port(port) {}
  void begin() { started = true; }
  MockClient accept() {
    if (pending.empty()) {
      // the next queued connection is opened once the backlog was emptied
      // (e.g. after the server dropped stale data connections for a PASV)
      if (!future.empty()) {
        pending.push_back(future.front());
        future.pop_front();
      }
      return MockClient();
    }
    MockClient result = pending.front();
    pending.pop_front();
    return result;
  }

  uint16_t port;
  bool started = false;
  std::deque<MockClient> pending;  // connections in the backlog
  std::deque<MockClient> future;   // connections which are opened later
};

/// Emulator storage with mode strings ("r", "w", "a") like fs::FS
class ModeSdFat : public SdFat {
 public:
  using SdFat::open;
  File open(const char* path, const char* mode) {
    int flags = mode[0] == 'r'   ? (int)std::ios::in
                : mode[0] == 'a' ? (int)(std::ios::out | std::ios::app)
                                 : (int)std::ios::out;
    return SdFat::open(path, flags);
  }
};
static ModeSdFat ModeSD;

/// Emulator storage extended by rename (like ESP32 fs::FS or LittleFS)
class RenameSdFat : public SdFat {
 public:
  bool rename(const char* from, const char* to) {
    return std::rename(from, to) == 0;
  }
};
static RenameSdFat RenameSD;

/// Exposes the mock servers so that tests can inject sessions
template <typename TStorage>
class TestFTPServerT : public FTPServer<MockServer, TStorage> {
  using Base = FTPServer<MockServer, TStorage>;
  using Base::server;
  using Base::sessions;

 public:
  using Base::Base;
  using Base::connectionCount;
  using Base::doLoop;
  MockServer& mockServer() { return server; }
  /// passive data server of the (first) connection
  MockServer& mockDataServer(int index = 0) { return Base::dataServer(index); }
  bool loggedIn() {
    for (auto& s : sessions)
      if (s.active && s.isLoggedIn) return true;
    return false;
  }

  /// Queues a passive data connection: returns its state
  std::shared_ptr<MockClient::State> addDataClient(
      const std::string& input = "", int index = 0) {
    auto state = std::make_shared<MockClient::State>();
    state->input = input;
    mockDataServer(index).future.push_back(MockClient(state));
    return state;
  }

  /// Connects a client with the given input: returns its state
  std::shared_ptr<MockClient::State> connect(const std::string& input) {
    auto state = std::make_shared<MockClient::State>();
    state->input = input;
    server.pending.push_back(MockClient(state));
    return state;
  }

  /// Calls doLoop() until all connections are closed
  void run() {
    unsigned long start = millis();
    do {
      doLoop();
    } while ((connectionCount() > 0 || !server.pending.empty()) &&
             millis() - start < 5000);
  }

  /// Runs one client session with the given input and returns the output
  std::string session(const std::string& input,
                      std::shared_ptr<MockClient::State>* outState = nullptr) {
    auto state = connect(input);
    run();
    if (outState) *outState = state;
    return state->output;
  }
};
using TestFTPServer = TestFTPServerT<SdFat>;

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int failures = 0;
static int checks = 0;
static const char* currentTest = "";

#define CHECK(cond)                                                        \
  do {                                                                     \
    checks++;                                                              \
    if (!(cond)) {                                                         \
      failures++;                                                          \
      printf("  FAILED %s:%d [%s]: %s\n", __FILE__, __LINE__, currentTest, \
             #cond);                                                       \
    }                                                                      \
  } while (0)

#define CHECK_CONTAINS(text, part) CHECK(contains(text, part))
#define CHECK_NOT_CONTAINS(text, part) CHECK(!contains(text, part))

static bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

static size_t count(const std::string& text, const std::string& part) {
  size_t result = 0;
  for (size_t p = text.find(part); p != std::string::npos;
       p = text.find(part, p + 1))
    result++;
  return result;
}

#define RUN(test)            \
  do {                       \
    currentTest = #test;     \
    printf("- %s\n", #test); \
    test();                  \
  } while (0)

/// Temporary directory used as FTP storage root
static fs_std::path root;

static std::string path(const std::string& rel) {
  return (root / rel).string();
}

static void writeFile(const std::string& rel, const std::string& content) {
  std::ofstream out(path(rel), std::ios::binary);
  out << content;
}

static std::string readFile(const std::string& rel) {
  std::ifstream in(path(rel), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

static const char* USER = "test";
static const char* PWD = "secret";
static const std::string LOGIN =
    std::string("USER ") + USER + "\r\nPASS " + PWD + "\r\n";

/// Server with a user and a short data timeout
struct Fixture {
  TestFTPServer ftp{21, &SD};
  Fixture(bool withUser = true) {
    if (withUser) ftp.addUser(USER, PWD);
    ftp.setDataTimeout(50);
    ftp.begin();
  }
};

// ---------------------------------------------------------------------------
// Session and login
// ---------------------------------------------------------------------------

static void testNoClientDoesNothing() {
  Fixture f;
  CHECK(f.ftp.mockServer().started);
  CHECK(f.ftp.mockServer().port == 21);
  CHECK(f.ftp.mockDataServer().port == FTP_DATA_PORT);
  CHECK(!f.ftp.mockDataServer().started);  // only started by PASV
  f.ftp.doLoop();  // must return immediately without a pending client
}

static void testWelcomeAndQuit() {
  Fixture f;
  std::shared_ptr<MockClient::State> state;
  std::string out = f.ftp.session("QUIT\r\n", &state);
  CHECK(out.rfind("220 ", 0) == 0);
  CHECK_CONTAINS(out, "221 Goodbye");
  CHECK(state->stopped);
}

static void testLoginOk() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "NOOP\r\n");
  CHECK_CONTAINS(out, "331 ");
  CHECK_CONTAINS(out, "230 User logged in");
  CHECK_CONTAINS(out, "200 NOOP ok");
}

static void testLoginWrongPasswordDisconnects() {
  Fixture f;
  std::shared_ptr<MockClient::State> state;
  std::string out =
      f.ftp.session("USER test\r\nPASS wrong\r\nNOOP\r\n", &state);
  CHECK_CONTAINS(out, "530 Login incorrect");
  CHECK_NOT_CONTAINS(out, "200 NOOP ok");  // connection was closed
  CHECK(state->stopped);
  CHECK(!f.ftp.loggedIn());
}

static void testCommandsRequireLogin() {
  Fixture f;
  std::string out = f.ftp.session(
      "PWD\r\nLIST\r\nRETR /x\r\nSTOR /x\r\nCWD /\r\nPASV\r\nPORT 1,2,3,4,5,6\r\n");
  CHECK(count(out, "530 Not logged in") == 7);
}

static void testHelpWithoutLogin() {
  Fixture f;
  std::string out = f.ftp.session("HELP\r\n");
  CHECK_CONTAINS(out, "214-");
  CHECK_CONTAINS(out, " CWD ");
  CHECK_CONTAINS(out, " PASV ");
  CHECK_CONTAINS(out, "214 Help OK.");
}

static void testLoginIsPerConnection() {
  Fixture f;
  // first client logs in and drops the connection without QUIT
  f.ftp.session(LOGIN);
  CHECK(!f.ftp.loggedIn());
  // the next client must log in again
  std::string out = f.ftp.session("PWD\r\n");
  CHECK_CONTAINS(out, "530 Not logged in");
}

static void testQuitLogsOut() {
  Fixture f;
  f.ftp.session(LOGIN + "QUIT\r\n");
  CHECK(!f.ftp.loggedIn());
  std::string out = f.ftp.session("PWD\r\n");
  CHECK_CONTAINS(out, "530 Not logged in");
}

static void testWrongUserDisconnects() {
  Fixture f;
  std::shared_ptr<MockClient::State> state;
  std::string out =
      f.ftp.session("USER other\r\nPASS secret\r\nNOOP\r\n", &state);
  CHECK_CONTAINS(out, "530 Login incorrect");
  CHECK_NOT_CONTAINS(out, "200 NOOP ok");
  CHECK(state->stopped);
}

static void testPassWithoutUser() {
  Fixture f;
  std::string out = f.ftp.session("PASS secret\r\nNOOP\r\n" + LOGIN +
                                  "NOOP\r\n");
  CHECK_CONTAINS(out, "503 Login with USER first");
  CHECK(count(out, "530 Not logged in") == 1);  // first NOOP
  CHECK(count(out, "200 NOOP ok") == 1);        // after the login
}

static void testMultipleUsers() {
  Fixture f;
  f.ftp.addUser("admin", "admin-pwd");
  std::string out =
      f.ftp.session("USER admin\r\nPASS admin-pwd\r\nNOOP\r\n");
  CHECK_CONTAINS(out, "230 User logged in");
  CHECK_CONTAINS(out, "200 NOOP ok");
  out = f.ftp.session(LOGIN + "NOOP\r\n");
  CHECK_CONTAINS(out, "200 NOOP ok");
  // passwords are bound to their user
  out = f.ftp.session("USER admin\r\nPASS secret\r\n");
  CHECK_CONTAINS(out, "530 Login incorrect");
}

static void testAddUserUpdatesPassword() {
  Fixture f;
  f.ftp.addUser(USER, "changed");
  std::string out = f.ftp.session(LOGIN);
  CHECK_CONTAINS(out, "530 Login incorrect");
  out = f.ftp.session("USER test\r\nPASS changed\r\nNOOP\r\n");
  CHECK_CONTAINS(out, "200 NOOP ok");
}

static void testUserRestartsLogin() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "USER other\r\nNOOP\r\n");
  CHECK_CONTAINS(out, "230 User logged in");
  CHECK_CONTAINS(out, "530 Not logged in");
  CHECK_NOT_CONTAINS(out, "200 NOOP ok");
}

static void testNoPasswordOpenServer() {
  Fixture f(false);
  std::string out = f.ftp.session("PWD\r\n");
  CHECK_CONTAINS(out, "257 \"/\" is current directory");
}

static void testNoPasswordAcceptsAnyPassword() {
  Fixture f(false);
  std::string out =
      f.ftp.session("USER anonymous\r\nPASS guest@\r\nNOOP\r\nQUIT\r\n");
  CHECK_CONTAINS(out, "230 User logged in");
  CHECK_CONTAINS(out, "200 NOOP ok");
  // a QUIT must not lock out the following clients
  out = f.ftp.session("PWD\r\n");
  CHECK_CONTAINS(out, "257 ");
}

// ---------------------------------------------------------------------------
// Command parsing
// ---------------------------------------------------------------------------

static void testSimpleReplies() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "SYST\r\nTYPE I\r\nNOOP\r\nPWD\r\n");
  CHECK_CONTAINS(out, "215 UNIX Type: L8");
  CHECK_CONTAINS(out, "200 Type set to I");
  CHECK_CONTAINS(out, "200 NOOP ok");
  CHECK_CONTAINS(out, "257 \"/\" is current directory");
}

static void testCommandsAreCaseInsensitive() {
  Fixture f;
  std::string out = f.ftp.session("user test\r\npass secret\r\nnoop\r\n");
  CHECK_CONTAINS(out, "230 User logged in");
  CHECK_CONTAINS(out, "200 NOOP ok");
}

static void testCommandNeedsExactMatch() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "NOOPX\r\nPWDX\r\n");
  CHECK(count(out, "502 Command not implemented") == 2);
  CHECK_NOT_CONTAINS(out, "200 NOOP ok");
  CHECK_NOT_CONTAINS(out, "257 ");
}

static void testUnknownCommand() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "FOOBAR\r\nNOOP\r\n");
  CHECK_CONTAINS(out, "502 Command not implemented");
  CHECK_CONTAINS(out, "200 NOOP ok");  // still connected
}

static void testNoStorage() {
  TestFTPServer ftp(21, nullptr);
  ftp.addUser(USER, PWD);
  ftp.begin();
  std::string out = ftp.session(LOGIN +
                                "LIST\r\nCWD /\r\nRETR /a\r\nSTOR /a\r\n");
  CHECK(count(out, "550 Storage not available") == 4);
}

// ---------------------------------------------------------------------------
// Directories
// ---------------------------------------------------------------------------

static void testCwdAbsoluteRelativeAndUp() {
  fs_std::create_directories(path("nav/sub"));
  Fixture f;
  std::string base = path("nav");
  std::string out = f.ftp.session(LOGIN + "CWD " + base +
                                  "\r\nPWD\r\nCWD sub\r\nPWD\r\nCDUP\r\nPWD\r\n"
                                  "CWD ./sub/..\r\nPWD\r\nCD sub\r\nPWD\r\n");
  CHECK_CONTAINS(out, "250 Directory changed to " + base);
  CHECK(count(out, "257 \"" + base + "\" is current directory") == 3);
  CHECK(count(out, "257 \"" + base + "/sub\" is current directory") == 2);
}

static void testCwdUpStopsAtRoot() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "CWD ../../..\r\nPWD\r\n");
  CHECK_CONTAINS(out, "250 Directory changed to /");
  CHECK_CONTAINS(out, "257 \"/\" is current directory");
}

static void testCwdInvalid() {
  writeFile("plain.txt", "x");
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "CWD " + path("missing") +
                                  "\r\nCWD " + path("plain.txt") +
                                  "\r\nPWD\r\n");
  CHECK(count(out, "550 Not a directory") == 2);
  CHECK_CONTAINS(out, "257 \"/\" is current directory");  // unchanged
}

static void testCurrentDirIsPerConnection() {
  fs_std::create_directories(path("nav"));
  Fixture f;
  f.ftp.session(LOGIN + "CWD " + path("nav") + "\r\n");
  std::string out = f.ftp.session(LOGIN + "PWD\r\n");
  CHECK_CONTAINS(out, "257 \"/\" is current directory");
}

// ---------------------------------------------------------------------------
// Data connection
// ---------------------------------------------------------------------------

static void testPasvUsesLocalIP() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "PASV\r\n");
  // 50009 = 195 * 256 + 89
  CHECK_CONTAINS(out, "227 Entering Passive Mode (192,168,1,10,195,89)");
  CHECK(f.ftp.mockDataServer().started);
}

static void testPasvUsesConfiguredIP() {
  Fixture f;
  f.ftp.setPassiveIP(IPAddress(10, 0, 0, 7));
  std::string out = f.ftp.session(LOGIN + "PASV\r\n");
  CHECK_CONTAINS(out, "227 Entering Passive Mode (10,0,0,7,195,89)");
}

static void testEpsv() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "EPSV\r\n");
  CHECK_CONTAINS(out, "229 Entering Extended Passive Mode (|||50009|)");
  CHECK(f.ftp.mockDataServer().started);
}

static void testTransferWithoutPasvFails() {
  writeFile("hello.txt", "Hello FTP");
  Fixture f;
  std::string out =
      f.ftp.session(LOGIN + "RETR " + path("hello.txt") + "\r\nLIST\r\n");
  CHECK(count(out, "425 Use PORT or PASV first") == 2);
  CHECK_NOT_CONTAINS(out, "Hello FTP");
}

static void testDataConnectionTimeout() {
  writeFile("hello.txt", "Hello FTP");
  Fixture f;  // no data client is queued
  std::string out =
      f.ftp.session(LOGIN + "PASV\r\nRETR " + path("hello.txt") + "\r\n");
  CHECK_CONTAINS(out, "425 Can't open data connection");
}

static void testPasvIsUsedOnlyOnce() {
  writeFile("hello.txt", "Hello FTP");
  Fixture f;
  f.ftp.addDataClient();
  std::string out = f.ftp.session(LOGIN + "PASV\r\nRETR " + path("hello.txt") +
                                  "\r\nRETR " + path("hello.txt") + "\r\n");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK_CONTAINS(out, "425 Use PORT or PASV first");
}

static void testList() {
  fs_std::create_directories(path("dir/sub"));
  writeFile("dir/file1.txt", "1");
  writeFile("dir/file2.txt", "22");
  Fixture f;
  auto data = f.ftp.addDataClient();
  std::string out =
      f.ftp.session(LOGIN + "CWD " + path("dir") + "\r\nPASV\r\nLIST\r\n");
  CHECK_CONTAINS(out, "150 Here comes the directory listing");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK_NOT_CONTAINS(out, "file1.txt");  // not on the control connection
  CHECK_CONTAINS(data->output, "-rw-r--r-- 1 owner group 1 Jan  1 00:00 file1.txt\r\n");
  CHECK_CONTAINS(data->output, "-rw-r--r-- 1 owner group 2 Jan  1 00:00 file2.txt\r\n");
  CHECK_CONTAINS(data->output, "drwxr-xr-x 1 owner group 0 Jan  1 00:00 sub\r\n");
  CHECK_NOT_CONTAINS(data->output, root.string());  // only base names
  CHECK(data->stopped);
}

static void testListWithPathAndOptions() {
  fs_std::create_directories(path("dir"));
  writeFile("dir/file1.txt", "1");
  Fixture f;
  auto data1 = f.ftp.addDataClient();
  auto data2 = f.ftp.addDataClient();
  std::string out = f.ftp.session(LOGIN + "PASV\r\nLIST " + path("dir") +
                                  "\r\nCWD " + path("dir") +
                                  "\r\nPASV\r\nLIST -a\r\n");
  CHECK(count(out, "226 Transfer complete") == 2);
  CHECK_CONTAINS(data1->output, "file1.txt");
  CHECK_CONTAINS(data2->output, "file1.txt");
}

static void testListMissing() {
  Fixture f;
  f.ftp.addDataClient();
  std::string out =
      f.ftp.session(LOGIN + "PASV\r\nLIST " + path("missing") + "\r\n");
  CHECK_CONTAINS(out, "550 No such file or directory");
}

static void testNlst() {
  fs_std::create_directories(path("dir"));
  writeFile("dir/file1.txt", "1");
  Fixture f;
  auto data = f.ftp.addDataClient();
  std::string out =
      f.ftp.session(LOGIN + "PASV\r\nNLST " + path("dir") + "\r\n");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK_CONTAINS(data->output, "file1.txt\r\n");
  CHECK_NOT_CONTAINS(data->output, "owner");
}

static void testRetr() {
  writeFile("hello.txt", "Hello FTP");
  Fixture f;
  auto data = f.ftp.addDataClient();
  std::string out =
      f.ftp.session(LOGIN + "PASV\r\nRETR " + path("hello.txt") + "\r\n");
  CHECK_CONTAINS(out, "150 Opening data connection");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK_NOT_CONTAINS(out, "Hello FTP");
  CHECK(data->output == "Hello FTP");
  CHECK(data->stopped);
}

static void testRetrLargeBinaryFile() {
  std::string content;
  for (int j = 0; j < 5000; j++) content += (char)(j % 256);
  writeFile("large.bin", content);
  Fixture f;
  auto data = f.ftp.addDataClient();
  f.ftp.session(LOGIN + "EPSV\r\nRETR " + path("large.bin") + "\r\n");
  CHECK(data->output == content);
}

static void testRetrRelativeToCurrentDir() {
  fs_std::create_directories(path("dir"));
  writeFile("dir/rel.txt", "relative");
  Fixture f;
  auto data = f.ftp.addDataClient();
  f.ftp.session(LOGIN + "CWD " + path("dir") + "\r\nPASV\r\nRETR rel.txt\r\n");
  CHECK(data->output == "relative");
}

static void testRetrMissing() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "PASV\r\nRETR " +
                                  path("nope.txt") + "\r\nRETR " +
                                  root.string() + "\r\n");
  CHECK(count(out, "550 File not found") == 2);  // missing and directory
}

static void testStor() {
  Fixture f;
  std::string content = "line1\nline2\r\n";
  for (int j = 0; j < 2000; j++) content += (char)(j % 256);
  auto data = f.ftp.addDataClient(content);
  std::string out =
      f.ftp.session(LOGIN + "PASV\r\nSTOR " + path("upload.bin") + "\r\n");
  CHECK_CONTAINS(out, "150 Ready to receive data");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(readFile("upload.bin") == content);
  CHECK(data->stopped);
}

static void testStorRelativeToCurrentDir() {
  fs_std::create_directories(path("dir"));
  Fixture f;
  f.ftp.addDataClient("uploaded");
  f.ftp.session(LOGIN + "CWD " + path("dir") + "\r\nPASV\r\nSTOR up.txt\r\n");
  CHECK(readFile("dir/up.txt") == "uploaded");
}

static void testStorInvalidPath() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "PASV\r\nSTOR " +
                                  path("missing-dir/x.bin") + "\r\n");
  CHECK_CONTAINS(out, "550 Cannot open file for writing");
}

static void testPortActiveMode() {
  writeFile("hello.txt", "Hello FTP");
  Fixture f;
  auto data = std::make_shared<MockClient::State>();
  MockClient::connectQueue.push_back(data);
  std::string out = f.ftp.session(LOGIN + "PORT 127,0,0,1,4,1\r\nRETR " +
                                  path("hello.txt") + "\r\n");
  CHECK_CONTAINS(out, "200 PORT command successful");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(MockClient::lastConnectIP == IPAddress(127, 0, 0, 1));
  CHECK(MockClient::lastConnectPort == 1025);
  CHECK(data->output == "Hello FTP");
}

static void testPortConnectFails() {
  writeFile("hello.txt", "Hello FTP");
  Fixture f;
  MockClient::connectQueue.clear();
  std::string out = f.ftp.session(LOGIN + "PORT 127,0,0,1,4,1\r\nRETR " +
                                  path("hello.txt") + "\r\n");
  CHECK_CONTAINS(out, "425 Can't open data connection");
}

static void testPortInvalid() {
  Fixture f;
  std::string out =
      f.ftp.session(LOGIN + "PORT 1,2,3\r\nPORT 1,2,3,4,5,256\r\nPORT\r\n");
  CHECK(count(out, "501 Syntax error in parameters") == 3);
}

// ---------------------------------------------------------------------------
// Custom commands
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// File management
// ---------------------------------------------------------------------------

static void testMkdAndRmd() {
  fs_std::create_directories(path("mk"));
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "CWD " + path("mk") +
                                  "\r\nMKD new\r\nMKD new\r\nMKD\r\nCWD new\r\n"
                                  "RMD " + path("mk/new") + "\r\nPWD\r\n");
  CHECK_CONTAINS(out, "257 \"" + path("mk/new") + "\" created");
  CHECK_CONTAINS(out, "550 Already exists");
  CHECK_CONTAINS(out, "501 Missing directory name");
  CHECK_CONTAINS(out, "250 Directory removed");
  CHECK(!fs_std::exists(path("mk/new")));
  // the removed directory is no longer the current directory
  CHECK_CONTAINS(out, "257 \"/\" is current directory");
}

static void testRmdInvalid() {
  fs_std::create_directories(path("full/x"));
  writeFile("rmfile.txt", "x");
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "RMD " + path("missing") +
                                  "\r\nRMD " + path("rmfile.txt") +
                                  "\r\nRMD /\r\nRMD " + path("full") + "\r\n");
  CHECK(count(out, "550 Not a directory") == 3);
  CHECK_CONTAINS(out, "550 Remove directory failed");  // not empty
  CHECK(fs_std::exists(path("full/x")));
}

static void testDele() {
  fs_std::create_directories(path("deldir"));
  writeFile("del.txt", "x");
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "DELE " + path("del.txt") +
                                  "\r\nDELE " + path("del.txt") + "\r\nDELE " +
                                  path("deldir") + "\r\n");
  CHECK_CONTAINS(out, "250 File deleted");
  CHECK(count(out, "550 File not found") == 2);  // missing and directory
  CHECK(!fs_std::exists(path("del.txt")));
  CHECK(fs_std::exists(path("deldir")));
}

static void testSize() {
  writeFile("size.txt", "12345");
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "SIZE " + path("size.txt") +
                                  "\r\nSIZE " + path("nope") + "\r\nSIZE " +
                                  root.string() + "\r\n");
  CHECK_CONTAINS(out, "213 5\r\n");
  CHECK(count(out, "550 File not found") == 2);
}

static void testFeatAndOpts() {
  Fixture f;
  std::string out = f.ftp.session("FEAT\r\nOPTS UTF8 ON\r\nOPTS FOO\r\n");
  CHECK_CONTAINS(out, "211-Features:");
  CHECK_CONTAINS(out, " EPSV\r\n");
  CHECK_CONTAINS(out, " SIZE\r\n");
  CHECK_CONTAINS(out, "211 End");
  CHECK_CONTAINS(out, "200 UTF8 mode enabled");
  CHECK_CONTAINS(out, "501 Option not supported");
  CHECK_NOT_CONTAINS(out, "530");
}

static void testFileCommandsRequireLogin() {
  Fixture f;
  std::string out = f.ftp.session(
      "DELE /x\r\nMKD /x\r\nRMD /x\r\nRNFR /x\r\nRNTO /y\r\nSIZE /x\r\n");
  CHECK(count(out, "530 Not logged in") == 6);
}

static void testRenameNotSupported() {
  writeFile("ren.txt", "x");
  Fixture f;  // emulator SdFat has no rename()
  std::string out = f.ftp.session(LOGIN + "RNFR " + path("ren.txt") + "\r\n");
  CHECK_CONTAINS(out, "502 Rename not supported by storage");
}

static void testRename() {
  fs_std::create_directories(path("rendir"));
  writeFile("ren1.txt", "content");
  TestFTPServerT<RenameSdFat> ftp(21, &RenameSD);
  ftp.addUser(USER, PWD);
  ftp.begin();
  std::string out = ftp.session(LOGIN + "CWD " + path("rendir") +
                                "\r\nRNFR ../ren1.txt\r\nRNTO ren2.txt\r\n");
  CHECK_CONTAINS(out, "350 Ready for destination name");
  CHECK_CONTAINS(out, "250 Rename successful");
  CHECK(!fs_std::exists(path("ren1.txt")));
  CHECK(readFile("rendir/ren2.txt") == "content");
}

static void testRenameErrors() {
  writeFile("a.txt", "a");
  writeFile("b.txt", "b");
  TestFTPServerT<RenameSdFat> ftp(21, &RenameSD);
  ftp.addUser(USER, PWD);
  ftp.begin();
  std::string out = ftp.session(
      LOGIN + "RNTO " + path("c.txt") + "\r\nRNFR " + path("missing") +
      "\r\nRNFR " + path("a.txt") + "\r\nRNTO " + path("b.txt") + "\r\nRNFR " +
      path("a.txt") + "\r\nNOOP\r\nRNTO " + path("c.txt") + "\r\n");
  CHECK(count(out, "503 Bad sequence of commands") == 2);  // no or stale RNFR
  CHECK_CONTAINS(out, "550 File not found");
  CHECK_CONTAINS(out, "553 Destination already exists");
  CHECK(readFile("a.txt") == "a");
  CHECK(readFile("b.txt") == "b");
  CHECK(!fs_std::exists(path("c.txt")));
}

static void testCustomCommand() {
  Fixture f;
  f.ftp.addCommand("MYCMD", [](Client& c, const String& cmd) {
    c.println("200 custom");
    return true;
  });
  // replace an existing handler
  f.ftp.addCommand("SYST", [](Client& c, const String& cmd) {
    c.println("215 custom system");
    return true;
  });
  std::string out = f.ftp.session(LOGIN + "mycmd\r\nSYST\r\n");
  CHECK_CONTAINS(out, "200 custom");
  CHECK_CONTAINS(out, "215 custom system");
  CHECK_NOT_CONTAINS(out, "UNIX Type");
}

static void testCustomCommandRequiresLogin() {
  Fixture f;
  f.ftp.addCommand("MYCMD", [](Client& c, const String& cmd) {
    c.println("200 custom");
    return true;
  });
  f.ftp.addCommand(
      "OPEN",
      [](Client& c, const String& cmd) {
        c.println("200 open");
        return true;
      },
      false);
  std::string out = f.ftp.session("MYCMD\r\nOPEN\r\n");
  CHECK_CONTAINS(out, "530 Not logged in");
  CHECK_NOT_CONTAINS(out, "200 custom");
  CHECK_CONTAINS(out, "200 open");
}

static void testCustomCommandCanDisconnect() {
  Fixture f;
  f.ftp.addCommand("BYE", [](Client& c, const String& cmd) {
    c.println("221 bye");
    return false;
  });
  std::string out = f.ftp.session(LOGIN + "BYE\r\nNOOP\r\n");
  CHECK_CONTAINS(out, "221 bye");
  CHECK_NOT_CONTAINS(out, "200 NOOP ok");
}

// ---------------------------------------------------------------------------
// Additional commands and multiple connections
// ---------------------------------------------------------------------------

static std::string binaryContent(int size) {
  std::string content;
  for (int j = 0; j < size; j++) content += (char)(j % 256);
  return content;
}

static void testBinAscAndOptsUtf8() {
  Fixture f;
  std::string out =
      f.ftp.session(LOGIN + "BIN\r\nASC\r\nOPTS UTF8\r\nOPTS UTF8 ON\r\n");
  CHECK_CONTAINS(out, "200 Type set to I");
  CHECK_CONTAINS(out, "200 Type set to A");
  CHECK(count(out, "200 UTF8 mode enabled") == 2);
}

static void testAppe() {
  TestFTPServerT<ModeSdFat> ftp{21, &ModeSD};
  ftp.addUser(USER, PWD);
  ftp.setDataTimeout(50);
  ftp.begin();
  writeFile("append.txt", "Hello ");
  auto data = ftp.addDataClient("World");
  std::string out =
      ftp.session(LOGIN + "PASV\r\nAPPE " + path("append.txt") + "\r\n");
  CHECK_CONTAINS(out, "150 Ready to receive data");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(readFile("append.txt") == "Hello World");
  // STOR replaces the file
  auto data2 = ftp.addDataClient("New");
  ftp.session(LOGIN + "PASV\r\nSTOR " + path("append.txt") + "\r\n");
  CHECK(readFile("append.txt") == "New");
}

static void testStorReplacesExistingFile() {
  writeFile("replace.txt", "old content which is longer");
  Fixture f;
  auto data = f.ftp.addDataClient("new");
  f.ftp.session(LOGIN + "PASV\r\nSTOR " + path("replace.txt") + "\r\n");
  CHECK(readFile("replace.txt") == "new");
}

static void testAborDuringTransfer() {
  std::string content = binaryContent(20000);
  writeFile("abort.bin", content);
  Fixture f;
  auto data = f.ftp.addDataClient();
  std::string out = f.ftp.session(LOGIN + "PASV\r\nRETR " +
                                  path("abort.bin") + "\r\nABOR\r\n");
  CHECK_CONTAINS(out, "150 Opening data connection");
  CHECK_CONTAINS(out, "426 Transfer aborted");
  CHECK_CONTAINS(out, "226 Abort successful");
  CHECK_NOT_CONTAINS(out, "226 Transfer complete");
  CHECK(data->output.size() < content.size());
  CHECK(data->stopped);
}

static void testAborWithoutTransfer() {
  Fixture f;
  std::string out = f.ftp.session(LOGIN + "ABOR\r\n");
  CHECK_CONTAINS(out, "225 ");
}

static void testCommandsWaitForTransfer() {
  std::string content = binaryContent(5000);
  writeFile("wait.bin", content);
  Fixture f;
  auto data = f.ftp.addDataClient();
  std::string out = f.ftp.session(LOGIN + "PASV\r\nRETR " +
                                  path("wait.bin") + "\r\nNOOP\r\n");
  CHECK(data->output == content);
  size_t done = out.find("226 Transfer complete");
  size_t noop = out.find("200 NOOP ok");
  CHECK(done != std::string::npos && noop != std::string::npos);
  CHECK(done < noop);
}

static void testMultipleConnections() {
  std::string content = binaryContent(10000);
  writeFile("multi.bin", content);
  Fixture f;
  auto c1 = f.ftp.connect(LOGIN + "PASV\r\nRETR " + path("multi.bin") + "\r\n");
  auto c2 = f.ftp.connect(LOGIN + "PASV\r\nRETR " + path("multi.bin") + "\r\n");
  auto d1 = f.ftp.addDataClient("", 0);
  auto d2 = f.ftp.addDataClient("", 1);
  // both transfers are running at the same time
  bool interleaved = false;
  for (int j = 0; j < 1000 && (d1->output.size() < content.size() ||
                               d2->output.size() < content.size());
       j++) {
    f.ftp.doLoop();
    if (!d1->output.empty() && !d2->output.empty() &&
        d1->output.size() < content.size() &&
        d2->output.size() < content.size())
      interleaved = true;
  }
  f.ftp.run();
  CHECK(interleaved);
  CHECK(d1->output == content);
  CHECK(d2->output == content);
  // each connection has its own passive data port
  CHECK_CONTAINS(c1->output, ",195,89)");  // 50009
  CHECK_CONTAINS(c2->output, ",195,90)");  // 50010
}

static void testTooManyConnections() {
  Fixture f;
  std::string noops;
  for (int j = 0; j < 50; j++) noops += "NOOP\r\n";
  std::vector<std::shared_ptr<MockClient::State>> clients;
  for (int j = 0; j <= FTP_SERVER_MAX_SESSIONS; j++)
    clients.push_back(f.ftp.connect(LOGIN + noops));
  for (int j = 0; j <= FTP_SERVER_MAX_SESSIONS; j++) f.ftp.doLoop();
  CHECK(f.ftp.connectionCount() == FTP_SERVER_MAX_SESSIONS);
  CHECK_CONTAINS(clients.back()->output, "421 ");
  CHECK(clients.back()->stopped);
  f.ftp.run();
  for (int j = 0; j < FTP_SERVER_MAX_SESSIONS; j++)
    CHECK(count(clients[j]->output, "200 NOOP ok") == 50);
}

static void testRmdResetsCurrentDirOfOtherConnections() {
  fs_std::create_directories(path("shared"));
  Fixture f;
  std::string noops;
  for (int j = 0; j < 20; j++) noops += "NOOP\r\n";
  auto c1 = f.ftp.connect(LOGIN + "CWD " + path("shared") + "\r\n" + noops +
                          "PWD\r\n");
  auto c2 = f.ftp.connect(LOGIN + "NOOP\r\nNOOP\r\nNOOP\r\nRMD " +
                          path("shared") + "\r\n");
  f.ftp.run();
  CHECK_CONTAINS(c2->output, "250 Directory removed");
  CHECK_CONTAINS(c1->output, "257 \"/\" is current directory");
}

// ---------------------------------------------------------------------------
// CallbackFS
// ---------------------------------------------------------------------------

/// Records the CallbackFS operations; exists() is true for the paths in
/// existing
struct CallbackFixture {
  TestFTPServerT<CallbackFSClass> ftp{21, &CallbackFS};
  std::vector<std::string> ops;
  std::vector<std::string> existing;
  // directories which are provided by the list callback
  std::map<std::string, std::vector<CallbackFileInfo>> dirs;
  // files which are provided by the read callback
  std::map<std::string, std::string> files;
  // files which were received by the write callback (paths starting with /up)
  std::map<std::string, std::string> uploads;

  CallbackFixture() {
    CallbackFS.setReadCallback(
        [this](const char* path, size_t pos, uint8_t* data, size_t len) {
          auto it = files.find(path);
          if (it == files.end()) return -1;
          if (data == nullptr) return (int)it->second.size();
          if (pos >= it->second.size()) return 0;
          size_t n = std::min(len, it->second.size() - pos);
          memcpy(data, it->second.data() + pos, n);
          return (int)n;
        });
    CallbackFS.setWriteCallback(
        [this](const char* path, const uint8_t* data, size_t len) {
          if (data == nullptr) return strncmp(path, "/up", 3) == 0;
          uploads[path].append((const char*)data, len);
          return true;
        });
    CallbackFS.setListCallback(
        [this](const char* path, std::vector<CallbackFileInfo>& entries) {
          auto it = dirs.find(path);
          if (it == dirs.end()) return false;
          entries = it->second;
          return true;
        });
    CallbackFS.setCallback([this](CallbackFSClass::Operation op,
                                  const char* path, const char* param) {
      std::string entry = std::string(CallbackFSClass::toString(op)) + " " +
                          path + (param ? std::string(" ") + param : "");
      ops.push_back(entry);
      // a new upload replaces the file: "a" appends
      if (op == CallbackFSClass::Open && strcmp(param, "w") == 0)
        uploads[path].clear();
      if (op == CallbackFSClass::Exists) {
        for (auto& e : existing)
          if (e == path) return true;
        return false;
      }
      return true;
    });
    ftp.addUser(USER, PWD);
    ftp.setDataTimeout(50);
    ftp.begin();
  }
  ~CallbackFixture() {
    CallbackFS.setCallback(nullptr);
    CallbackFS.setListCallback(nullptr);
    CallbackFS.setReadCallback(nullptr);
    CallbackFS.setWriteCallback(nullptr);
  }

  bool has(const std::string& entry) {
    for (auto& op : ops)
      if (op == entry) return true;
    return false;
  }
};

static void testCallbackFSIsSingleInstance() {
  CHECK(&CallbackFS == &CallbackFSClass::instance());
}

static void testCallbackFSMkd() {
  CallbackFixture f;
  std::string out = f.ftp.session(LOGIN + "MKD new\r\n");
  CHECK_CONTAINS(out, "257 \"/new\" created");
  CHECK(f.has("exists /new"));
  CHECK(f.has("mkdir /new"));
}

static void testCallbackFSDeleteAndRename() {
  CallbackFixture f;
  f.existing.push_back("/a.txt");
  std::string out =
      f.ftp.session(LOGIN + "DELE /a.txt\r\nRNFR /a.txt\r\nRNTO /b.txt\r\n");
  CHECK_CONTAINS(out, "250 File deleted");
  CHECK(f.has("exists /a.txt"));
  CHECK(f.has("remove /a.txt"));
  CHECK_CONTAINS(out, "350 ");
  CHECK_CONTAINS(out, "250 Rename successful");
  CHECK(f.has("rename /a.txt /b.txt"));
}

static void testCallbackFSOpenProvidesNoFile() {
  CallbackFixture f;
  std::string out = f.ftp.session(
      LOGIN + "RETR /x.txt\r\nSTOR /y.txt\r\nAPPE /z.txt\r\n");
  CHECK(f.has("open /x.txt r"));
  CHECK(f.has("open /y.txt w"));
  CHECK(f.has("open /z.txt a"));
  CHECK_CONTAINS(out, "550 File not found");
  CHECK_CONTAINS(out, "550 Cannot open file for writing");
}

/// Collects the log output
class StringLogger : public Stream {
 public:
  std::string data;
  size_t write(uint8_t c) override {
    data += (char)c;
    return 1;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

static void testCallbackFSLogsOperations() {
  static StringLogger logger;  // stays registered as log output
  CallbackFS.setCallback(nullptr);
  FTPLogger.setOutput(logger);
  FTPLogger.setLogLevel(LOG_INFO);
  CallbackFS.rename("/a", "/b");
  CallbackFS.open("/c", "w");
  FTPLogger.setLogLevel(LOG_ERROR);
  CHECK_CONTAINS(logger.data, "CallbackFS: rename /a /b");
  CHECK_CONTAINS(logger.data, "CallbackFS: open /c w");
}

static CallbackFileInfo info(const char* name, size_t size, bool isDir) {
  CallbackFileInfo result;
  result.name = name;
  result.size = size;
  result.isDirectory = isDir;
  return result;
}

static void testCallbackFSList() {
  CallbackFixture f;
  f.dirs["/dir"] = {info("a.txt", 3, false), info("sub", 0, true)};
  auto data = f.ftp.addDataClient();
  std::string out = f.ftp.session(LOGIN + "PASV\r\nLIST /dir\r\n");
  CHECK_CONTAINS(out, "150 Here comes the directory listing");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(data->output ==
        "-rw-r--r-- 1 owner group 3 Jan  1 00:00 a.txt\r\n"
        "drwxr-xr-x 1 owner group 0 Jan  1 00:00 sub\r\n");
  CHECK(f.has("open /dir r"));
}

static void testCallbackFSNlstOfCurrentDir() {
  CallbackFixture f;
  f.dirs["/"] = {info("x.txt", 1, false), info("y", 0, true)};
  auto data = f.ftp.addDataClient();
  std::string out = f.ftp.session(LOGIN + "PASV\r\nNLST\r\n");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(data->output == "x.txt\r\ny\r\n");
}

static void testCallbackFSListMissing() {
  CallbackFixture f;
  f.dirs["/dir"] = {};
  std::string out = f.ftp.session(LOGIN + "PASV\r\nLIST /nope\r\n");
  CHECK_CONTAINS(out, "550 No such file or directory");
}

static void testCallbackFSCwd() {
  CallbackFixture f;
  f.dirs["/"] = {info("dir", 0, true)};
  f.dirs["/dir"] = {};
  std::string out = f.ftp.session(
      LOGIN + "CWD /dir\r\nPWD\r\nCDUP\r\nPWD\r\nCWD /nope\r\n");
  CHECK_CONTAINS(out, "250 Directory changed to /dir");
  CHECK_CONTAINS(out, "257 \"/dir\" is current directory");
  CHECK_CONTAINS(out, "257 \"/\" is current directory");
  CHECK_CONTAINS(out, "550 Not a directory");
}

static void testCallbackFSRmd() {
  CallbackFixture f;
  f.dirs["/dir"] = {};
  std::string out = f.ftp.session(LOGIN + "RMD /dir\r\nRMD /nope\r\n");
  CHECK_CONTAINS(out, "250 Directory removed");
  CHECK(f.has("rmdir /dir"));
  CHECK_CONTAINS(out, "550 Not a directory");
  CHECK(!f.has("rmdir /nope"));
}

static void testCallbackFSRetrOfDirectory() {
  CallbackFixture f;
  f.dirs["/dir"] = {};
  std::string out = f.ftp.session(LOGIN + "RETR /dir\r\nSIZE /dir\r\n");
  CHECK(count(out, "550 File not found") == 2);
}

static void testCallbackFSGet() {
  CallbackFixture f;
  f.files["/data.txt"] = "hello callback";
  auto data = f.ftp.addDataClient();
  std::string out = f.ftp.session(
      LOGIN + "SIZE /data.txt\r\nPASV\r\nRETR /data.txt\r\n");
  CHECK_CONTAINS(out, "213 14");
  CHECK_CONTAINS(out, "150 Opening data connection");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(data->output == "hello callback");
  CHECK(f.has("open /data.txt r"));
  CHECK(f.has("close /data.txt r"));
}

static void testCallbackFSGetLargeFile() {
  CallbackFixture f;
  f.files["/large.bin"] = binaryContent(5000);
  auto data = f.ftp.addDataClient();
  f.ftp.session(LOGIN + "PASV\r\nRETR /large.bin\r\n");
  CHECK(data->output == f.files["/large.bin"]);
}

static void testCallbackFSGetMissing() {
  CallbackFixture f;
  std::string out = f.ftp.session(LOGIN + "PASV\r\nRETR /missing.txt\r\n");
  CHECK_CONTAINS(out, "550 File not found");
}

static void testCallbackFSListOfFile() {
  CallbackFixture f;
  f.files["/data.txt"] = "hello callback";
  auto data = f.ftp.addDataClient();
  f.ftp.session(LOGIN + "PASV\r\nLIST /data.txt\r\n");
  CHECK(data->output == "-rw-r--r-- 1 owner group 14 Jan  1 00:00 data.txt\r\n");
}

static void testCallbackFSPut() {
  CallbackFixture f;
  std::string content = binaryContent(3000);
  auto data = f.ftp.addDataClient(content);
  std::string out = f.ftp.session(LOGIN + "PASV\r\nSTOR /up.bin\r\n");
  CHECK_CONTAINS(out, "150 Ready to receive data");
  CHECK_CONTAINS(out, "226 Transfer complete");
  CHECK(f.uploads["/up.bin"] == content);
  CHECK(f.has("open /up.bin w"));
  CHECK(f.has("close /up.bin w"));
}

static void testCallbackFSAppend() {
  CallbackFixture f;
  f.uploads["/up.txt"] = "Hello ";
  auto data = f.ftp.addDataClient("World");
  f.ftp.session(LOGIN + "PASV\r\nAPPE /up.txt\r\n");
  CHECK(f.uploads["/up.txt"] == "Hello World");
  CHECK(f.has("close /up.txt a"));
  auto data2 = f.ftp.addDataClient("New");
  f.ftp.session(LOGIN + "PASV\r\nSTOR /up.txt\r\n");
  CHECK(f.uploads["/up.txt"] == "New");
}

static void testCallbackFSPutRejected() {
  CallbackFixture f;
  std::string out = f.ftp.session(LOGIN + "PASV\r\nSTOR /readonly.txt\r\n");
  CHECK_CONTAINS(out, "550 Cannot open file for writing");
  CHECK(f.uploads.count("/readonly.txt") == 0 ||
        f.uploads["/readonly.txt"].empty());
}

static void testCallbackFSWithoutCallback() {
  CallbackFS.setCallback(nullptr);
  CHECK(!CallbackFS.exists("/x"));
  CHECK(!CallbackFS.mkdir("/x"));
  CHECK(!CallbackFS.open("/x"));
}

int main() {
  root = fs_std::temp_directory_path() /
         ("tinyftp-test-" + std::to_string(getpid()));
  fs_std::create_directories(root);

  RUN(testNoClientDoesNothing);
  RUN(testWelcomeAndQuit);
  RUN(testLoginOk);
  RUN(testLoginWrongPasswordDisconnects);
  RUN(testCommandsRequireLogin);
  RUN(testHelpWithoutLogin);
  RUN(testLoginIsPerConnection);
  RUN(testQuitLogsOut);
  RUN(testNoPasswordOpenServer);
  RUN(testNoPasswordAcceptsAnyPassword);
  RUN(testWrongUserDisconnects);
  RUN(testPassWithoutUser);
  RUN(testMultipleUsers);
  RUN(testAddUserUpdatesPassword);
  RUN(testUserRestartsLogin);

  RUN(testSimpleReplies);
  RUN(testCommandsAreCaseInsensitive);
  RUN(testCommandNeedsExactMatch);
  RUN(testUnknownCommand);
  RUN(testNoStorage);

  RUN(testCwdAbsoluteRelativeAndUp);
  RUN(testCwdUpStopsAtRoot);
  RUN(testCwdInvalid);
  RUN(testCurrentDirIsPerConnection);

  RUN(testPasvUsesLocalIP);
  RUN(testPasvUsesConfiguredIP);
  RUN(testEpsv);
  RUN(testTransferWithoutPasvFails);
  RUN(testDataConnectionTimeout);
  RUN(testPasvIsUsedOnlyOnce);
  RUN(testList);
  RUN(testListWithPathAndOptions);
  RUN(testListMissing);
  RUN(testNlst);
  RUN(testRetr);
  RUN(testRetrLargeBinaryFile);
  RUN(testRetrRelativeToCurrentDir);
  RUN(testRetrMissing);
  RUN(testStor);
  RUN(testStorRelativeToCurrentDir);
  RUN(testStorInvalidPath);
  RUN(testPortActiveMode);
  RUN(testPortConnectFails);
  RUN(testPortInvalid);

  RUN(testMkdAndRmd);
  RUN(testRmdInvalid);
  RUN(testDele);
  RUN(testSize);
  RUN(testFeatAndOpts);
  RUN(testFileCommandsRequireLogin);
  RUN(testRenameNotSupported);
  RUN(testRename);
  RUN(testRenameErrors);

  RUN(testCustomCommand);
  RUN(testCustomCommandRequiresLogin);
  RUN(testCustomCommandCanDisconnect);

  RUN(testBinAscAndOptsUtf8);
  RUN(testAppe);
  RUN(testStorReplacesExistingFile);
  RUN(testAborDuringTransfer);
  RUN(testAborWithoutTransfer);
  RUN(testCommandsWaitForTransfer);
  RUN(testMultipleConnections);
  RUN(testTooManyConnections);
  RUN(testRmdResetsCurrentDirOfOtherConnections);

  RUN(testCallbackFSIsSingleInstance);
  RUN(testCallbackFSMkd);
  RUN(testCallbackFSDeleteAndRename);
  RUN(testCallbackFSOpenProvidesNoFile);
  RUN(testCallbackFSLogsOperations);
  RUN(testCallbackFSList);
  RUN(testCallbackFSNlstOfCurrentDir);
  RUN(testCallbackFSListMissing);
  RUN(testCallbackFSCwd);
  RUN(testCallbackFSRmd);
  RUN(testCallbackFSRetrOfDirectory);
  RUN(testCallbackFSGet);
  RUN(testCallbackFSGetLargeFile);
  RUN(testCallbackFSGetMissing);
  RUN(testCallbackFSListOfFile);
  RUN(testCallbackFSPut);
  RUN(testCallbackFSAppend);
  RUN(testCallbackFSPutRejected);
  RUN(testCallbackFSWithoutCallback);

  fs_std::remove_all(root);

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
