cmake_minimum_required(VERSION 3.10)

set(LIB_MAJOR_VERSION "0")
set(LIB_MINOR_VERSION "1")
set(LIB_PATCH_VERSION "0")
set(LIB_VERSION_STRING "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_PATCH_VERSION}")

PROJECT(liburing VERSION "${LIB_VERSION_STRING}" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if (NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    message(WARNING "io_uring is only supported by Linux, but the target OS is ${CMAKE_SYSTEM_NAME}.")
endif ()

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

include(${PROJECT_SOURCE_DIR}/cmake/Option.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Configure.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Install.cmake)

target_include_directories(${PROJECT_NAME} INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if (LIBURING_BUILD_EXAMPLES OR LIBURING_BUILD_BENCHMARKS)
    # compile in release by default
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    endif ()
endif ()

add_subdirectory(example)
