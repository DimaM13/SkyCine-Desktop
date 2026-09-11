cmake_minimum_required(VERSION 3.20)
project(fastwarp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_library(fastwarp SHARED
    plugin.cpp
    vulkan_warper.cpp
    vulkan_warper.h
)

target_include_directories(fastwarp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bin/vapoursynth/include
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(fastwarp PRIVATE
    Vulkan::Vulkan
)

set_target_properties(fastwarp PROPERTIES
    PREFIX ""
    OUTPUT_NAME "librife_fastwarp"
)
