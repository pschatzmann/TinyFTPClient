cmake_minimum_required(VERSION 3.20)

# set the project name
project(ls)
set (CMAKE_CXX_STANDARD 11)
set (DCMAKE_CXX_FLAGS "-Werror")

include(FetchContent)


# build sketch as executable
set_source_files_properties(ls.ino PROPERTIES LANGUAGE CXX)
add_executable (ls ls.ino)

# set preprocessor defines
target_compile_definitions(arduino_emulator PUBLIC -DDEFINE_MAIN)
target_compile_definitions(ls PUBLIC -DARDUINO -DIS_DESKTOP)

# specify libraries
target_link_libraries(ls arduino_emulator ftp-client)