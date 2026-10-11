# 
# You can build the examples of this project on the desktop using the Arduino Emulator 
# with cmake using the following commands:
#
#  mkdir build
#  cd build
#  cmake ..
#  make
#  ctest --output-on-failure
# 

cmake_minimum_required(VERSION 3.16)

# set the project name
project(tinyftp)

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
add_library(tinyftp INTERFACE)
target_include_directories(tinyftp INTERFACE src)
add_library(ftp-client ALIAS tinyftp)

enable_testing()

add_subdirectory("examples")
add_subdirectory("tests")