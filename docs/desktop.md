# Building on the Desktop

You can build the examples and the automatic tests on your desktop (Linux, macOS) with CMake. The Arduino API is
provided by the [Arduino Emulator](https://github.com/pschatzmann/Arduino-Emulator), so the same code which runs on
your microcontroller is compiled and executed on your computer.

## Requirements

- CMake (3.20 or newer)
- A C++ compiler with C++17 support (e.g. gcc or clang)
- git and internet access: the Arduino Emulator is downloaded during the first build

## Building

```bash
mkdir build
cd build
cmake ..
make
```

This builds the library examples (e.g. `build/examples/client-ls/client-ls`) and the test programs in `build/tests`.

If you already have a local copy of the Arduino Emulator (e.g. to work offline or to use your own version), you can
use it instead of the download:

```bash
cmake .. -DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=/path/to/Arduino-Emulator
```

## Tests

Run all automatic tests from the build directory with:

```bash
ctest --output-on-failure
```

The tests don't need any network or FTP server: they use in-memory network connections.

| Test | Description |
|---|---|
| `ftp-client-tests` | The FTPClient against a scripted mock FTP server: commands, uploads, downloads, listings, sessions, slow data, aborts, logging |
| `ftp-server-tests` | The FTPServer with scripted clients: all commands, login, data connections, multiple connections, storages (SD, CallbackFS) |
| `ftp-integration-tests` | End to end: the FTPClient accesses the FTPServer, which runs in a separate thread |

You can also run a single test program directly, e.g. `./tests/ftp-server-tests`: it prints each test and the
failed checks. Or select tests with ctest, e.g. `ctest -R server --output-on-failure`.
