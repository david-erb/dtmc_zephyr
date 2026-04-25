cmake_minimum_required(VERSION 3.20.0)

set(CONF_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_SYSTEM_COMMON.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_OPENTHREAD_COAP_COMMON.conf"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app_zephyr)

set(DTCORE_LIBRARY        ${CMAKE_CURRENT_SOURCE_DIR}/../../dtcore_library)
set(DTCORE_TESTS          ${CMAKE_CURRENT_SOURCE_DIR}/../../dtcore_tests)
set(DTMC_BASE_LIBRARY     ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_base_library)
set(DTMC_BASE_TESTS       ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_base_tests)
set(DTMC_ZEPHYR_LIBRARY  ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_zephyr_library)
set(DTMC_ZEPHYR_TESTS    ${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_zephyr_tests)

# Bring in external object libraries
add_subdirectory(${DTCORE_LIBRARY}        ${CMAKE_BINARY_DIR}/dtcore_library)
add_subdirectory(${DTCORE_TESTS}          ${CMAKE_BINARY_DIR}/dtcore_tests)
add_subdirectory(${DTMC_BASE_LIBRARY}     ${CMAKE_BINARY_DIR}/dtmc_base_library)
add_subdirectory(${DTMC_BASE_TESTS}       ${CMAKE_BINARY_DIR}/dtmc_base_tests)
add_subdirectory(${DTMC_ZEPHYR_LIBRARY}  ${CMAKE_BINARY_DIR}/dtmc_zephyr_library)
add_subdirectory(${DTMC_ZEPHYR_TESTS}    ${CMAKE_BINARY_DIR}/dtmc_zephyr_tests)

target_include_directories(app PRIVATE
    ${DTCORE_LIBRARY}/include
    ${DTCORE_TESTS}/include
    ${DTMC_BASE_LIBRARY}/include
    ${DTMC_BASE_TESTS}/include
    ${DTMC_ZEPHYR_LIBRARY}/include
    ${DTMC_ZEPHYR_TESTS}/include
)

# Use the Zephyr build target (called 'app') – this is created implicitly
target_sources(app PRIVATE
    src/main.c
)
