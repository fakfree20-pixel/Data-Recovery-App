cmake_minimum_required(VERSION 3.22.1)
project("datarecovery")

add_library(
        datarecovery
        SHARED
        native-lib.cpp
)

find_library(
        log-lib
        log
)

target_link_libraries(
        datarecovery
        ${log-lib}
)

