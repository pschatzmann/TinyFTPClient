cmake_minimum_required(VERSION 3.20)

# automatic tests which run on the desktop
project(tinyftp-tests)
set (CMAKE_CXX_STANDARD 11)

add_executable (ftp-client-tests test_ftp_client.cpp test_logger_tu.cpp test_namespace_tu.cpp)
target_compile_definitions(ftp-client-tests PUBLIC -DARDUINO -DIS_DESKTOP -DFTP_CONNECT_RETRIES=2)
target_compile_options(ftp-client-tests PRIVATE -Wall)
target_link_libraries(ftp-client-tests arduino_emulator tinyftp)

add_test(NAME ftp-client-tests COMMAND ftp-client-tests)

# server tests: USE_FILESYSTEM enables directory listing in the emulated SD
add_executable (ftp-server-tests test_ftp_server.cpp)
set_target_properties(ftp-server-tests PROPERTIES CXX_STANDARD 17)
target_compile_definitions(ftp-server-tests PRIVATE -DARDUINO -DIS_DESKTOP -DUSE_FILESYSTEM -DNO_GATEWAYIP -DFTP_CONNECT_RETRIES=2)
target_link_libraries(ftp-server-tests arduino_emulator tinyftp)

add_test(NAME ftp-server-tests COMMAND ftp-server-tests)

# end to end tests: FTPClient <-> FTPServer (in a separate thread)
find_package(Threads REQUIRED)
add_executable (ftp-integration-tests test_integration.cpp)
set_target_properties(ftp-integration-tests PROPERTIES CXX_STANDARD 17)
target_compile_definitions(ftp-integration-tests PRIVATE -DARDUINO -DIS_DESKTOP -DUSE_FILESYSTEM -DNO_GATEWAYIP)
target_link_libraries(ftp-integration-tests arduino_emulator tinyftp Threads::Threads)

add_test(NAME ftp-integration-tests COMMAND ftp-integration-tests)
