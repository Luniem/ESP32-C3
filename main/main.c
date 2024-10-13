#include <stdio.h>
#include <math.h>
#include "led_strip.h"
#include "driver/gpio.h"
#include "esp_random.h"

#define CYCLE_TIME 50
#define LED_GPIO 8
#define LEFT_BUTTON_GPIO 9
#define RIGHT_BUTTON_GPIO 2

#define LED_COUNT 25
#define BRIGHTNESS 100
#define SCRAMBLE_SIZE 5

#define BUTTON_DEBOUNCE 25

static int current_button_debounce = 0;
static int current_value = 0;

static led_strip_handle_t led_strip;

static int dice_values[6][LED_COUNT] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 1
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, // 2
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, // 3
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1}, // 4
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1}, // 5
    {1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1}, // 6
};

void next_step(void)
{
    if (current_button_debounce == 0)
    {
        int isClicked = gpio_get_level(LEFT_BUTTON_GPIO) == 0;
        if (isClicked)
        {
            // start scramble
            current_button_debounce = BUTTON_DEBOUNCE;
            current_value = esp_random() % 6;
        }
        else
        {
            led_strip_clear(led_strip);
            // number is shown
            for (int i = 0; i < LED_COUNT; i++)
            {
                if (dice_values[current_value][i] == 1)
                {
                    led_strip_set_pixel(led_strip, i, 0, 0, BRIGHTNESS);
                }
            }
        }
    }
    else
    {
        led_strip_clear(led_strip);
        int selected_leds[SCRAMBLE_SIZE];

        for (int i = 0; i < SCRAMBLE_SIZE; i++)
        {
            int selected_led;
            bool doesAlreadyExist;
            do
            {
                doesAlreadyExist = false;
                selected_led = esp_random() % LED_COUNT;
                for (int y = 0; y < i; y++)
                {
                    if (selected_leds[y] == selected_led)
                    {
                        doesAlreadyExist = true;
                        break;
                    }
                }

            } while (doesAlreadyExist);

            selected_leds[i] = selected_led;
            int value = floor(BRIGHTNESS * (current_button_debounce / (float)BUTTON_DEBOUNCE));
            led_strip_set_pixel(led_strip, selected_led, 0, 0, value);
        }
        current_button_debounce--;
    }

    /* Refresh the strip to send data */
    led_strip_refresh(led_strip);
}

static void configure_led_and_buttons(void)
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

void app_main(void)
{
    configure_led_and_buttons();

    while (1)
    {
        next_step();
        vTaskDelay(CYCLE_TIME / portTICK_PERIOD_MS);
    }
}