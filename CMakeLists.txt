


cmake_minimum_required(VERSION 3.10)
project(webench)

set(CMAKE_CXX_COMPILE "/usr/bin/g++")
set(CMAKE_CXX_STANDARD 14)

add_executable(webbench webbench.cc)
target_compile_options(webbench PRIVATE "-O3")

find_library(boostsystem boost_system)
target_link_libraries(webbench ${boostsystem})