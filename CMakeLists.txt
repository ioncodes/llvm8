# Based on https://github.com/ttroy50/cmake-examples/tree/master/02-sub-projects/A-basic
# https://cliutils.gitlab.io/modern-cmake/
cmake_minimum_required(VERSION 3.15)

# Hunter package configuration
include(CMake/HunterPackages.cmake)

project(llvm8)

# Enable solution folder support
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Require c++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM wrapper
include(CMake/LLVM.cmake)

# MSVC-specific options
if(MSVC)
    # TODO: This assumes the installed LLVM was built in Release mode
    # TODO: this is not very friendly cmake, probably this should respect the cache and not override the user's choice
    set(CMAKE_C_FLAGS_DEBUG "/ZI /Od /Ob0 /DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "/ZI /Od /Ob0 /DNDEBUG" CACHE STRING "" FORCE)

    if(${LLVM_USE_CRT_RELEASE} STREQUAL "MD")
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    elseif(${LLVM_USE_CRT_RELEASE} STREQUAL "MT")
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    else()
        message(FATAL_ERROR "Unsupported LLVM_USE_CRT_RELEASE=${LLVM_USE_CRT_RELEASE}")
    endif()
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/instructions.hpp
    src/utils.hpp
    src/argparse.hpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE LLVM)
target_include_directories(${PROJECT_NAME} PRIVATE include)

# Set the main project as the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})