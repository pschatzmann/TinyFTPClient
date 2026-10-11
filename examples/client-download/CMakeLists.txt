cmake_minimum_required(VERSION 3.20)

# set the project name
project(download)
set (CMAKE_CXX_STANDARD 11)
set (DCMAKE_CXX_FLAGS "-Werror")

include(FetchContent)

# build sketch as executable
set_source_files_properties(download.ino PROPERTIES LANGUAGE CXX)
add_executable (download download.ino)

# set preprocessor defines
target_compile_definitions(arduino_emulator PUBLIC -DDEFINE_MAIN)
target_compile_definitions(download PUBLIC -DARDUINO -DIS_DESKTOP)

# specify libraries
target_link_libraries(download arduino_emulator ftp-client)