cmake_minimum_required(VERSION 3.22.1)
project("nativecarver")

add_library(
        nativecarver
        SHARED
        native-lib.cpp
)

find_library(
        log-lib
        log
)

target_link_libraries(
        nativecarver
        ${log-lib}
)
