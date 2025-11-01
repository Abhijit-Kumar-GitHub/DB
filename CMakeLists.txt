cmake_minimum_required(VERSION 3.31)
project(ArborDB)

set(CMAKE_CXX_STANDARD 20)

add_executable(ArborDB main.cpp
        db.hpp)
