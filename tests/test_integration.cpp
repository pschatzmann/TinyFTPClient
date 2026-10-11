/**
 * End to end tests: the FTPClient accesses the FTPServer, which runs in a
 * separate thread, via the in-memory LoopNetwork.
 */
#include <Arduino.h>
#include <SD.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "LoopNetwork.h"
#include "TinyFTP.h"

namespace fs_std = std::filesystem;

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

#define RUN(test)                                      \
  do {                                                 \
    unsigned long start = millis();                    \
    test();                                            \
    printf("%s (%lu ms)\n", #test, millis() - start);  \
  } while (0)

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
  bool rename(const char* from, const char* to) {
    return std::rename(from, to) == 0;
  }
};
static ModeSdFat storage;

static const uint16_t PORT = 2121;
static const IPAddress SERVER(127, 0, 0, 1);
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

static std::string binaryContent(int size) {
  std::string content;
  for (int j = 0; j < size; j++) content += (char)(j % 251);
  return content;
}

static std::string readAll(FTPFile& file) {
  std::string result;
  uint8_t buffer[100];
  while (file.available() > 0) {
    size_t len = file.readBytes(buffer, sizeof(buffer));
    result.append((char*)buffer, len);
  }
  return result;
}

void testUploadAndDownload() {
  FTPClient<LoopClient> client(PORT);
  CHECK(client.begin(SERVER, "user", "pwd"));
  std::string content = binaryContent(20000);
  FTPFile out = client.open(path("updown.bin").c_str(), WRITE_MODE);
  CHECK(out);
  CHECK(out.write((const uint8_t*)content.data(), content.size()) ==
        content.size());
  out.close();
  CHECK(readFile("updown.bin") == content);

  FTPFile in = client.open(path("updown.bin").c_str());
  CHECK(readAll(in) == content);
  in.close();
  client.end();
}

void testAppend() {
  writeFile("append.txt", "Hello ");
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open(path("append.txt").c_str(), WRITE_APPEND_MODE);
  file.print("World");
  file.close();
  CHECK(readFile("append.txt") == "Hello World");
  client.end();
}

void testCopyWithTwoOpenFiles() {
  // the two transfers use two server connections at the same time
  std::string content = binaryContent(50000);
  writeFile("src.bin", content);
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "pwd");
  FTPFile src = client.open(path("src.bin").c_str());
  FTPFile dst = client.open(path("dst.bin").c_str(), WRITE_MODE);
  CHECK(client.count() == 2);
  uint8_t buffer[256];
  while (src.available() > 0) {
    size_t len = src.readBytes(buffer, sizeof(buffer));
    dst.write(buffer, len);
  }
  src.close();
  dst.close();
  CHECK(readFile("dst.bin") == content);
  client.end();
}

void testDirectoryOperations() {
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "pwd");
  CHECK(client.mkdir(path("dir").c_str()));
  CHECK(fs_std::is_directory(path("dir")));
  writeFile("dir/a.txt", "a");
  writeFile("dir/b.txt", "bb");

  std::string names;
  for (auto file : client.ls(path("dir").c_str())) {
    names += std::string(file.name()) + ";";
  }
  CHECK(names.find("a.txt;") != std::string::npos);
  CHECK(names.find("b.txt;") != std::string::npos);

  CHECK(client.remove(path("dir/a.txt").c_str()));
  CHECK(client.remove(path("dir/b.txt").c_str()));
  CHECK(!fs_std::exists(path("dir/a.txt")));
  CHECK(client.rmdir(path("dir").c_str()));
  CHECK(!fs_std::exists(path("dir")));
  client.end();
}

void testFileInfoAndReuse() {
  writeFile("info.txt", "12345");
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open(path("info.txt").c_str());
  CHECK(file.size() == 5);
  CHECK(!file.isDirectory());
  file.close();  // the data connection was opened but not used
  // the next transfer on the same session must not use the old connection
  FTPFile again = client.open(path("info.txt").c_str());
  CHECK(readAll(again) == "12345");
  again.close();
  CHECK(client.count() == 1);
  client.end();
}

void testTypes() {
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "pwd");
  CHECK(client.binary());  // BIN
  CHECK(client.ascii());   // ASC
  client.setUseTypeCommand(true);
  CHECK(client.binary());  // TYPE I
  CHECK(client.ascii());   // TYPE A
  client.end();
}

