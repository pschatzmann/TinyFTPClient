# FTP over a Serial Connection with TinyPPP

## Networking in TinyFTP

TinyFTP does not contain any network stack: the client and the server are templates which use the network classes
that are provided by your Arduino core or by a network library.

- `FTPClient<ClientType>` uses a `ClientType` that implements the Arduino `Client` interface (`connect()`, `read()`,
  `write()`, `stop()`, `connected()`...), e.g. `WiFiClient` or `EthernetClient`. Each session uses two of them: one
  for the command connection and one for the data connection. The client always uses passive mode: it opens the data
  connection to the port which the server announces in its reply to PASV.
- `FTPServer<TServer, TStorage>` uses a `TServer` that can be constructed with a port and that provides `begin()` and
  `accept()`, e.g. `WiFiServer` or `EthernetServer`. `accept()` must return a `Client`. The server listens on the
  command port (21) and each connection gets its own passive data server on the data port + the index of the
  connection. In active mode (PORT) it connects to the FTP client with the client type which is returned by
  `accept()`. The address which is reported by PASV is the local address of the command connection (if the client type
  provides `localIP()`) or the address which was defined with `setPassiveIP()`.

So TinyFTP works with any network which provides these classes: WiFi, Ethernet and also PPP over a serial line with
[TinyPPP](https://github.com/pschatzmann/TinyPPP).

## TinyPPP

TinyPPP is a header-only library for IP networking with PPP (Point-to-Point Protocol) over any Arduino `Stream`, e.g.
the USB serial port or a hardware UART. It plugs into the network API of the Arduino core and provides `PPPClient` and
`PPPServer` as aliases for the native classes:

| Platform | PPPClient | PPPServer |
|---|---|---|
| ESP32 (arduino-esp32 3.x) | `NetworkClient` | `NetworkServer` |
| RP2040 / RP2350 (arduino-pico) | `WiFiClient` | `WiFiServer` |

So you can use them as template parameters of `FTPClient` and `FTPServer`: your board does not need WiFi or Ethernet.
The computer on the other end of the serial line runs `pppd`, which assigns the IP addresses: in the following
examples the computer gets 192.168.7.1 and the board 192.168.7.2.

## FTP Server over the USB Serial Port

The following sketch serves the files of LittleFS over the USB serial port:

```C++
#include <TinyPPP.h>
#include <LittleFS.h>
#include "FTPServer.h"

TinyPPP ppp;
FTPServer<PPPServer, fs::FS> ftpServer(21, &LittleFS);

void setup() {
  // the USB serial carries PPP: don't print anything else on it!
  Serial.begin(115200);
  ppp.begin(Serial);  // returns at once, the link comes up in the background
  LittleFS.begin();
  ftpServer.addUser("ftp", "ftp");
  ftpServer.begin();
}

void loop() { ftpServer.doLoop(); }
```

On the computer (Linux) start pppd on the serial device of the board:

```bash
sudo pppd /dev/ttyACM0 115200 192.168.7.1:192.168.7.2 noauth local nodetach \
     novj noccp nodefaultroute
```

Then you can use any FTP client, e.g. `ftp 192.168.7.2` or `curl ftp://ftp:ftp@192.168.7.2/`.

## FTP Client over the USB Serial Port

The board can also access an FTP server which runs on the computer (e.g. vsftpd) to upload or download files:

```C++
#include <TinyPPP.h>
#include "FTPClient.h"

TinyPPP ppp;
FTPClient<PPPClient> client;

void setup() {
  // the USB serial carries PPP: don't print anything else on it!
  Serial.begin(115200);
  ppp.begin(Serial);
  ppp.waitConnected(60000);

  // the computer which runs pppd has the address 192.168.7.1
  client.begin(IPAddress(192, 168, 7, 1), "user", "password");
  FTPFile file = client.open("/tmp/sensor.csv", WRITE_MODE);
  file.println("time,value");
  file.print(millis());
  file.println(",42");
  file.close();
  client.end();
}

void loop() {}
```

## Notes

- The serial port which carries PPP can't be used for anything else: don't print on it and don't log to it. If you
  need logging, use another output, e.g. `FTPLogger.begin(Serial1, LOG_INFO);`
- On the ESP32 PPP runs in client mode, so the computer has to assign the IP addresses (e.g.
  `192.168.7.1:192.168.7.2` on the pppd command line).
- On the RP2040 the serial line is polled every 20 ms by default: for more throughput lower the period with
  `ppp.setPollingPeriod(2);`.
- See the [TinyPPP documentation](https://github.com/pschatzmann/TinyPPP) for the setup of pppd on Linux and macOS,
  authentication and internet access via NAT.
