cmake_minimum_required(VERSION 3.10)
project(spdlite_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(example example.cpp)
target_include_directories(example PRIVATE ../include)
