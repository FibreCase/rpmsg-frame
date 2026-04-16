cmake_minimum_required(VERSION 3.10)

find_package(Threads REQUIRED)

add_library(rframe
    tty_driver.c
    rframe.c
)

target_include_directories(rframe 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rframe
    PUBLIC
        Threads::Threads
)