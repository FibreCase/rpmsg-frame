cmake_minimum_required(VERSION 3.10)

enable_testing()

add_executable(test_add 
    test_add.c
)

target_link_libraries(test_add 
    PRIVATE 
        add
        unity
)

add_test(test_add test_add)