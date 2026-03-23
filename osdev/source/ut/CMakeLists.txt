cmake_minimum_required(VERSION 3.10)
project(ut C)

set(CMAKE_C_STANDARD 11)

include_directories("../misc")
include_directories("../auxiliary")
include_directories("../core")

add_executable(ut main.c
        lock_ut.c "../auxiliary/spinlock.c"
        list_ut.c
        heap_ut.c "../auxiliary/heap.c"
        cxa_guard_ut.c "../auxiliary/cxa_guard.c")

target_compile_definitions(ut PRIVATE UNIT_TEST_BUILD)
