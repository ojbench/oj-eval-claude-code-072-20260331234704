cmake_minimum_required(VERSION 3.10)
project(RISCVSimulator)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall")

add_executable(code simulator.cpp)
