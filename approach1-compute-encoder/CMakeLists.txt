# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
cmake_minimum_required(VERSION 3.16)
project(bc250-compute-encoder C)

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

# Custom target for shaders
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
file(GLOB SHADERS "${SHADER_DIR}/*.comp")
set(SPV_OUTPUTS "")

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.spv")
    
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND glslangValidator -V ${SHADER} -o ${SPV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling compute shader ${SHADER_NAME}"
    )
    list(APPEND SPV_OUTPUTS ${SPV_FILE})
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SPV_OUTPUTS})

# Build the VA-API driver
set(SOURCES
    src/va_backend.c
    src/gpu_compute.c
)

add_library(bc250_drv_video SHARED ${SOURCES})
add_dependencies(bc250_drv_video compile_shaders)

target_link_libraries(bc250_drv_video
    ${Vulkan_LIBRARIES}
    ${LIBVA_LIBRARIES}
    ${DRM_LIBRARIES}
)

# Installation
if(NOT LIBVA_DRIVERS_PATH)
    set(LIBVA_DRIVERS_PATH "/usr/lib/x86_64-linux-gnu/dri/")
endif()

install(TARGETS bc250_drv_video DESTINATION ${LIBVA_DRIVERS_PATH})

message(STATUS "VA-API driver will be installed to: ${LIBVA_DRIVERS_PATH}")
message(STATUS "To use this driver, set: LIBVA_DRIVER_NAME=bc250")
