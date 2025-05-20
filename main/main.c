#include <stdio.h>
#include <stdbool.h>
#include "led_strip.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_wifi.h"


#define LEFT_BUTTON_GPIO 9

#define TASK_STACKSIZE 2048
#define TASK_PRIORITY 3

static led_strip_handle_t led_strip;

TaskHandle_t gLedTaskHandle = NULL;
TaskHandle_t gButtonCheckTaskHandle = NULL;

typedef enum {
    BUTTON_PRESSED,
    BUTTON_RELEASED
} button_event_type_t;

typedef struct {
    button_event_type_t event;
    TickType_t timestamp;
} button_event_data_t;

QueueHandle_t button_event_queue = NULL;

SemaphoreHandle_t s = NULL;

static void configureLED() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = 8,
        .max_leds = 25, 
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

static void configureGPIOButton() {
    gpio_config_t gpioConfigIn = {
        .pin_bit_mask = (1 << LEFT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&gpioConfigIn);
}

void checkButtonPress() {
    int previousButtonState = 1;
    int newButtonState = 0;
    
    while (true) {
        newButtonState = gpio_get_level(LEFT_BUTTON_GPIO);

        // check if we pressed the button
        if (newButtonState == 0 && previousButtonState == 1) {
            button_event_data_t event = {BUTTON_PRESSED, xTaskGetTickCount()};
            xQueueSend(button_event_queue, &event, 0);
        }

        // check if we released the button
        if (newButtonState == 1 && previousButtonState == 0) {
            button_event_data_t event = {BUTTON_RELEASED, xTaskGetTickCount()};
            xQueueSend(button_event_queue, &event, 0); 
        }

        previousButtonState = newButtonState;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void ledLightTask() {
    // create a button event data structure
    button_event_data_t button_pressed_event = {BUTTON_RELEASED, 0};

    while (true) {
        BaseType_t return_type = xQueueReceive(button_event_queue, &button_pressed_event, 3000 / portTICK_PERIOD_MS);

        if(return_type == pdTRUE) {
            // check if we pressed or released the button
            if(button_pressed_event.event == BUTTON_PRESSED) {
                led_strip_set_pixel(led_strip, 25/2, 0, 50, 0); // gloomy green light on middle led
            } else {
                led_strip_clear(led_strip);
            }
        }else {
            // we did not get an event in expected time, check if the last event was pressed
            if(button_pressed_event.event == BUTTON_PRESSED) {
                // do something special
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

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("WiFi disconnected, trying to reconnect...\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    esp_netif_t *netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "...",
            .password = "...",
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

}

void app_main(void)
{
    wifi_init_sta();
    // s = xSemaphoreCreateCounting(100, 0);
    // printf("Start of Program\n");

    // configureGPIOButton();
    // configureLED();

    // button_event_queue = xQueueCreate(100, sizeof(button_event_data_t));

    // xTaskCreate(checkButtonPress, "CHECK_BUTTON", TASK_STACKSIZE, NULL, TASK_PRIORITY, &gButtonCheckTaskHandle);
    // xTaskCreate(ledLightTask, "LED_LIGHT", TASK_STACKSIZE, NULL, TASK_PRIORITY, &gLedTaskHandle);
    // assert(gLedTaskHandle != NULL && gButtonCheckTaskHandle != NULL);
}