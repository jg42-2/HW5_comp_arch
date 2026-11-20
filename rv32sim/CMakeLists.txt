cmake_minimum_required(VERSION 3.10)
project(rv32sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(rv32sim
  src/main.cpp
  src/cpu.cpp
  src/disasm.cpp
  src/loader.cpp
)
target_compile_options(rv32sim PRIVATE -Wall -Wextra)
if(MINGW)
  target_link_options(rv32sim PRIVATE -static)
endif()
