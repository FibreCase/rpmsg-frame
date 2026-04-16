cmake_minimum_required(VERSION 3.10)

enable_testing()

add_executable(test_rframe 
    test_rframe.c
)

target_link_libraries(test_rframe 
    PRIVATE 
        rframe
        unity
)

add_test(test_rframe test_rframe)