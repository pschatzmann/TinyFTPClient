# TinyFTP

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

Sometimes you might have the need to write (your sensor) data into a remote file or you might need
more data than you can store on your local Arduino device for your local processing logic . 

If your device is connected to a network this library is coming to your rescue: The FTP protocol 
is one of oldest communication protocols: It is easy to implement and therefore rather efficient and
you can find plenty of free server implementations on all platforms.

This is a simple header only C++ FTP library for Arduino which provides

- an [FTP Client](docs/client.md) (`FTPClient`): read and write remote files with a Stream based API, list remote
  directories and create or delete remote files and directories.
- an [FTP Server](docs/server.md) (`FTPServer`): make the files of your local storage (e.g. SD, SPIFFS, LittleFS) available
  to any regular FTP client.

```C++
#include "TinyFTP.h"     // FTP client and server functionality
```

or if you need only one of them

```C++
#include "FTPClient.h"   // FTP client functionality
#include "FTPServer.h"   // FTP server functionality
```

All classes are in the `tinyftp` namespace: it is imported automatically unless you define `FTP_USE_NAMESPACE` as false.

## Documentation

- [FTP Client](docs/client.md)
- [FTP Server](docs/server.md)
- [FTP over a Serial Connection with TinyPPP](docs/tinyppp.md)
- [Logging](docs/logging.md)
- [Building on the Desktop](docs/desktop.md): examples and automatic tests
- [Class Documentation](https://pschatzmann.github.io/TinyFTP/annotated.html)

## Support

Please read the documentation first and check the issues and discussions before posting any new ones on Github.

Open issues only for bugs and if it is not a bug, use a discussion: Provide enough information about 
- the selected scenario/sketch 
- what exactly you are trying to do
- your hardware
- your software version (from the Boards Manager) and library versions
- what exactly your problem is

to enable others to understand and reproduce your issue.

Finally, __don't__ send me any e-mails or post questions on my personal website! 


## Installation

You can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with
```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyFTP.git
```

I recommend to use git because you can easily update to the latest version just by executing the git pull command in the project folder. 


## Sponsor Me

This software is totally free, but you can make me happy by rewarding me with a treat

- [Buy me a coffee](https://www.buymeacoffee.com/philschatzh)
- [Paypal me](https://paypal.me/pschatzmann?country.x=CH&locale.x=en_US)




