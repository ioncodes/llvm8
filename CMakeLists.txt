# Based on https://github.com/ttroy50/cmake-examples/tree/master/02-sub-projects/A-basic
# https://cliutils.gitlab.io/modern-cmake/
cmake_minimum_required(VERSION 3.15)

project(llvm8)

# Enable solution folder support
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Append the CMake module search path so we can use our own modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/CMake)

# Require c++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM wrapper
include(LLVM)

# MSVC-specific options
if(MSVC)
    # This assumes the installed LLVM was built in Release mode
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/ZI /Od /Ob0 /DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/ZI /Od /Ob0 /DNDEBUG" CACHE STRING "" FORCE)

    if(${LLVM_USE_CRT_RELEASE} STREQUAL "MD")
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    elseif(${LLVM_USE_CRT_RELEASE} STREQUAL "MT")
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    else()
        message(FATAL_ERROR "Unsupported LLVM_USE_CRT_RELEASE=${LLVM_USE_CRT_RELEASE}")
    endif()
endif()

include_directories(include)

add_executable(${PROJECT_NAME} src/main.cpp src/instructions.hpp src/utils.hpp src/argparse.hpp)
target_link_libraries(${PROJECT_NAME} PRIVATE LLVM)

# Set the plugin as the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})