# FTP Client

This is a simple but powerful header only C++ FTP client library for Arduino that provides a Stream based API for the remote files. 

We support

- The reading of Remote Files (File Download)
- The writing to Remote Files (File Upload)
- The listing of Remote Files and Directories
- Creation and deletion of remote directories
- Deletion of remote files

## TCP IP

The initialization of TCP/IP is pretty much platform dependent. You just need to provide your platform specific client implementation. 

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
    WiFi.begin("Network name", "wifi password");
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

You open a FTP connection to a remote host by calling the begin() method on a FTPClient object where you pass
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
Instead of reading a block of characters we can request to read a line (which is delimited with LF)

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
the file already exists it will be replaced with the new content if you use the FileMode WRITE.

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

## Directory Operations
The FTPClient supports the following directory operations:

- bool mkdir(char *filepath) 
- bool remove(char *filepath) 
- bool rmdir(char *filepath)
- FTPFileIterator ls(char *directoryPath)

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

See the [Logging](../README.md#logging) chapter of the README.

## Examples

- [client-download](../examples/client-download/client-download.ino)
- [client-upload](../examples/client-upload/client-upload.ino)
- [client-ls](../examples/client-ls/client-ls.ino)
- [client-fileinfo](../examples/client-fileinfo/client-fileinfo.ino)
