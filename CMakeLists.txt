cmake_minimum_required(VERSION 3.23)
project(tsp C)

set(CMAKE_C_STANDARD 99)

find_package(OpenCL REQUIRED)
include_directories(${OpenCL_INCLUDE_DIRS})
link_directories(${OpenCL_LIBRARY})

add_executable(list list.c)
target_include_directories(list PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(list ${OpenCL_LIBRARY})

add_executable(sbt simple_bruteforce.c)
target_include_directories(sbt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sbt ${OpenCL_LIBRARY})

add_executable(dlx dancing_links.c)
target_include_directories(dlx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dlx ${OpenCL_LIBRARY})