#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "driver/gpio.h"
#include "led_strip.h"

#include "mqtt_client.h"

static const char *TAG = "MQTT_EXAMPLE";

static esp_mqtt_client_handle_t client;

// ========== CONFIGURATION ==========
#define WIFI_SSID       "..."
#define WIFI_PASS       "..."
#define MAX_RETRY       5
#define UDP_SERVER_IP   "192.168.0.196"  // Your PC's IP address
#define UDP_SERVER_PORT 12345

#define LEFT_BUTTON_GPIO 9
#define LED_GPIO 8
#define NUM_LEDS 25

#define TAG "ESP32_WIFI_UDP"
// ====================================

typedef enum {
    BUTTON_PRESSED,
    BUTTON_RELEASED
} button_event_type_t;

typedef struct {
    button_event_type_t event;
    TickType_t timestamp;
} button_event_data_t;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static QueueHandle_t button_event_queue = NULL;
static led_strip_handle_t led_strip;

// ========== Wi-Fi Event Handler ==========

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying Wi-Fi...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Connect to the AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ========== Wi-Fi Setup ==========
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi init complete, waiting for connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to AP");
    } else {
        ESP_LOGE(TAG, "Unexpected Wi-Fi event");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            break;
        default:
            break;
    }
}

// ========== MQTT Sender ==========
void send_mqtt_event(const char *payload)
{
    printf("Sending data...\n");
    esp_mqtt_client_publish(client, "/esp32/button", payload, 0, 1, 0);
}

void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.0.196"
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// ========== UDP Sender ==========
void send_udp_event(const char *payload, size_t payloadLen)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed: errno %d", errno);
        return;
    }

    struct sockaddr_in dest_addr = {
        .sin_addr.s_addr = inet_addr(UDP_SERVER_IP),
        .sin_family = AF_INET,
        .sin_port = htons(UDP_SERVER_PORT),
    };

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));

    err = send(sock, payload, payloadLen, 0);
    shutdown(sock, 0);
    close(sock);
}

// ========== LED Strip Setup ==========
static void configure_led()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = NUM_LEDS,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

// ========== GPIO Button Setup ==========
static void configure_button()
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << LEFT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
}

// ========== Button Polling Task ==========
void check_button_task(void *pv)
{
    int prev = 1;
    while (true) {
        int curr = gpio_get_level(LEFT_BUTTON_GPIO);
        if (curr == 0 && prev == 1) {
            button_event_data_t evt = {BUTTON_PRESSED, xTaskGetTickCount()};
            xQueueSend(button_event_queue, &evt, 0);
        }
        if (curr == 1 && prev == 0) {
            button_event_data_t evt = {BUTTON_RELEASED, xTaskGetTickCount()};
            xQueueSend(button_event_queue, &evt, 0);
        }
        prev = curr;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ========== LED Task ==========
void led_task(void *pv)
{
    button_event_data_t button_pressed_event = {BUTTON_RELEASED, 0};
    while (true) {
        BaseType_t return_type = xQueueReceive(button_event_queue, &button_pressed_event, 3000 / portTICK_PERIOD_MS);

        if(return_type == pdTRUE){
            // log
            if(button_pressed_event.event == BUTTON_PRESSED) {
                ESP_LOGI(TAG, "Received pressed!");
            } else {
                ESP_LOGI(TAG, "Received released!");
            }

            // Prepare a simple payload
            char payload[64];
            // int payloadLen = snprintf(payload, sizeof(payload), 
            //          "event=%s;timestamp=%lu", 
            //          button_pressed_event.event == BUTTON_PRESSED ? "PRESSED" : "RELEASED",
            //          button_pressed_event.timestamp);
            sprintf(payload, "event=%s;timestamp=%lu", button_pressed_event.event == BUTTON_PRESSED ? "PRESSED" : "RELEASED", button_pressed_event.timestamp);
            send_mqtt_event(payload);

            if (button_pressed_event.event == BUTTON_PRESSED) {
                led_strip_set_pixel(led_strip, 25 / 2, 0, 50, 0);
            } else {
                led_strip_clear(led_strip);
            }
        } else {
            // Timeout: no event received in 3 seconds
            if (button_pressed_event.event == BUTTON_PRESSED) {
                for (int i = 0; i < 25; i++) {
                    led_strip_clear(led_strip);
                    led_strip_set_pixel(led_strip, i, 50, 0, 0);
                    led_strip_refresh(led_strip);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
            }
        }

        led_strip_refresh(led_strip);
    }
}

// ========== Main ==========
void app_main(void)
{
    ESP_LOGI(TAG, "Starting UDP button sender app...");

    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();
    mqtt_app_start();

    configure_led();
    configure_button();

    button_event_queue = xQueueCreate(10, sizeof(button_event_data_t));

    xTaskCreate(check_button_task, "check_button", 2048, NULL, 5, NULL);
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
}
