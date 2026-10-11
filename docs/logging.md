# Logging

The client and the server log via the `FTPLogger` object, which is inactive until you start it with the output
Stream and the log level:

```C++
    Serial.begin(115200);
    FTPLogger.begin(Serial, LOG_INFO);
```

Supported log levels are `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN` and `LOG_ERROR` (default): only messages with the
indicated level or above are written. You can change the level later with `FTPLogger.setLogLevel(LOG_DEBUG);`.

Each line contains the level, the module and the message, e.g.

```
FTP INFO - FTPClient: open: /test.txt
FTP ERROR - FTPBasicAPI::checkResult: RETR /missing.txt
```
