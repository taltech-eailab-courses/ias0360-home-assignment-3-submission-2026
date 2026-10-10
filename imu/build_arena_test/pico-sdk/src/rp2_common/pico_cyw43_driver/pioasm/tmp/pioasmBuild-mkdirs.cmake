# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/opt/pico-sdk/tools/pioasm"
  "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pioasm"
  "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pioasm-install"
  "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/tmp"
  "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp"
  "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src"
  "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/root/submission/ias0360-home-assignment-3-submission-2026/imu/build_arena_test/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
