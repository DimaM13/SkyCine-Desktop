cmake_minimum_required(VERSION 3.20)
project(fastwarp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DVS_USE_LATEST_API)

find_package(Vulkan REQUIRED)
find_package(OpenMP)

find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)
if(GLSLANG_VALIDATOR)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/warp_spv.h
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/warp_yuv.comp -o ${CMAKE_CURRENT_SOURCE_DIR}/warp_spv.h --vn warp_spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/warp_yuv.comp
        COMMENT "Compiling Vulkan compute shader to SPIR-V header"
    )
endif()

add_library(fastwarp SHARED
    plugin.cpp
    vulkan_warper.cpp
)

target_include_directories(fastwarp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(fastwarp PRIVATE
    Vulkan::Vulkan
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(fastwarp PRIVATE OpenMP::OpenMP_CXX)
endif()

if(MSVC)
    target_compile_options(fastwarp PRIVATE /O2 /Oi /Ot /fp:fast)
endif()

set_target_properties(fastwarp PROPERTIES
    PREFIX ""
    OUTPUT_NAME "librife_fastwarp"
)
