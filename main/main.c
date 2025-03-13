#include <stdio.h>
#include <stdbool.h>
#include "led_strip.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>

#define LEFT_BUTTON_GPIO 9

#define TASK_STACKSIZE 2048
#define TASK_PRIORITY 3

static led_strip_handle_t led_strip;

TaskHandle_t gLedTaskHandle = NULL;
TaskHandle_t gButtonCheckTaskHandle = NULL;

bool lightLED = false;

SemaphoreHandle_t s = NULL;

static void configureLED() {
        /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = 8,
        .max_leds = 25, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
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
    while (true) {
        bool pressed = gpio_get_level(LEFT_BUTTON_GPIO) == 0;
        bool shouldTurnOn = pressed && !lightLED;
        bool shouldTurnOff = !pressed && lightLED;

        if (shouldTurnOn || shouldTurnOff) {
            if(shouldTurnOn) {
                lightLED = true;
            } else {
                lightLED = false;
            }

            xSemaphoreGive(s);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void ledLightTask() {
    while (true) {
        xSemaphoreTake(s, portMAX_DELAY);
        printf("T\n");

        if(lightLED) {
            led_strip_set_pixel(led_strip, 25/2, 0, 50, 0); // gloomy green light on middle led
        } else {
            led_strip_clear(led_strip);
        }
        
        led_strip_refresh(led_strip);
    }
}

void app_main(void)
{
    s = xSemaphoreCreateCounting(100, 0);
    printf("Start of Program\n");

    configureGPIOButton();
    configureLED();

    xTaskCreate(checkButtonPress, "CHECK_BUTTON", TASK_STACKSIZE, NULL, TASK_PRIORITY, &gButtonCheckTaskHandle);
    xTaskCreate(ledLightTask, "LED_LIGHT", TASK_STACKSIZE, NULL, TASK_PRIORITY, &gLedTaskHandle);
    assert(gLedTaskHandle != NULL && gButtonCheckTaskHandle != NULL);
}