# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
cmake_minimum_required(VERSION 3.16)

add_executable(test_bitstream test_bitstream.c ../src/bitstream.c)
target_include_directories(test_bitstream PRIVATE ../src)

add_executable(test_va_api test_va_api.c)
target_include_directories(test_va_api PRIVATE ../src ${Vulkan_INCLUDE_DIRS} ${LIBVA_INCLUDE_DIRS} ${DRM_INCLUDE_DIRS})
find_package(Threads REQUIRED)
target_link_libraries(test_va_api PRIVATE bc250_drv_video ${Vulkan_LIBRARIES} ${LIBVA_LIBRARIES} ${DRM_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} m)

enable_testing()
add_test(NAME BitstreamTest COMMAND test_bitstream)
add_test(NAME VaApiDriverTest COMMAND test_va_api)
