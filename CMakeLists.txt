cmake_minimum_required(VERSION 3.10)
project(frf LANGUAGES C)

find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(3rdparty/unity)
    add_subdirectory(tests)
endif()


# Main executable
add_executable(${PROJECT_NAME}
    src/main.c
)
    
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
        rframe
)


# Optional pts executable
add_executable(${PROJECT_NAME}_pts
    src/pts.c
)

target_link_libraries(${PROJECT_NAME}_pts
    PRIVATE
        Threads::Threads
)
