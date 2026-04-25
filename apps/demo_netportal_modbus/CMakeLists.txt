cmake_minimum_required(VERSION 3.20.0)

set(CONF_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_SYSTEM_COMMON.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_DTIOX_MODBUS_COMMON.conf"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app_zephyr)

set(DTCORE_LIBRARY        ${CMAKE_CURRENT_SOURCE_DIR}/../../dtcore_library)
set(DTMC_BASE_LIBRARY     ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_base_library)
set(DTMC_BASE_DEMOS       ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_base_demos)
set(DTMC_ZEPHYR_LIBRARY   ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_zephyr_library)

# Bring in external object libraries
add_subdirectory(${DTCORE_LIBRARY}        ${CMAKE_BINARY_DIR}/dtcore_library)
add_subdirectory(${DTMC_BASE_LIBRARY}     ${CMAKE_BINARY_DIR}/dtmc_base_library)
add_subdirectory(${DTMC_BASE_DEMOS}       ${CMAKE_BINARY_DIR}/dtmc_base_demos)
add_subdirectory(${DTMC_ZEPHYR_LIBRARY}   ${CMAKE_BINARY_DIR}/dtmc_zephyr_library)

target_include_directories(app PRIVATE
    ${DTCORE_LIBRARY}/include
    ${DTMC_BASE_LIBRARY}/include
    ${DTMC_BASE_DEMOS}/include
    ${DTMC_ZEPHYR_LIBRARY}/include
)

# Use the Zephyr build target (called 'app') – this is created implicitly
target_sources(app PRIVATE
    src/main.c
)
