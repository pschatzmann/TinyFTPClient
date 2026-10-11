# FTP Server

A simple, extensible header-only C++ FTP server for Arduino: `FTPServer<TServer, TStorage>` serves the files of a storage
(e.g. SD, SPIFFS, LittleFS) to any regular FTP client.

## Features

- Supports common FTP commands: USER, PASS, LIST, NLST, PWD, CWD (CD), CDUP, RETR, STOR, APPE, ABOR, DELE, MKD, RMD,
  RNFR, RNTO, SIZE, SYST, TYPE, BIN, ASC, NOOP, PASV, EPSV, PORT, FEAT, OPTS, QUIT, HELP: so all commands which are
  used by the FTPClient of this library
- Multiple clients can be connected at the same time (`FTP_SERVER_MAX_SESSIONS`, default 4) and the transfers of
  the connections run in parallel
- Standard FTP data connections in passive (PASV/EPSV) and active (PORT) mode, so regular FTP clients work
- Multiple users with `addUser(user, password)`; without any user the server is open to everybody
- Pluggable server and storage backends (e.g., WiFiServer, SD, SD_MMC, SPIFFS, LittleFS)
- Easy to extend with custom command handlers

## TCP IP

The initialization of TCP/IP is pretty much platform dependent. You just need to provide your platform specific
server implementation as first template parameter: the server class must provide an `accept()` method which returns
the connected client.

### ESP32

```C++
    #include <WiFi.h>
    #include <SD.h>
    #include "FTPServer.h"

    FTPServer<WiFiServer, SDClass> ftpServer(21, &SD);
```

### ESP8266

```C++
    #include <ESP8266WiFi.h>
    #include <SD.h>
    #include "FTPServer.h"

    FTPServer<WiFiServer, SDClass> ftpServer(21, &SD);
```

### RP2040 W

```C++
    #include <WiFi.h>
    #include <SD.h>
    #include "FTPServer.h"

    FTPServer<WiFiServer, SDClass> ftpServer(21, &SD);
```

### Ethernet

```C++
    #include <Ethernet.h>
    #include <SD.h>
    #include "FTPServer.h"

    FTPServer<EthernetServer, SDClass> ftpServer(21, &SD);
```

### Other Platforms

I suggest that you have a look at the network specific examples provided by Arduino, in order to find what includes
and server class you need to use on your platform.

## Storage

The second template parameter defines the storage which provides the files. It needs to support
`open(path, mode)` (with a mode string like "r" or with the SD FILE_READ/FILE_WRITE modes), `exists()`, `remove()`,
`mkdir()` and `rmdir()`. If the storage also provides `rename()`, the RNFR/RNTO commands are supported as well.
Please include the header of your storage (e.g. SD.h) before FTPServer.h.

### SD

```C++
    FTPServer<WiFiServer, SDClass> ftpServer(21, &SD);

    void setup() {
      SD.begin(chipSelectPin);
      ...
    }
```

### LittleFS, SPIFFS or SD_MMC

On the ESP32, ESP8266 and RP2040 the file systems are based on `fs::FS`:

```C++
    #include <WiFi.h>
    #include <LittleFS.h>
    #include "FTPServer.h"

    FTPServer<WiFiServer, fs::FS> ftpServer(21, &LittleFS);

    void setup() {
      LittleFS.begin();
      ...
    }
```

On the ESP32 you can use the SD card with the SD_MMC interface in the same way:

```C++
    #include <WiFi.h>
    #include <SD_MMC.h>
    #include "FTPServer.h"

    FTPServer<WiFiServer, fs::FS> ftpServer(21, &SD_MMC);

    void setup() {
      SD_MMC.begin();
      ...
    }
```

### TinyFATFS

