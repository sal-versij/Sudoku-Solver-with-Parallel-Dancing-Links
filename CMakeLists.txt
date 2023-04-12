cmake_minimum_required(VERSION 3.23)
project(tsp C)

set(CMAKE_C_STANDARD 99)

find_package(OpenCL REQUIRED)
include_directories(${OpenCL_INCLUDE_DIRS})
link_directories(${OpenCL_LIBRARY})

add_executable(list list.c)
target_include_directories(list PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(list ${OpenCL_LIBRARY})

add_executable(dlx_serial dancing_links_serial.c)
target_include_directories(dlx_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dlx_serial ${OpenCL_LIBRARY})

add_executable(dlx_parallel dancing_links_parallel.c)
target_include_directories(dlx_parallel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dlx_parallel ${OpenCL_LIBRARY})

add_executable(dlx_parallel_2 dancing_links_parallel_2.c)
target_include_directories(dlx_parallel_2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dlx_parallel_2 ${OpenCL_LIBRARY})
