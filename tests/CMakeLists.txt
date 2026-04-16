cmake_minimum_required(VERSION 3.10)

enable_testing()

add_executable(test_rframe 
    test_rframe.c
)

target_link_libraries(test_rframe 
    PRIVATE 
        rframe
        pts_runtime
        unity
)

add_executable(test_pts
    test_pts.c
)

target_link_libraries(test_pts
    PRIVATE
        pts_runtime
        unity
)

add_test(test_rframe test_rframe)
add_test(test_pts test_pts)