void testAbort() {
  writeFile("abort.bin", binaryContent(200000));
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "pwd");
  FTPFile file = client.open(path("abort.bin").c_str());
  CHECK(file.available() > 0);
  CHECK(file.read() >= 0);
  CHECK(client.abort(READ_OP));
  CHECK(client.count(NOP) == 1);
  // no desync after the abort
  CHECK(client.mkdir(path("after-abort").c_str()));
  CHECK(client.rmdir(path("after-abort").c_str()));
  client.end();
}

void testTwoClients() {
  std::string content = binaryContent(30000);
  writeFile("two.bin", content);
  FTPClient<LoopClient> client1(PORT);
  FTPClient<LoopClient> client2(PORT);
  client1.begin(SERVER, "user", "pwd");
  client2.begin(SERVER, "user", "pwd");
  FTPFile in = client1.open(path("two.bin").c_str());
  FTPFile out = client2.open(path("two-copy.bin").c_str(), WRITE_MODE);
  uint8_t buffer[100];
  while (in.available() > 0) {
    size_t len = in.readBytes(buffer, sizeof(buffer));
    out.write(buffer, len);
  }
  in.close();
  out.close();
  CHECK(readFile("two-copy.bin") == content);
  client1.end();
  client2.end();
}

void testWrongPassword() {
  FTPClient<LoopClient> client(PORT);
  client.begin(SERVER, "user", "wrong");
  CHECK(!client.mkdir(path("x").c_str()));
  CHECK(client.count() == 0);
}

static const uint16_t CALLBACK_PORT = 2122;

void testCallbackFSGetPutAndList() {
  std::string content = binaryContent(20000);
  std::string uploaded;
  CallbackFS.setReadCallback(
      [&](const char* path, size_t pos, uint8_t* data, size_t len) {
        if (strcmp(path, "/sensor.bin") != 0) return -1;
        if (data == nullptr) return (int)content.size();
        if (pos >= content.size()) return 0;
        size_t n = std::min(len, content.size() - pos);
        memcpy(data, content.data() + pos, n);
        return (int)n;
      });
  CallbackFS.setWriteCallback(
      [&](const char* path, const uint8_t* data, size_t len) {
        if (data != nullptr) uploaded.append((const char*)data, len);
        return true;
      });
  CallbackFS.setListCallback(
      [&](const char* path, std::vector<CallbackFileInfo>& entries) {
        if (strcmp(path, "/") != 0) return false;
        CallbackFileInfo entry;
        entry.name = "sensor.bin";
        entry.size = content.size();
        entries.push_back(entry);
        return true;
      });

  FTPClient<LoopClient> client(CALLBACK_PORT);
  client.begin(SERVER, "user", "pwd");
  std::string names;
  for (auto file : client.ls("/")) names += std::string(file.name()) + ";";
  CHECK(names == "sensor.bin;");

  FTPFile in = client.open("/sensor.bin");
  CHECK(in.size() == content.size());
  CHECK(readAll(in) == content);
  in.close();

  FTPFile out = client.open("/upload.bin", WRITE_MODE);
  out.write((const uint8_t*)content.data(), content.size());
  out.close();
  CHECK(uploaded == content);
  client.end();

  CallbackFS.setReadCallback(nullptr);
  CallbackFS.setWriteCallback(nullptr);
  CallbackFS.setListCallback(nullptr);
}

int main() {
  root = fs_std::temp_directory_path() /
         ("tinyftp-integration-" + std::to_string(getpid()));
  fs_std::create_directories(root);

  FTPServer<LoopServer, ModeSdFat> server(PORT, &storage, 50000);
  server.addUser("user", "pwd");
  server.begin();
  FTPServer<LoopServer, CallbackFSClass> callbackServer(CALLBACK_PORT,
                                                        &CallbackFS, 51000);
  callbackServer.addUser("user", "pwd");
  callbackServer.begin();
  std::atomic<bool> running(true);
  std::thread serverThread([&]() {
    while (running) {
      server.doLoop();
      callbackServer.doLoop();
      delay(1);
    }
  });

  RUN(testUploadAndDownload);
  RUN(testAppend);
  RUN(testCopyWithTwoOpenFiles);
  RUN(testDirectoryOperations);
  RUN(testFileInfoAndReuse);
  RUN(testTypes);
  RUN(testAbort);
  RUN(testTwoClients);
  RUN(testWrongPassword);
  RUN(testCallbackFSGetPutAndList);

  running = false;
  serverThread.join();
  fs_std::remove_all(root);

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
