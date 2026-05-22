set(ENV_FILE "${CMAKE_SOURCE_DIR}/.env")

if(NOT EXISTS "${ENV_FILE}")
    message(FATAL_ERROR "Missing ${ENV_FILE}. Create it with WIFI_SSID=..., WIFI_PASSWORD=..., SERVER_IP=..., SEND_INTERVAL_US=..., PAYLOAD_SIZE_BYTES=..., and TEST_DURATION_SEC=...")
endif()

file(STRINGS "${ENV_FILE}" ENV_LINES)

foreach(ENV_LINE IN LISTS ENV_LINES)
    string(STRIP "${ENV_LINE}" ENV_LINE)

    if(ENV_LINE STREQUAL "" OR ENV_LINE MATCHES "^#")
        continue()
    endif()

    if(ENV_LINE MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
        set(ENV_KEY "${CMAKE_MATCH_1}")
        set(ENV_VALUE "${CMAKE_MATCH_2}")
        string(STRIP "${ENV_VALUE}" ENV_VALUE)

        if(ENV_VALUE MATCHES "^\"(.*)\"$")
            set(ENV_VALUE "${CMAKE_MATCH_1}")
        elseif(ENV_VALUE MATCHES "^'(.*)'$")
            set(ENV_VALUE "${CMAKE_MATCH_1}")
        endif()

        set(ENV_${ENV_KEY} "${ENV_VALUE}")
    endif()
endforeach()

if(NOT DEFINED ENV_WIFI_SSID OR ENV_WIFI_SSID STREQUAL "")
    message(FATAL_ERROR "ESP32/.env must define WIFI_SSID")
endif()

if(NOT DEFINED ENV_WIFI_PASSWORD)
    message(FATAL_ERROR "ESP32/.env must define WIFI_PASSWORD")
endif()

if(NOT DEFINED ENV_SERVER_IP OR ENV_SERVER_IP STREQUAL "")
    message(FATAL_ERROR "ESP32/.env must define SERVER_IP")
endif()

if(NOT DEFINED ENV_SEND_INTERVAL_US OR NOT ENV_SEND_INTERVAL_US MATCHES "^[0-9]+$")
    message(FATAL_ERROR "ESP32/.env must define numeric SEND_INTERVAL_US")
endif()

if(NOT DEFINED ENV_PAYLOAD_SIZE_BYTES OR NOT ENV_PAYLOAD_SIZE_BYTES MATCHES "^[0-9]+$")
    message(FATAL_ERROR "ESP32/.env must define numeric PAYLOAD_SIZE_BYTES")
endif()

if(NOT DEFINED ENV_TEST_DURATION_SEC OR NOT ENV_TEST_DURATION_SEC MATCHES "^[0-9]+$")
    message(FATAL_ERROR "ESP32/.env must define numeric TEST_DURATION_SEC")
endif()

function(escape_c_string INPUT OUTPUT)
    string(REPLACE "\\" "\\\\" ESCAPED_VALUE "${INPUT}")
    string(REPLACE "\"" "\\\"" ESCAPED_VALUE "${ESCAPED_VALUE}")
    set(${OUTPUT} "${ESCAPED_VALUE}" PARENT_SCOPE)
endfunction()

escape_c_string("${ENV_WIFI_SSID}" WIFI_SSID_C)
escape_c_string("${ENV_WIFI_PASSWORD}" WIFI_PASSWORD_C)
escape_c_string("${ENV_SERVER_IP}" SERVER_IP_C)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/wifi_config.h"
    "#pragma once\n"
    "#define WIFI_SSID \"${WIFI_SSID_C}\"\n"
    "#define WIFI_PASSWORD \"${WIFI_PASSWORD_C}\"\n"
    "#define SERVER_IP \"${SERVER_IP_C}\"\n"
    "#define SEND_INTERVAL_US ${ENV_SEND_INTERVAL_US}ULL\n"
    "#define PAYLOAD_SIZE_BYTES ${ENV_PAYLOAD_SIZE_BYTES}U\n"
    "#define TEST_DURATION_SEC ${ENV_TEST_DURATION_SEC}ULL\n")

idf_component_register(SRCS "udp_benchmark_main.cpp"
                       PRIV_REQUIRES esp_event esp_netif esp_rom esp_timer esp_wifi lwip nvs_flash
                       INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}")
