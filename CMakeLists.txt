cmake_minimum_required(VERSION 3.10)

project(steg_project LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(steg_lib STATIC
    src/bmp.c
    src/steg.c
)

# Link math library if needed (for sqrt)
if(UNIX)
    target_link_libraries(steg_lib m)
endif()

add_executable(steg_cli
    src/main.c
)

target_link_libraries(steg_cli steg_lib)

enable_testing()
add_subdirectory(tests)


