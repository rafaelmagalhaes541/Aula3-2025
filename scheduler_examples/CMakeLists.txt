cmake_minimum_required(VERSION 3.25)
project(scheduling C)

set(CMAKE_C_STANDARD 11)

add_executable(scheduler ossim.c queue.c fifo.c
        cmake-build-debug/sfj.c
        cmake-build-debug/rr.c
        sjf.h
        rr.h)

add_executable(app app.c)

add_executable(app-io app-io.c burst_queue.c)
