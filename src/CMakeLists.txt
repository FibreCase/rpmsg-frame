cmake_minimum_required(VERSION 3.10)

find_package(Threads REQUIRED)


# rframe library
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


# pts library
add_library(pts_runtime
    pts.c
)

target_compile_definitions(pts_runtime
    PRIVATE
        PTS_NO_MAIN
)

target_include_directories(pts_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pts_runtime
    PUBLIC
        Threads::Threads
)