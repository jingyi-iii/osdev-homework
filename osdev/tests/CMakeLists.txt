cmake_minimum_required(VERSION 3.10)
project(ut C)

set(CMAKE_C_STANDARD 11)

include_directories("../include/lib")
include_directories("../include/sync")
include_directories("../include/mm")
include_directories("../include/kernel")

add_executable(ut main.c
        lock_ut.c "../kernel/sync/spinlock.c"
        list_ut.c
        heap_ut.c "../kernel/mm/heap.c"
        cxa_guard_ut.c "../kernel/sync/cxa_guard.c")

target_compile_definitions(ut PRIVATE UNIT_TEST_BUILD)
