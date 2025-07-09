if (CMAKE_VERSION VERSION_LESS 3.21)
    get_property(not_top DIRECTORY PROPERTY PARENT_DIRECTORY)
    if (not_top)
        set(${PROJECT_NAME}_IS_TOP_LEVEL false)
    else ()
        set(${PROJECT_NAME}_IS_TOP_LEVEL true)
    endif ()
endif ()

include(CMakeDependentOption)

cmake_dependent_option(LIBURING_BUILD_EXAMPLES "Build liburing examples." ON "${PROJECT_NAME}_IS_TOP_LEVEL" OFF)
cmake_dependent_option(LIBURING_BUILD_BENCHMARKS "Build liburing benchmarks." ON "${PROJECT_NAME}_IS_TOP_LEVEL" OFF)