[TinyFATFS](https://github.com/pschatzmann/TinyFATFS) provides the FAT file system with the Arduino SD API and
flexible drivers (e.g. SD card via SPI, RAM disk). Use `fatfs::SDClass` with its `SD` object as storage:

```C++
    #include <WiFi.h>
    #include <SPI.h>
    #include "fatfs.h"
    #include "FTPServer.h"

    const int chipSelectPin = 5;
    FTPServer<WiFiServer, fatfs::SDClass> ftpServer(21, &SD);

    void setup() {
      SPI.begin();
      while (!SD.begin(chipSelectPin, SPI)) {
        delay(1000);
      }
      ...
    }
```

Instead of the SPI driver you can e.g. use a RAM disk with `RamIO drive{200, 512}; SD.begin(drive);`.
TinyFATFS does not provide `rename()`, so RNFR/RNTO reply with 502.

### CallbackFS

The `CallbackFS` object does not store any files: it logs each file operation and reports it to your callback,
whose result is used for `exists()`, `remove()`, `mkdir()`, `rmdir()` and `rename()`. For `open()` the callback gets
the mode ("r", "w" or "a") as parameter:

```C++
    FTPServer<WiFiServer, CallbackFSClass> ftpServer(21, &CallbackFS);

    void setup() {
      CallbackFS.setCallback([](CallbackFSClass::Operation op, const char* path, const char* param) {
        Serial.println(CallbackFSClass::toString(op));
        return true;
      });
      ...
    }
```

With the optional list callback you define the directories: return true if the path is a directory and add its
entries. This makes LIST, NLST, CWD, CDUP and RMD work:

```C++
    CallbackFS.setListCallback([](const char* path, std::vector<CallbackFileInfo>& entries) {
      if (strcmp(path, "/") != 0) return false;  // only the root directory exists
      CallbackFileInfo entry;
      entry.name = "sensor.csv";
      entry.size = 100;
      entry.isDirectory = false;
      entries.push_back(entry);
      return true;
    });
```

With the optional read callback you provide the content of the files (RETR / get, SIZE): it copies up to `len`
bytes starting at `pos` and returns the number of bytes (0 at the end of the file). If `data` is `nullptr` it returns
the file size (or 0 if unknown) or -1 if the file does not exist:

```C++
    const char* csv = "time,value\n1,42\n";

    CallbackFS.setReadCallback([](const char* path, size_t pos, uint8_t* data, size_t len) {
      if (strcmp(path, "/sensor.csv") != 0) return -1;  // file does not exist
      size_t size = strlen(csv);
      if (data == nullptr) return (int)size;            // file size
      if (pos >= size) return 0;                        // end of file
      size_t n = min(len, size - pos);
      memcpy(data, csv + pos, n);
      return (int)n;
    });
```

With the optional write callback you receive the uploaded files (STOR, APPE / put). It is called with
`data == nullptr` when the file is opened: return false to reject the upload. The operation callback is called with
`Open` and the mode ("w" replaces, "a" appends) before and with `Close` after the upload:

```C++
    CallbackFS.setWriteCallback([](const char* path, const uint8_t* data, size_t len) {
      if (data == nullptr) return strcmp(path, "/upload.txt") == 0;  // accept only this file
      Serial.write(data, len);
      return true;
    });
```

Without read or write callback, RETR, STOR and APPE are reported to the operation callback but fail with a 550 reply.

## Starting the Server

Connect to the network, initialize the storage, define the users and start the server. Call `doLoop()` from your
loop: it does not block, but accepts new clients and processes one step (a command or a part of a transfer) of each
connected client. So keep your loop short and call `doLoop()` as often as possible.

```C++
#include <WiFi.h>
#include <SD.h>
#include "FTPServer.h"

const char* ssid = "SSID";
const char* password = "PASSWORD";
const int chipSelectPin = 5;
FTPServer<WiFiServer, SDClass> ftpServer(21, &SD);

void setup() {
  Serial.begin(115200);

  // login to wireless network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println();
  Serial.print("Started with address ");
  Serial.println(WiFi.localIP());

  // setup storage
  while (!SD.begin(chipSelectPin)) {
    Serial.println("SD initialization failed");
    delay(1000);
  }

  ftpServer.addUser("your-ftp-user", "your-ftp-password");
  ftpServer.begin();
}

void loop() {
  ftpServer.doLoop();
}
```

## Data Connections

File transfers and listings use a separate data connection:

- Passive mode (PASV/EPSV): each connection listens on its own data port: the data port + the index of the connection.
  So with the default data port 50009 and 4 connections the ports 50009 - 50012 are used. The data port can be set in
  the constructor: `FTPServer<WiFiServer, SDClass> ftpServer(21, &SD, 50009);`
- The IP reported by PASV is taken from the control connection when the client type provides `localIP()`. Otherwise, set it with `ftpServer.setPassiveIP(WiFi.localIP());`. EPSV does not need an IP.
- Active mode (PORT): the server connects to the client.
- `setDataTimeout(ms)` defines how long the server waits for a data connection.
- During a transfer only ABOR is processed immediately: the other commands of the connection are processed when the
  transfer has ended.

## Configuration

The following defines can be set before including FTPServer.h:

| Define | Default | Description |
|---|---|---|
| `FTP_SERVER_MAX_SESSIONS` | 4 | Max number of clients which can be connected at the same time |
| `FTP_SERVER_BUFFER_SIZE` | 512 | Transfer buffer size of each connection |
| `FTP_SERVER_MAX_LINE` | 512 | Max length of a command line |
| `FTP_DATA_TIMEOUT` | 5000 | Timeout in ms for opening and idling on data connections |

## Extending

Add custom commands using `addCommand(name, handler, needsAuth = true)` or override handler methods.

## Examples

- [server-simple](../examples/server-simple/server-simple.ino)
