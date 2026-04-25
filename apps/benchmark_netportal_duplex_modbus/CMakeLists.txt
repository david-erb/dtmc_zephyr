cmake_minimum_required(VERSION 3.20.0)

set(CONF_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_SYSTEM_COMMON.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_DTIOX_MODBUS_COMMON.conf"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app_zephyr)

include(${CMAKE_CURRENT_LIST_DIR}/../../dthelpers.cmake)
dthelpers_resolve(DTCORE_DIR dtcore "dtcore repo directory")
dthelpers_resolve(DTMC_BASE_DIR dtmc_base "dtmc_base repo directory")

add_subdirectory(${DTCORE_DIR}/dtcore_library           ${CMAKE_BINARY_DIR}/dtcore_library)
add_subdirectory(${DTMC_BASE_DIR}/dtmc_base_library     ${CMAKE_BINARY_DIR}/dtmc_base_library)
add_subdirectory(${DTMC_BASE_DIR}/dtmc_base_benchmarks  ${CMAKE_BINARY_DIR}/dtmc_base_benchmarks)
add_subdirectory(../../dtmc_zephyr_library              ${CMAKE_BINARY_DIR}/dtmc_zephyr_library)


target_include_directories(app PRIVATE
    ${DTCORE_DIR}/dtcore_library/include
    ${DTMC_BASE_DIR}/dtmc_base_library/include
    ${DTMC_BASE_DIR}/dtmc_base_benchmarks/include
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/include
)

# Use the Zephyr build target (called 'app') – this is created implicitly
target_sources(app PRIVATE
    src/main.c
)
