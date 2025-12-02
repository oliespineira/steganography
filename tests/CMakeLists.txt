cmake_minimum_required(VERSION 3.10)

enable_testing()

find_package(GTest REQUIRED)

add_executable(run_all_tests
    test_steg.cpp
)

target_include_directories(run_all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(run_all_tests
    steg_lib
    GTest::GTest
    GTest::Main
)

# Link math library if needed
if(UNIX)
    target_link_libraries(run_all_tests m)
endif()

add_test(NAME steg_tests COMMAND run_all_tests)


