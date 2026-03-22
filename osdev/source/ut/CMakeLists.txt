cmake_minimum_required(VERSION 3.10)
project(ut C)

set(CMAKE_C_STANDARD 11)

include_directories("../misc")

add_executable(ut main.c
        lock_ut.c "../misc/spinlock.c"
        list_ut.c)
