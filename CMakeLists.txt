cmake_minimum_required(VERSION 3.16)

# set the project name
project(ftp-client)

# lots of warnings and all warnings as errors
# add_compile_options(-Wall -Wextra )

include(FetchContent)

# Build with Linux Arduino Emulator
FetchContent_Declare(arduino_emulator GIT_REPOSITORY "https://github.com/pschatzmann/Arduino-Emulator.git" GIT_TAG main )
FetchContent_GetProperties(arduino_emulator)
if(NOT arduino_emulator_POPULATED)
    FetchContent_Populate(arduino_emulator)
    add_subdirectory(${arduino_emulator_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/emulator)
endif()

# define location for header files
add_library(ftp-client INTERFACE)
target_include_directories(ftp-client INTERFACE src)

add_subdirectory("examples")