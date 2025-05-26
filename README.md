# TinyFTPClient

Sometimes you might have the need to write (your sensor) data into a remote file or you might need
more data than you can store on your local Arduino device for your local processing logic . 

If your device is connected to the Internet this library is coming to your rescue: The FTP protocol 
is one of oldest communication protocols: It is easy to implement and therefore rather efficient and
you can find plenty of free server implementations on all platforms.

This is a simple but powerful header only C++ FTP client library for Arduino that provides a Stream based API for the remote files. 

We support

- The reading of Remote Files (File Download)
- The writing to Remote Files (File Upload)
- The listing of Remote Files and Directories
- Creation and deletion of remote directories
- Deletion of remote files

## TCP IP

The initialization of TCP/IP is pretty much platform dependent. You just need to provide your platform specific client implemenation. 

To set up the FTPClient you need to provide a network client as template class parameter: 

### ESP32

```C++
    #include "WiFi.h"
    #include "FTPClient.h"

    FTPClient<WiFiClient> client;
```

### ESP8266

```C++
    #include <ESP8266WiFi.h>
    #include "FTPClient.h"

    FTPClient<WiFiClient> client;
```

### RP2040 W

```C++
    #include <WiFi.h>
    #include "FTPClient.h"

    FTPClient<WiFiClient> client;
```


### Arduino Uno WiFi Rev.2, Arduino Nano 33 IoT etc

```C++
    #include <WiFiNINA.h>
    #include "FTPClient.h"

    FTPClient<WiFiClient> client;
```

### Ethernet Shield

```C++
    #include <Ethernet.h>
    #include "FTPClient.h"

    FTPClient<EthernetClient> client;
```

### Other Platforms

The initialization of TCP/IP is pretty much platform dependent. I suggest that you have a look at the network specific examples provided by Arduino, in order to find what includes and classes you need to use on your platform.


### Connecting

This is platform specific as well. On a ESP32 or ESP8266 e.g. the following login into the WiFi network is working:

```C++
    Serial.begin(115200);
    // connect to WIFI
    WiFi.begin("Network name, "wifi password");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    IPAddress localAddress = WiFi.localIP();
    Serial.println();
    Serial.print("Started with address ");
    Serial.println(localAddress.toString());

```

## File Download - Reading Remote Files

You open a FTP connection to a remote host by calling the begin() method on a ArduinoFTPClient object where you pass
- the IP address of your server
- the userid to log into your server and 
- the related password  

The open method provides a FTPFile for the indicated file name. The FTPFile is just a Stream so
you have all reading and writing functionality available that you already know e.g. from Serial.

```C++
    FTPClient<WiFiClient> client;

    client.begin(IPAddress(192,168,1,10), "user", "password");
    FTPFile file = client.open("/test.txt");
    byte buffer[100];
    while (file.available()>0){
        int len = file.read(buffer, 100);
        Serial.write(buffer, len);
    }
    file.close();
    client.end();
```
After the processing you need to close the file with close() and the client with end() to release 
the resources.

## File Download - Line Based
Instead of reading a bock of characters we can request to read a line (which is delimited with LF)

```C++
    FTPClient<WiFiClient> client;

    client.begin(IPAddress(192,168,1,10), "user", "password");
    FTPFile file = client.open("/test.txt");
    char buffer[100];
    while (file.available()>0){
        file.readln(buffer, 100);
        Serial.println(buffer);
    }
    file.close();
    client.end();
```

## File Upload - Writing to Remote Files
You can write to a file on a remote system by using the regular Stream write() or print() methods. If
the file alreay exists it will be replaced with the new content if you use the FileMode WRITE.

```C++
    FTPClient<WiFiClient> client;

    client.begin(IPAddress(192,168,1,10), "user", "password");
    FTPFile file = client.open("/test.txt", WRITE_MODE);
    char buffer[100];
    for (int j=0;j<100;j++){
        sprintf(buffer, "test %d", j);
        file.println(buffer);
    }
    file.close();
    client.end();
```

## File Upload - Appending
You can also append information to an existing remote file by indicating the FileMode WRITE_APPEND

```C++
    FTPClient<WiFiClient> client;

    client.begin(IPAddress(192,168,1,10), "user", "password");
    FTPFile file = client.open("/test.txt", WRITE_APPEND_MODE);
    char buffer[100];
    for (int j=0;j<100;j++){
        sprintf(buffer, "test %d", j);
        file.println(buffer);
    }
    file.close();
    client.end();
```

##  Directory operatrions
The ArduinoFTPClient supports the following directory operations:

- bool mkdir(char *filepath) 
- bool remove(char *filepath) 
- bool rmdir(char *filepath)
- FileIterator ls(char*directoryPath)

### List Directory
The files of a directory are listed with the help of an Iterator. 

```C++
    FTPClient<WiFiClient> client;
    
    client.begin(IPAddress(192,168,1,10), "user", "password");

    // list files
    for (auto file : client.ls("/"))  {
        Serial.println(file.name());
    }
```

## Logging
You can activate the logging by defining the Stream which should be used for logging and setting the log level. 
Supported log levels are LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR

```C++
    Serial.begin(9600);
    FTPLogger::setOutput(Serial);
    FTPLogger::setLogLevel(LOG_DEBUG);
```


## Documentation

- [Class Documentation](https://pschatzmann.github.io/TinyFTPClient/docs/html/annotated.html)

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
git clone https://github.com/pschatzmann/TinyFTPClient.git
```

I recommend to use git because you can easily update to the latest version just by executing the git pull command in the project folder. 


## Sponsor Me

This software is totally free, but you can make me happy by rewarding me with a treat

- [Buy me a coffee](https://www.buymeacoffee.com/philschatzh)
- [Paypal me](https://paypal.me/pschatzmann?country.x=CH&locale.x=en_US)




