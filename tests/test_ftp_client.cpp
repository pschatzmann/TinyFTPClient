/**
 * Automatic tests for FTPClient which run on the desktop (Arduino Emulator)
 * against the in-memory MockFTPServer.
 */
#include <cstdio>
#include <string>

#include "FTPClient.h"
#include "MockFTPServer.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                   \
  } while (0)

#define RUN(test)                                   \
  do {                                              \
    server().reset();                               \
    unsigned long start = millis();                 \
    test();                                         \
    printf("%s (%lu ms)\n", #test, millis() - start); \
  } while (0)

// implemented in test_logger_tu.cpp
void setLoggerOutputFromOtherTU(Stream &out);
// implemented in test_namespace_tu.cpp
bool qualifiedNamesWork();

/// Stream which collects everything written into a std::string
class StringStream : public Stream {
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

static const IPAddress SERVER(127, 0, 0, 1);

static std::string readAll(FTPFile &file) {
  std::string result;
  uint8_t buffer[16];
  while (file.available() > 0) {
    size_t len = file.readBytes(buffer, sizeof(buffer));
    result.append((char *)buffer, len);
  }
  return result;
}

void testLoginAndDirectoryCommands() {
  FTPClient<MockClient> client;
  CHECK(client.begin(SERVER, "user", "pwd"));
  CHECK(client.count() == 0);  // sessions are opened on demand

  CHECK(client.mkdir("/dir"));
  CHECK(server().dirs.count("/dir") == 1);
  CHECK(server().count("USER user") == 1);
  CHECK(server().count("PASS pwd") == 1);
  CHECK(client.rmdir("/dir"));
  CHECK(!client.rmdir("/dir"));  // 550

  server().files["/a.txt"] = "x";
  CHECK(client.remove("/a.txt"));
  CHECK(server().files.empty());
  CHECK(client.count() == 1);  // the session was reused

  client.end();
  CHECK(server().count("QUIT") == 1);
  CHECK(client.count() == 0);
}

void testNoPasswordRequired() {
  server().user_needs_password = false;
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  CHECK(client.mkdir("/dir"));
  CHECK(server().count("PASS") == 0);
}

void testRejectedLogin() {
  server().reject_user = true;
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  CHECK(!client.mkdir("/dir"));
  CHECK(server().count("MKD") == 0);
  CHECK(client.count() == 0);
}

void testMultiLineReply() {
  server().user_needs_password = false;
  server().multi_line_login = true;
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  // the continuation lines must not be taken as replies to later commands
  CHECK(client.mkdir("/a"));
  CHECK(client.mkdir("/b"));
  CHECK(server().dirs.size() == 2);
}

void testMultiLineWelcome() {
  server().welcome = "220-Hello\r\n220-More\r\n220 Ready\r\n";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  CHECK(client.mkdir("/a"));
}

void testUpload() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/up.txt", WRITE_MODE);
  CHECK(file);
  CHECK(client.count(RESERVED_OP) == 1);
  file.print("hello ");
  file.print("world");
  CHECK(client.count(WRITE_OP) == 1);
  file.close();
  CHECK(server().files["/up.txt"] == "hello world");
  CHECK(client.count(NOP) == 1);

  // append
  FTPFile file2 = client.open("/up.txt", WRITE_APPEND_MODE);
  file2.print("!");
  file2.close();
  CHECK(server().files["/up.txt"] == "hello world!");
  CHECK(client.count() == 1);
}

void testWriteInReadModeFails() {
  server().files["/r.txt"] = "abc";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/r.txt", READ_MODE);
  CHECK(file.write((uint8_t)'x') == 0);
  CHECK(server().count("STOR") == 0);
  file.close();
}

void testDownload() {
  server().files["/down.txt"] = "The quick brown fox jumps over the lazy dog";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/down.txt");
  CHECK(readAll(file) == server().files["/down.txt"]);
  file.close();
  CHECK(client.count(NOP) == 1);

  // the session can be used again after the close
  FTPFile again = client.open("/down.txt");
  CHECK(readAll(again) == server().files["/down.txt"]);
  again.close();
  CHECK(client.count() == 1);
}

void testFileInfoWithoutTransfer() {
  server().files["/info.txt"] = "12345";
  server().dirs.insert("/dir");
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");

  FTPFile file = client.open("/info.txt");
  CHECK(file.size() == 5);
  CHECK(!file.isDirectory());
  file.close();

  FTPFile dir = client.open("/dir");
  CHECK(dir.isDirectory());
  dir.close();

  CHECK(server().count("RETR") == 0);
  CHECK(client.count(NOP) == 1);
}

void testTwoOpenFilesUseSeparateSessions() {
  server().files["/src.txt"] = "copy me";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");

  FTPFile src = client.open("/src.txt");
  FTPFile dst = client.open("/dst.txt", WRITE_MODE);
  CHECK(client.count() == 2);

  uint8_t buffer[4];
  while (src.available() > 0) {
    size_t len = src.readBytes(buffer, sizeof(buffer));
    dst.write(buffer, len);
  }
  src.close();
  dst.close();
  CHECK(server().files["/dst.txt"] == "copy me");
  CHECK(client.count(NOP) == 2);
}

void testMaxSessions() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile files[FTP_MAX_SESSIONS];
  for (int j = 0; j < FTP_MAX_SESSIONS; j++) {
    files[j] = client.open("/f.txt", WRITE_MODE);
    CHECK(files[j]);
  }
  FTPFile extra = client.open("/f.txt", WRITE_MODE);
  CHECK(!extra);
  CHECK(!client.mkdir("/x"));
  for (auto &f : files) f.close();
  CHECK(client.mkdir("/x"));
}

