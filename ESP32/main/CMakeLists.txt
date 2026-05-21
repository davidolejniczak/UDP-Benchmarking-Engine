set(ENV_FILE "${CMAKE_SOURCE_DIR}/.env")

if(NOT EXISTS "${ENV_FILE}")
    message(FATAL_ERROR "Missing ${ENV_FILE}. Create it with WIFI_SSID=..., WIFI_PASSWORD=..., and SERVER_IP=...")
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
    "#define SERVER_IP \"${SERVER_IP_C}\"\n")

idf_component_register(SRCS "udp_benchmark_main.c"
                       PRIV_REQUIRES esp_event esp_netif esp_timer esp_wifi lwip nvs_flash
                       INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}")
