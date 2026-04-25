# dtmc_zephyr_tests/CMakeLists.txt

file(GLOB MY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/test*.c)

# zephyr_library_named(dtmc_zephyr_tests_library)

# zephyr_library_sources(${MY_SOURCES})

# zephyr_library_include_directories(
#     ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_tests/include
#     ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/include
#     ${DTMC_BASE_DIR}/dtmc_base_library/include
#     ${DTCORE_DIR}/dtcore_library/include
# )

# zephyr_library_link_libraries(
#     dtmc_base_library
#     dtcore_library
# )

target_sources(app PRIVATE ${MY_SOURCES})
target_include_directories(app PRIVATE 
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_tests/include
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/include
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/src
    ${DTMC_BASE_DIR}/dtmc_base_library/include
    ${DTCORE_DIR}/dtcore_library/include
)
