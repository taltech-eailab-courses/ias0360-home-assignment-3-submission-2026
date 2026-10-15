# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# set(PICO_BOARD pico_w)
set(PICO_BOARD pico2_w CACHE STRING "Board type")

set(PROGRAM_NAME main)

cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(${PROGRAM_NAME} C CXX ASM)
set(PICO_CYW43_ARCH lwip_sys_freertos CACHE STRING "" FORCE)


pico_sdk_init()


add_subdirectory(lib/config)
add_subdirectory(lib/lcd)
add_subdirectory(lib/font)
add_subdirectory(lib/context)
add_subdirectory(lib/http_client)
add_subdirectory(lib/model)
add_subdirectory(lib/imu)
add_subdirectory(lib/pico-tflmicro)
add_subdirectory(lib/sd_card_driver)

include_directories(./lib/config)
include_directories(./lib/lcd)
include_directories(./lib/font)
include_directories(./lib/context)
include_directories(./lib/http_client)
include_directories(./lib/model)
include_directories(./lib/imu)
include_directories(./lib/pico-tflmicro)
include_directories(./lib/sd_card_driver)


add_library(sd_card_wrapper
    src/sd_card_wrapper.cpp
    config/hw_config.c
)

target_include_directories(sd_card_wrapper PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/lib/sd_card_driver
)

target_link_libraries(sd_card_wrapper
    pico_stdlib
    sd_custom_driver
)

add_library(data_preprocessor
    src/data_preprocessor.cpp
    src/data_preprocessor.hpp
)

target_include_directories(data_preprocessor PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(data_preprocessor
    pico_stdlib
)

# air-writing trajectory estimation + file/payload formats (portable, also built on a PC by test/host/Makefile)
add_library(position_estimator src/position_estimator.cpp)
target_include_directories(position_estimator PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)

# Deployment settings, e.g. cmake -DAW_SERVER_HOST=my.server -DAW_SERVER_PORT=5000 -DAW_API_TOKEN=secret
#   -DAW_WIFI_SSID=... -DAW_WIFI_PASSWORD=... -DAW_UPLOAD_RESAMPLE_POINTS=45 -DAW_LPF_CUTOFF_HZ=5
foreach(var AW_SERVER_HOST AW_API_TOKEN AW_WIFI_SSID AW_WIFI_PASSWORD)
    set(${var} "" CACHE STRING "air-writing setting (string)")
endforeach()
foreach(var AW_SERVER_PORT AW_UPLOAD_RESAMPLE_POINTS AW_LPF_CUTOFF_HZ)
    set(${var} "" CACHE STRING "air-writing setting (number)")
endforeach()


add_executable(gesture_recognition gesture_recognition.cpp) # to test hand written digit recognition with static test data.
add_executable(letter_recognition letter_recognition.cpp) # to test hand written letter recognition with static test data.

add_executable(tensor_arena_test test/arena_size_test.cpp) # to check arena size for a given model.
add_executable(local_inference_test test/local_inference_test.cpp) # to test hand written digit recognition with static test data.
add_executable(local_inference_raw_test test/local_inference_raw_test.cpp) # to test hand written digit recognition with static test data.
add_executable(gesture_recognition_test test/gesture_recognition_test.cpp) # to test hand written digit recognition with static test data.
add_executable(letter_recognition_test test/letter_recognition_test.cpp) # to test hand written letter recognition with static test data.


target_include_directories(gesture_recognition
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(letter_recognition
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_include_directories(tensor_arena_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(local_inference_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(gesture_recognition_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(local_inference_raw_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(letter_recognition_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
    ${CMAKE_CURRENT_LIST_DIR}/src
)


set_target_properties(gesture_recognition
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(letter_recognition
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)

set_target_properties(tensor_arena_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(local_inference_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(local_inference_raw_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(gesture_recognition_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(letter_recognition_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)


pico_set_linker_script(gesture_recognition ${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/gesture_recognition_memmap.ld)
pico_set_linker_script(letter_recognition ${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/letter_recognition_memmap.ld)

target_link_libraries(
    gesture_recognition
    lcd 
    font 
    config
    model
    context
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    pico-tflmicro
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mbedtls
    pico_mbedtls
)
target_link_libraries(
    letter_recognition 
    lcd 
    font 
    config
    context
    http_client
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    hardware_i2c
    pico_util
    icm20948
    sd_card_wrapper
    position_estimator
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
)


target_link_libraries(
    tensor_arena_test 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    pico-tflmicro
)
target_link_libraries(
    local_inference_test 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    data_preprocessor
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
)
target_link_libraries(
    local_inference_raw_test 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    data_preprocessor
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
)

# IMU-based gesture recognition test with raw IMU readings
target_link_libraries(
    gesture_recognition_test
    lcd 
    font 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    hardware_pio
    hardware_i2c
    pico_util
    icm20948
    data_preprocessor
    pico-tflmicro
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mbedtls
    pico_mbedtls
)

# OCR
target_link_libraries(
    letter_recognition_test
    lcd 
    font 
    config
    http_client
    position_estimator
    icm20948
    hardware_i2c
    pico_util
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    sd_card_wrapper
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
)

set_property(TARGET gesture_recognition APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET letter_recognition APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET tensor_arena_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET local_inference_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET local_inference_raw_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET letter_recognition_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET gesture_recognition_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)

pico_enable_stdio_usb(gesture_recognition 1)
pico_enable_stdio_uart(gesture_recognition 0)

pico_enable_stdio_usb(letter_recognition 1)
pico_enable_stdio_uart(letter_recognition 0)

pico_enable_stdio_usb(tensor_arena_test 1)
pico_enable_stdio_uart(tensor_arena_test 1)

pico_enable_stdio_usb(local_inference_test 1)
pico_enable_stdio_uart(local_inference_test 1)

pico_enable_stdio_usb(local_inference_raw_test 1)
pico_enable_stdio_uart(local_inference_raw_test 1)

pico_generate_pio_header(gesture_recognition_test ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
pico_enable_stdio_usb(gesture_recognition_test 1)
pico_enable_stdio_uart(gesture_recognition_test 0)

foreach(var AW_SERVER_HOST AW_API_TOKEN AW_WIFI_SSID AW_WIFI_PASSWORD)
    if(NOT "${${var}}" STREQUAL "")
        target_compile_definitions(letter_recognition_test PRIVATE ${var}="${${var}}")
    endif()
endforeach()
foreach(var AW_SERVER_PORT AW_UPLOAD_RESAMPLE_POINTS AW_LPF_CUTOFF_HZ)
    if(NOT "${${var}}" STREQUAL "")
        target_compile_definitions(letter_recognition_test PRIVATE ${var}=${${var}})
        target_compile_definitions(position_estimator PUBLIC ${var}=${${var}})
    endif()
endforeach()

pico_enable_stdio_usb(letter_recognition_test 1)
pico_enable_stdio_uart(letter_recognition_test 0)


# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(gesture_recognition)

pico_add_extra_outputs(letter_recognition)

pico_add_extra_outputs(tensor_arena_test)

pico_add_extra_outputs(local_inference_test)
pico_add_extra_outputs(local_inference_raw_test)

pico_add_extra_outputs(gesture_recognition_test)

pico_add_extra_outputs(letter_recognition_test)
