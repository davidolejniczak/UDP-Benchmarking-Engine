#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_config.h"

#define SERVER_PORT 8010
#define SEND_INTERVAL_MS 1
#define WIFI_MAXIMUM_RETRY 10

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define UDP_PAYLOAD_SIZE 12

static const char* TAG = "udp_benchmark";
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count;

static void write_u32_le(uint8_t* output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xff);
    output[1] = (uint8_t)((value >> 8) & 0xff);
    output[2] = (uint8_t)((value >> 16) & 0xff);
    output[3] = (uint8_t)((value >> 24) & 0xff);
}

static void write_u64_le(uint8_t* output, uint64_t value)
{
    output[0] = (uint8_t)(value & 0xff);
    output[1] = (uint8_t)((value >> 8) & 0xff);
    output[2] = (uint8_t)((value >> 16) & 0xff);
    output[3] = (uint8_t)((value >> 24) & 0xff);
    output[4] = (uint8_t)((value >> 32) & 0xff);
    output[5] = (uint8_t)((value >> 40) & 0xff);
    output[6] = (uint8_t)((value >> 48) & 0xff);
    output[7] = (uint8_t)((value >> 56) & 0xff);
}

static void wifi_event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < WIFI_MAXIMUM_RETRY) {
            wifi_retry_count++;
            esp_wifi_connect();
            ESP_LOGI(TAG, "retrying WiFi connection");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        &wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        &ip_event_instance));

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(wifi_config.sta.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to WiFi SSID %s", WIFI_SSID);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "failed to connect to WiFi SSID %s", WIFI_SSID);
    return ESP_FAIL;
}

static void udp_sender_task(void* arg)
{
    uint8_t payload[UDP_PAYLOAD_SIZE];
    uint32_t packet_id = 1;

    struct sockaddr_in destination = {0};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &destination.sin_addr) != 1) {
        ESP_LOGE(TAG, "invalid SERVER_IP: %s", SERVER_IP);
        vTaskDelete(NULL);
        return;
    }

    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "socket creation failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "sending UDP packets to %s:%d", SERVER_IP, SERVER_PORT);

    while (true) {
        uint64_t timestamp_ns = (uint64_t)esp_timer_get_time() * 1000ULL;

        write_u32_le(payload, packet_id);
        write_u64_le(payload + sizeof(uint32_t), timestamp_ns);

        int bytes_sent = sendto(
            socket_fd,
            payload,
            sizeof(payload),
            0,
            (struct sockaddr*)&destination,
            sizeof(destination));

        if (bytes_sent < 0) {
            ESP_LOGE(TAG, "sendto failed: errno %d", errno);
        }

        packet_id++;
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}

void app_main(void)
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    if (wifi_init_sta() != ESP_OK) {
        return;
    }

    xTaskCreate(udp_sender_task, "udp_sender", 4096, NULL, 5, NULL);
}
