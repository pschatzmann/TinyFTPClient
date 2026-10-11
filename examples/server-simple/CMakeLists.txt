cmake_minimum_required(VERSION 3.20)

# set the project name
project(server-simple)
set (CMAKE_CXX_STANDARD 11)
set (DCMAKE_CXX_FLAGS "-Werror")

# build sketch as executable
set_source_files_properties(server-simple.ino PROPERTIES LANGUAGE CXX)
add_executable (server-simple server-simple.ino)

# set preprocessor defines
target_compile_definitions(arduino_emulator PUBLIC -DDEFINE_MAIN)
target_compile_definitions(server-simple PUBLIC -DARDUINO -DIS_DESKTOP -DEXIT_ON_STOP -DUSE_FILESYSTEM -DNO_GATEWAYIP)

# specify libraries
target_link_libraries(server-simple arduino_emulator tinyftp)