void testLs() {
  server().files["/d/a.txt"] = "a";
  server().files["/d/b.txt"] = "b";
  server().files["/d/c.txt"] = "c";
  server().files["/other.txt"] = "o";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");

  std::string names;
  for (auto file : client.ls("/d")) {
    names += std::string(file.name()) + ";";
    file.close();  // must not end the listing
  }
  CHECK(names == "a.txt;b.txt;c.txt;");
  CHECK(client.count(NOP) == 1);

  // postfix increment
  FTPFileIterator it = client.ls("/d");
  it.begin();
  FTPFileIterator old = it++;
  CHECK(std::string(old.fileName()) == "a.txt");
  CHECK(std::string(it.fileName()) == "b.txt");
  ++it;
  ++it;
  CHECK(it == it.end());
  CHECK(client.count(NOP) == 1);
}

void testAbort() {
  server().files["/big.txt"] = std::string(1000, 'x');
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/big.txt");
  file.read();
  CHECK(client.count(READ_OP) == 1);
  CHECK(client.abort(READ_OP));
  CHECK(server().count("ABOR") == 1);
  CHECK(client.count(NOP) == 1);
  CHECK(!client.abort(READ_OP));  // nothing to abort
  CHECK(client.mkdir("/after-abort"));
}

void testCancelReservedFile() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/x.txt", WRITE_MODE);
  CHECK(file.cancel());
  CHECK(server().count("ABOR") == 0);
  CHECK(client.count(NOP) == 1);
}

void testTypeCommands() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  CHECK(client.binary());
  CHECK(client.ascii());
  CHECK(server().count("BIN") == 1);
  CHECK(server().count("ASC") == 1);

  client.setUseTypeCommand(true);
  CHECK(client.binary());
  CHECK(client.ascii());
  CHECK(server().count("TYPE I") == 1);
  CHECK(server().count("TYPE A") == 1);
}

void testClosedIdleSessionIsReplaced() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  CHECK(client.mkdir("/a"));
  CHECK(server().command_connections == 1);
  server().dropControlConnections();  // e.g. server idle timeout
  CHECK(client.mkdir("/b"));
  CHECK(server().command_connections == 2);
  CHECK(client.count() == 1);
}

