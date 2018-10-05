cmake_minimum_required(VERSION 2.8.12)

project(lockstep_scheduler)

set (CMAKE_CXX_STANDARD 11)

add_definitions(-Wall -Wextra -Werror)

add_library(lockstep_scheduler
    lockstep_scheduler.cpp
)

add_executable(lockstep_scheduler_test
    lockstep_scheduler_test.cpp
)

target_link_libraries(lockstep_scheduler_test
    lockstep_scheduler
)
