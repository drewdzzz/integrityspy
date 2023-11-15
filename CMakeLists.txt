cmake_minimum_required(VERSION 2.8)
project(integrityspy C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
            "Default build type: Debug" FORCE)
endif()

add_subdirectory(src)
