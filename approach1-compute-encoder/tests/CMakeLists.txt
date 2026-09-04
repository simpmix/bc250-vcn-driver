# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
cmake_minimum_required(VERSION 3.16)

add_executable(test_bitstream test_bitstream.c ../src/bitstream.c)
target_include_directories(test_bitstream PRIVATE ../src)

add_executable(test_va_api test_va_api.c)
target_include_directories(test_va_api PRIVATE ../src ${Vulkan_INCLUDE_DIRS} ${LIBVA_INCLUDE_DIRS} ${DRM_INCLUDE_DIRS})
find_package(Threads REQUIRED)
target_link_libraries(test_va_api PRIVATE bc250_drv_video ${Vulkan_LIBRARIES} ${LIBVA_LIBRARIES} ${DRM_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} m)

add_executable(test_cavlc test_cavlc.c ../src/cavlc.c ../src/bitstream.c)
target_include_directories(test_cavlc PRIVATE ../src)

add_executable(test_encode test_encode.c)
target_include_directories(test_encode PRIVATE ../src ${Vulkan_INCLUDE_DIRS} ${LIBVA_INCLUDE_DIRS} ${DRM_INCLUDE_DIRS})
target_link_libraries(test_encode PRIVATE bc250_drv_video ${Vulkan_LIBRARIES} ${LIBVA_LIBRARIES} ${DRM_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} m)

enable_testing()
add_test(NAME BitstreamTest COMMAND test_bitstream)
add_test(NAME CavlcUnitTest COMMAND test_cavlc)
add_test(NAME VaApiDriverTest COMMAND test_va_api)
add_test(NAME EncodeBitstreamTest COMMAND test_encode WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
