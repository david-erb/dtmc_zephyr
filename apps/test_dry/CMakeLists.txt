cmake_minimum_required(VERSION 3.20.0)

set(CONF_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../DTMC_ZEPHYR_SYSTEM_COMMON.conf"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app_zephyr)

include(${CMAKE_CURRENT_LIST_DIR}/../../dthelpers.cmake)
dthelpers_resolve(DTMC_ZEPHYR_DIR dtmc_zephyr "dtmc_zephyr repo directory")
dthelpers_resolve(DTMC_BASE_DIR dtmc_base "dtmc_base repo directory")
dthelpers_resolve(DTCORE_DIR dtcore "dtcore repo directory")

add_subdirectory(${DTMC_ZEPHYR_DIR}/dtmc_zephyr_tests        ${CMAKE_BINARY_DIR}/dtmc_zephyr_tests)
add_subdirectory(${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library      ${CMAKE_BINARY_DIR}/dtmc_zephyr_library)
add_subdirectory(${DTMC_BASE_DIR}/dtmc_base_tests            ${CMAKE_BINARY_DIR}/dtmc_base_tests)
add_subdirectory(${DTMC_BASE_DIR}/dtmc_base_library          ${CMAKE_BINARY_DIR}/dtmc_base_library)
add_subdirectory(${DTCORE_DIR}/dtcore_tests                  ${CMAKE_BINARY_DIR}/dtcore_tests)
add_subdirectory(${DTCORE_DIR}/dtcore_library                ${CMAKE_BINARY_DIR}/dtcore_library)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_tests/include
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/include
    ${DTMC_BASE_DIR}/dtmc_base_tests/include
    ${DTMC_BASE_DIR}/dtmc_base_library/include
    ${DTCORE_DIR}/dtcore_tests/include
    ${DTCORE_DIR}/dtcore_library/include
)

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

target_link_libraries(app PRIVATE
    dtmc_base_tests
    dtmc_base_library
    dtcore_tests
    dtcore_library
)
