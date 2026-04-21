include(${CMAKE_CURRENT_LIST_DIR}/../dthelpers.cmake)
file(GLOB MY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dt*.c)

if(NOT CONFIG_BT)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtnetportal_btle")
endif()

if(NOT CONFIG_OPENTHREAD_COAP)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtnetportal_coap")
endif()

if(NOT CONFIG_SENSOR)
    list(REMOVE_ITEM MY_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dtlightsensor_zephyr.c
    )
endif()

if(NOT CONFIG_UART_INTERRUPT_DRIVEN)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtiox_zephyr_uartirq*")
endif()

if(NOT CONFIG_MODBUS)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtiox_zephyr_modbus")
endif()

if(NOT CONFIG_CAN)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtiox_zephyr_canbus")
endif()

if (NOT CONFIG_ADC)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtadc_zephyr_saadc")
endif()

if (NOT CONFIG_GPIO)
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtgpiopin_zephyr*")
endif()
  
if(CONFIG_NATIVE_SIM OR CONFIG_BOARD_NATIVE_SIM)                                                                                                                                                                                                                                                                   
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtcpu_zephyr_arm*")
else()                                                                                                                                                                                                                                                                                                             
    list(FILTER MY_SOURCES EXCLUDE REGEX "/dtcpu_zephyr_native*")
endif()  

target_sources(app PRIVATE ${MY_SOURCES})
target_include_directories(app PRIVATE 
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/include
    ${DTMC_ZEPHYR_DIR}/dtmc_zephyr_library/src
    ${DTCORE_DIR}/dtcore_library/include
)
