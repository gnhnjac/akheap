cmake_minimum_required(VERSION 3.31)

include_directories(include)
link_directories()

file(GLOB_RECURSE SOURCES main.c src/*.c)
add_executable(heap_test ${SOURCES})