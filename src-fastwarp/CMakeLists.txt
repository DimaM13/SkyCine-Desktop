cmake_minimum_required(VERSION 3.20)
project(fastwarp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP)

add_library(fastwarp SHARED
    plugin.cpp
)

target_include_directories(fastwarp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(fastwarp PRIVATE OpenMP::OpenMP_CXX)
endif()

if(MSVC)
    target_compile_options(fastwarp PRIVATE /O2 /Oi /Ot /arch:AVX2 /fp:fast)
endif()

set_target_properties(fastwarp PROPERTIES
    PREFIX ""
    OUTPUT_NAME "librife_fastwarp"
)
