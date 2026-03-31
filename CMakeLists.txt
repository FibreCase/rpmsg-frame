cmake_minimum_required(VERSION 3.10)
project(c_test_demo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(src)
add_subdirectory(3rdparty/unity)
add_subdirectory(tests)

add_executable(${PROJECT_NAME}
    src/main.c
)

target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
        add
)