void testUnreachableServer() {
  server().refuse_connections = true;
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/x.txt");  // must not crash
  CHECK(!file);
  CHECK(file.available() == 0);
  CHECK(file.read() == -1);
  CHECK(file.write((uint8_t)'x') == 0);
  CHECK(client.count() == 0);
}

void testDefaultObjects() {
  FTPFile file;
  CHECK(!file);
  CHECK(file.available() == 0);
  CHECK(file.size() == 0);
  file.close();

  FTPFileIterator it;
  CHECK(it.begin() == it.end());
}

void testLoggerIsSharedBetweenTranslationUnits() {
  StringStream out;
  setLoggerOutputFromOtherTU(out);
  FTPLogger.setLogLevel(LOG_INFO);
  FTPLogger.writeLog(LOG_INFO, "test", "message");
  CHECK(out.data.find("FTP INFO - test: message") != std::string::npos);
  FTPLogger.setLogLevel(LOG_ERROR);
  FTPLogger.writeLog(LOG_INFO, "test", "hidden");
  CHECK(out.data.find("hidden") == std::string::npos);
  static StringStream sink;
  FTPLogger.setOutput(sink);
}

void testLoggerBegin() {
  static StringStream out;  // stays registered as log output
  FTPLogger.begin(out, LOG_WARN);
  CHECK(FTPLogger.getLogLevel() == LOG_WARN);
  FTPLogger.writeLog(LOG_INFO, "test", "hidden");
  FTPLogger.writeLog(LOG_WARN, "test", "shown");
  CHECK(out.data == "FTP WARN - test: shown\r\n");
  FTPLogger.begin(out);  // default level
  CHECK(FTPLogger.getLogLevel() == LOG_ERROR);
}

void testSlowDownload() {
  // data arrives in small chunks: available() must not report the end early
  server().files["/slow.txt"] = std::string(200, 'a') + "end";
  server().chunk_size = 16;
  server().chunk_delay_ms = 5;
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/slow.txt");
  CHECK(readAll(file) == server().files["/slow.txt"]);
  // at the end of the file available() returns 0 without waiting
  unsigned long start = millis();
  CHECK(file.available() == 0);
  CHECK(millis() - start < 500);
  file.close();
  CHECK(client.count(NOP) == 1);
}

void testSlowLs() {
  server().files["/d/a.txt"] = "a";
  server().files["/d/b.txt"] = "b";
  server().files["/d/c.txt"] = "c";
  server().chunk_size = 3;
  server().chunk_delay_ms = 5;
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  std::string names;
  unsigned long start = millis();
  for (auto file : client.ls("/d")) names += std::string(file.name()) + ";";
  CHECK(names == "a.txt;b.txt;c.txt;");
  // the end of the listing is detected by the closed data connection
  CHECK(millis() - start < 500);
  CHECK(client.count(NOP) == 1);
}

void testReadMissingFile() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/missing.txt");
  unsigned long start = millis();
  CHECK(file.available() == 0);
  CHECK(file.read() == -1);
  CHECK(millis() - start < 500);
  CHECK(client.count(NOP) == 1);  // the session was released
  file.close();
  CHECK(client.mkdir("/after"));
}

void testReadInWriteModeFails() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/w.txt", WRITE_MODE);
  CHECK(file.available() == 0);
  CHECK(file.read() == -1);
  CHECK(server().count("RETR") == 0);
  file.close();
}

void testUnclosedFileReleasesSession() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  {
    FTPFile file = client.open("/unclosed.txt", WRITE_MODE);
    file.print("data");
  }  // destroyed without close
  CHECK(server().files["/unclosed.txt"] == "data");
  CHECK(client.count(NOP) == 1);
  {
    FTPFile file = client.open("/unused.txt");
  }  // never accessed
  CHECK(client.count(NOP) == 1);
  CHECK(client.count() == 1);
}

