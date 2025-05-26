cmake_minimum_required(VERSION 3.20)

# set the project name
project(fileinfo)
set (CMAKE_CXX_STANDARD 11)
set (DCMAKE_CXX_FLAGS "-Werror")

include(FetchContent)

# build sketch as executable
set_source_files_properties(fileinfo.ino PROPERTIES LANGUAGE CXX)
add_executable (fileinfo fileinfo.ino)

# set preprocessor defines
target_compile_definitions(arduino_emulator PUBLIC -DDEFINE_MAIN)
target_compile_definitions(fileinfo PUBLIC -DARDUINO -DIS_DESKTOP)

# specify libraries
target_link_libraries(fileinfo arduino_emulator ftp-client)