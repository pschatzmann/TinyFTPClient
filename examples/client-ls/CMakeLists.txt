cmake_minimum_required(VERSION 3.20)

# set the project name
project(client-ls)
set (CMAKE_CXX_STANDARD 11)
set (DCMAKE_CXX_FLAGS "-Werror")

include(FetchContent)


# build sketch as executable
set_source_files_properties(client-ls.ino PROPERTIES LANGUAGE CXX)
add_executable (client-ls client-ls.ino)

# set preprocessor defines
target_compile_definitions(arduino_emulator PUBLIC -DDEFINE_MAIN)
target_compile_definitions(client-ls PUBLIC -DARDUINO -DIS_DESKTOP)

# specify libraries
target_link_libraries(client-ls arduino_emulator ftp-client)