void testUnusedLsReleasesSession() {
  server().files["/d/a.txt"] = "a";
  server().files["/d/b.txt"] = "b";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  {
    FTPFileIterator it = client.ls("/d");
  }  // never iterated
  CHECK(client.count(NOP) == 1);
  for (auto file : client.ls("/d")) {
    break;  // leave the listing early
  }
  CHECK(client.count(NOP) == 1);
  CHECK(client.mkdir("/after-ls"));
}

static void passByValue(FTPFile file) { CHECK(file); }

void testCopiesShareTheTransfer() {
  server().files["/src.txt"] = "shared";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");

  // a destroyed copy does not end the transfer
  FTPFile file = client.open("/src.txt");
  passByValue(file);
  CHECK(client.count(RESERVED_OP) == 1);
  CHECK(readAll(file) == "shared");

  // closing a copy of an ended transfer does not affect the next transfer of
  // the same session
  FTPFile copy = file;
  file.close();
  FTPFile next = client.open("/next.txt", WRITE_MODE);
  CHECK(client.count() == 1);  // the session was reused
  copy.close();
  CHECK(client.count(RESERVED_OP) == 1);
  next.print("ok");
  next.close();
  CHECK(server().files["/next.txt"] == "ok");
}

void testListedFilesUseTheirOwnSession() {
  server().nlst_full_path = true;
  server().files["/d/a.txt"] = "aaa";
  server().files["/d/b.txt"] = "bbbbb";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  std::string result;
  for (auto file : client.ls("/d")) {
    result += std::string(file.name()) + ":" + std::to_string(file.size()) +
              ":" + readAll(file) + ";";
    file.close();
  }
  CHECK(result == "/d/a.txt:3:aaa;/d/b.txt:5:bbbbb;");
  CHECK(client.count(NOP) == client.count());
}

void testReopen() {
  server().files["/again.txt"] = "again";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/again.txt");
  CHECK(readAll(file) == "again");
  file.close();
  file.reopen();
  CHECK(readAll(file) == "again");
  file.close();
  CHECK(server().count("RETR") == 2);
}

void testAbortDuringWrite() {
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/abort.txt", WRITE_MODE);
  file.print("partial");
  CHECK(client.abort(WRITE_OP));
  CHECK(client.count(NOP) == 1);
  // the replies to the ABOR were consumed: no desync
  CHECK(client.mkdir("/x"));
  CHECK(client.rmdir("/x"));
}

void testFileAfterEnd() {
  server().files["/e.txt"] = "e";
  FTPClient<MockClient> client;
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open("/e.txt");
  client.end();
  CHECK(file.read() == -1);  // the transfer was ended by end()
  file.close();
  client.begin(SERVER, "user", "pwd");
  CHECK(client.mkdir("/x"));
}

void testNamespaces() { CHECK(qualifiedNamesWork()); }

int main() {
  RUN(testLoginAndDirectoryCommands);
  RUN(testNoPasswordRequired);
  RUN(testRejectedLogin);
  RUN(testMultiLineReply);
  RUN(testMultiLineWelcome);
  RUN(testUpload);
  RUN(testWriteInReadModeFails);
  RUN(testDownload);
  RUN(testFileInfoWithoutTransfer);
  RUN(testTwoOpenFilesUseSeparateSessions);
  RUN(testMaxSessions);
  RUN(testLs);
  RUN(testAbort);
  RUN(testCancelReservedFile);
  RUN(testTypeCommands);
  RUN(testClosedIdleSessionIsReplaced);
  RUN(testUnreachableServer);
  RUN(testDefaultObjects);
  RUN(testLoggerIsSharedBetweenTranslationUnits);
  RUN(testLoggerBegin);
  RUN(testSlowDownload);
  RUN(testSlowLs);
  RUN(testReadMissingFile);
  RUN(testReadInWriteModeFails);
  RUN(testUnclosedFileReleasesSession);
  RUN(testUnusedLsReleasesSession);
  RUN(testCopiesShareTheTransfer);
  RUN(testListedFilesUseTheirOwnSession);
  RUN(testReopen);
  RUN(testAbortDuringWrite);
  RUN(testFileAfterEnd);
  RUN(testNamespaces);

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
