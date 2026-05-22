#include <errno.h>
#include <cstddef>
#include <stdint.h>
#include <memory>
#include <new>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_config.h"

#define SERVER_PORT 8010
#define WIFI_MAXIMUM_RETRY 10

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define UDP_HEADER_SIZE 12

#if PAYLOAD_SIZE_BYTES < UDP_HEADER_SIZE
#error "PAYLOAD_SIZE_BYTES must be at least 12"
#endif

static const char* TAG = "udp_benchmark";
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count;

class EspUdpPacketBuilder {
public:
    static constexpr size_t HEADER_SIZE = UDP_HEADER_SIZE;

    explicit EspUdpPacketBuilder(size_t payload_size)
        : payload_size_(payload_size),
          buffer_(new (std::nothrow) uint8_t[payload_size])
    {
        if (buffer_) {
            reset();
        }
    }

    bool valid() const
    {
        return buffer_ != nullptr;
    }

    EspUdpPacketBuilder& reset()
    {
        if (buffer_) {
            memset(buffer_.get(), 0, payload_size_);
        }
        return *this;
    }

    EspUdpPacketBuilder& with_packet_id(uint32_t packet_id)
    {
        packet_id_ = packet_id;
        write_u32_le(buffer_.get(), packet_id);
        return *this;
    }

    EspUdpPacketBuilder& with_timestamp_ns(uint64_t timestamp_ns)
    {
        write_u64_le(buffer_.get() + sizeof(uint32_t), timestamp_ns);
        return *this;
    }

    EspUdpPacketBuilder& with_deterministic_payload()
    {
        for (size_t i = HEADER_SIZE; i < payload_size_; i++) {
            buffer_[i] = static_cast<uint8_t>(packet_id_ + i);
        }
        return *this;
    }

    const uint8_t* data() const
    {
        return buffer_.get();
    }

    size_t size() const
    {
        return payload_size_;
    }

private:
    static void write_u32_le(uint8_t* output, uint32_t value)
    {
        output[0] = static_cast<uint8_t>(value & 0xff);
        output[1] = static_cast<uint8_t>((value >> 8) & 0xff);
        output[2] = static_cast<uint8_t>((value >> 16) & 0xff);
        output[3] = static_cast<uint8_t>((value >> 24) & 0xff);
    }

    static void write_u64_le(uint8_t* output, uint64_t value)
    {
        output[0] = static_cast<uint8_t>(value & 0xff);
        output[1] = static_cast<uint8_t>((value >> 8) & 0xff);
        output[2] = static_cast<uint8_t>((value >> 16) & 0xff);
        output[3] = static_cast<uint8_t>((value >> 24) & 0xff);
        output[4] = static_cast<uint8_t>((value >> 32) & 0xff);
        output[5] = static_cast<uint8_t>((value >> 40) & 0xff);
        output[6] = static_cast<uint8_t>((value >> 48) & 0xff);
        output[7] = static_cast<uint8_t>((value >> 56) & 0xff);
    }

    size_t payload_size_;
    uint32_t packet_id_ = 0;
    std::unique_ptr<uint8_t[]> buffer_;
};

static void delay_until_us(uint64_t target_time_us)
{
    while (true) {
        uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (now_us >= target_time_us) {
            return;
        }

        uint64_t remaining_us = target_time_us - now_us;
        if (remaining_us >= 2000) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(remaining_us / 1000)));
        } else {
            esp_rom_delay_us((uint32_t)remaining_us);
        }
    }
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
    EspUdpPacketBuilder packet_builder(PAYLOAD_SIZE_BYTES);
    uint32_t packet_id = 1;

    if (!packet_builder.valid()) {
        ESP_LOGE(TAG, "failed to allocate %u byte payload", PAYLOAD_SIZE_BYTES);
        vTaskDelete(NULL);
        return;
    }

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
    ESP_LOGI(
        TAG,
        "payload=%u bytes interval=%llu us duration=%llu sec",
        PAYLOAD_SIZE_BYTES,
        SEND_INTERVAL_US,
        TEST_DURATION_SEC);

    uint64_t start_time_us = (uint64_t)esp_timer_get_time();
    uint64_t end_time_us = start_time_us + (TEST_DURATION_SEC * 1000000ULL);
    uint64_t next_send_time_us = start_time_us;

    while ((uint64_t)esp_timer_get_time() < end_time_us) {
        uint64_t timestamp_ns = (uint64_t)esp_timer_get_time() * 1000ULL;

        packet_builder
            .reset()
            .with_packet_id(packet_id)
            .with_timestamp_ns(timestamp_ns)
            .with_deterministic_payload();

        int bytes_sent = sendto(
            socket_fd,
            packet_builder.data(),
            packet_builder.size(),
            0,
            reinterpret_cast<struct sockaddr*>(&destination),
            sizeof(destination));

        if (bytes_sent < 0) {
            ESP_LOGE(TAG, "sendto failed: errno %d", errno);
        }

        packet_id++;
        if (SEND_INTERVAL_US > 0) {
            next_send_time_us += SEND_INTERVAL_US;
            delay_until_us(next_send_time_us);
        }
    }

    ESP_LOGI(TAG, "benchmark complete, sent %lu packets", (unsigned long)(packet_id - 1));
    close(socket_fd);
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
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
