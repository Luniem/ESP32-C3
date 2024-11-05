#include <stdio.h>
#include <inttypes.h>
#include "led_strip.h"
#include "portmacro.h"
#include "math.h"
#include "driver/gpio.h"

#define CYCLE_TIME 1000
#define LED_GPIO 8
#define LEFT_BUTTON_GPIO 9
#define RIGHT_BUTTON_GPIO 2
#define BUTTON_DEBOUNCE 5

static int buttonDebounce = 200;

static led_strip_handle_t led_strip;

static int configuredTimeInSeconds = 60; // start with 1 minute
static int setTimeInSeconds = 0;
static bool isRunning = false;

static void configure_LED()
{
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 25, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);

    gpio_config_t gpioConfigIn = {
        .pin_bit_mask = (1 << LEFT_BUTTON_GPIO) | (1 << RIGHT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&gpioConfigIn);
}

static void displayRemainingTime()
{
    int minutes = configuredTimeInSeconds / 60;
    int seconds = configuredTimeInSeconds % 60;

    // minutes
    for (int i = 0; i < 5; i++)
    {
        if (minutes & (1 << (4 - i)))
        {
            led_strip_set_pixel(led_strip, i, 0, 0, 255);
        }
    }

    // seconds
    for (int i = 0; i < 5; i++)
    {
        if (seconds & (1 << (4 - i)))
        {
            led_strip_set_pixel(led_strip, i + 5, 0, 0, 255);
        }
    }
}

void static paintProgress()
{
    float progress = (float)(setTimeInSeconds - configuredTimeInSeconds) / setTimeInSeconds;
    int redValue = 255 - (255 * progress);
    int greenValue = 255 * progress;
    for (int y = 2; y < 5; y++)
    {
        for (int z = 0; z < 5; z++)
        {
            led_strip_set_pixel(led_strip, z + (y * 5), redValue, greenValue, 0);
        }
    }
}

void app_main(void)
{
    // time not really synced :( can we find a way to do that?
    configure_LED();

    while (1)
    {
        led_strip_clear(led_strip);
        if (!isRunning)
        {
            // time is not running | configuring time

            if (gpio_get_level(LEFT_BUTTON_GPIO) == 0)
            {
                // add a minute
                configuredTimeInSeconds = (configuredTimeInSeconds % (15 * 60)) + (60);

                vTaskDelay(buttonDebounce / portTICK_PERIOD_MS);
            }
            else if (gpio_get_level(RIGHT_BUTTON_GPIO) == 0)
            {
                setTimeInSeconds = configuredTimeInSeconds;
                paintProgress();
                isRunning = true;
            }

            displayRemainingTime();
        }
        else
        {
            // time is running
            configuredTimeInSeconds -= 1;

            if (configuredTimeInSeconds > 0)
            {
                // show decay
                paintProgress();
            }
            else
            {
                // make flashy thing
                isRunning = false;
                for (int i = 0; i < 20; i++)
                {
                    // rows 3 -5
                    for (int y = 2; y < 5; y++)
                    {
                        for (int z = 0; z < 5; z++)
                        {
                            led_strip_set_pixel(led_strip, z + (y * 5), 0, i % 2 == 0 ? 255 : 0, 0);
                        }
                    }
                    led_strip_refresh(led_strip);
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }

                // reset time
                configuredTimeInSeconds = 60;
            }
            displayRemainingTime();
        }
        led_strip_refresh(led_strip);

        if (isRunning)
        {
            vTaskDelay(CYCLE_TIME / portTICK_PERIOD_MS);
        }
        else
        {
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}