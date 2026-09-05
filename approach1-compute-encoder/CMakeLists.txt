# bc250-vcn-driver v0.2.0 - https://github.com/Kai/bc250-vcn-driver
cmake_minimum_required(VERSION 3.16)
project(bc250-compute-encoder VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -fPIC")

option(ENABLE_DEBUG "Enable debug build with sanitizers" OFF)
if(ENABLE_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fsanitize=address,undefined")
endif()

# Find dependencies
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVA REQUIRED libva)
pkg_check_modules(DRM REQUIRED libdrm)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${LIBVA_INCLUDE_DIRS}
    ${DRM_INCLUDE_DIRS}
)

# Look for shader compilers
find_program(GLSLANG_VALIDATOR glslangValidator)
find_program(GLSLC glslc)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
file(GLOB SHADERS "${SHADER_DIR}/*.comp")
set(SPV_OUTPUTS "")

# Compile shaders
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.spv")
    
    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling compute shader ${SHADER_NAME} with glslangValidator"
        )
        list(APPEND SPV_OUTPUTS ${SPV_FILE})
    elseif(GLSLC)
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLC} -c ${SHADER} -o ${SPV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling compute shader ${SHADER_NAME} with glslc"
        )
        list(APPEND SPV_OUTPUTS ${SPV_FILE})
    else()
        message(WARNING "Neither glslangValidator nor glslc found. Shaders must be compiled separately.")
    endif()
endforeach()

if(SPV_OUTPUTS)
    add_custom_target(compile_shaders ALL DEPENDS ${SPV_OUTPUTS})
endif()

# Build the VA-API driver
set(SOURCES
    src/va_backend.c
    src/gpu_compute.c
    src/bitstream.c
    src/cavlc.c
    src/rate_control.c
    src/encoder_h264.c
    src/encoder_h265.c
    src/decoder_h264.c
)

add_library(bc250_drv_video SHARED ${SOURCES})
set_target_properties(bc250_drv_video PROPERTIES
    PREFIX ""
    OUTPUT_NAME "bc250_drv_video"
)
if(TARGET compile_shaders)
    add_dependencies(bc250_drv_video compile_shaders)
endif()

find_package(Threads REQUIRED)

target_link_libraries(bc250_drv_video
    ${Vulkan_LIBRARIES}
    ${LIBVA_LIBRARIES}
    ${DRM_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    m
)

# Installation paths
if(NOT LIBVA_DRIVERS_PATH)
    if(EXISTS "/usr/lib/x86_64-linux-gnu/dri")
        set(LIBVA_DRIVERS_PATH "/usr/lib/x86_64-linux-gnu/dri/")
    elseif(EXISTS "/usr/lib64/dri")
        set(LIBVA_DRIVERS_PATH "/usr/lib64/dri/")
    else()
        set(LIBVA_DRIVERS_PATH "/usr/lib/dri/")
    endif()
endif()

set(SHADER_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/share/bc250/shaders")

install(TARGETS bc250_drv_video DESTINATION ${LIBVA_DRIVERS_PATH})
if(SPV_OUTPUTS)
    install(FILES ${SPV_OUTPUTS} DESTINATION ${SHADER_INSTALL_PATH})
endif()

# Tests
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

message(STATUS "VA-API driver target: bc250_drv_video.so")
message(STATUS "Driver install path: ${LIBVA_DRIVERS_PATH}")
message(STATUS "Shader install path: ${SHADER_INSTALL_PATH}")
message(STATUS "Usage: export LIBVA_DRIVER_NAME=bc250")
