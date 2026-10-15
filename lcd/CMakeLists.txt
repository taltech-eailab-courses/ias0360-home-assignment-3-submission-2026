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
set(PICO_BOARD pico_w)
# set(PICO_BOARD pico2_w CACHE STRING "Board type")

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
add_subdirectory(lib/pico-tflmicro)
add_subdirectory(lib/sd_card_driver)

include_directories(./lib/config)
include_directories(./lib/lcd)
include_directories(./lib/font)
include_directories(./lib/context)
include_directories(./lib/http_client)
include_directories(./lib/model)
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



add_executable(digit_recognition digit_recognition.cpp) # to test hand written digit recognition with static test data.
add_executable(letter_recognition letter_recognition.cpp) # to test hand written letter recognition with static test data.
add_executable(tensor_arena_test test/arena_size_test.cpp) # to check arena size for a given model.
add_executable(local_inference_test test/inference_test.cpp) # to test hand written digit recognition with static test data.
add_executable(digit_recognition_test test/digit_recognition_test.cpp) # to test hand written digit recognition with static test data.
add_executable(letter_recognition_test test/letter_recognition_test.cpp) # to test hand written letter recognition with static test data.


target_include_directories(digit_recognition
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
target_include_directories(digit_recognition_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(letter_recognition_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
    ${CMAKE_CURRENT_LIST_DIR}/src
)

set_target_properties(digit_recognition
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
set_target_properties(digit_recognition_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(letter_recognition_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)

pico_set_linker_script(digit_recognition ${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/digit_recognition_memmap.ld)
pico_set_linker_script(letter_recognition ${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/letter_recognition_memmap.ld)


target_link_libraries(
    digit_recognition
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
    sd_card_wrapper
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
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
)

# MNIST
target_link_libraries(
    digit_recognition_test
    lcd 
    font 
    config
    model
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

# OCR
target_link_libraries(
    letter_recognition_test
    lcd 
    font 
    config
    http_client
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

                        
set_property(TARGET digit_recognition APPEND_STRING PROPERTY LINK_FLAGS 
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
set_property(TARGET digit_recognition_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET letter_recognition_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)

# enable usb output, disable uart output

pico_enable_stdio_usb(digit_recognition 1)
pico_enable_stdio_uart(digit_recognition 0)

pico_enable_stdio_usb(letter_recognition 1)
pico_enable_stdio_uart(letter_recognition 0)

pico_enable_stdio_usb(tensor_arena_test 1)
pico_enable_stdio_uart(tensor_arena_test 1)

pico_enable_stdio_usb(local_inference_test 1)
pico_enable_stdio_uart(local_inference_test 1)

pico_enable_stdio_usb(digit_recognition_test 1)
pico_enable_stdio_uart(digit_recognition_test 0)

pico_enable_stdio_usb(letter_recognition_test 1)
pico_enable_stdio_uart(letter_recognition_test 0)


# create map/bin/hex/uf2 file etc.

pico_add_extra_outputs(digit_recognition)

pico_add_extra_outputs(letter_recognition)

pico_add_extra_outputs(tensor_arena_test)

pico_add_extra_outputs(local_inference_test)

pico_add_extra_outputs(digit_recognition_test)

pico_add_extra_outputs(letter_recognition_test)