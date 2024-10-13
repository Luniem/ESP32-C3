#include <stdio.h>
#include "led_strip.h"
#include "driver/gpio.h"
#include "esp_random.h"

#define CYCLE_TIME 10
#define LED_GPIO 8
#define LEFT_BUTTON_GPIO 9
#define RIGHT_BUTTON_GPIO 2

#define LED_COUNT 25
#define DEFAULT_LED_SIZE 8

#define DEACTIVATED_SLOT 31
#define BUTTON_DEBOUNCE 30
static int button_debounce = BUTTON_DEBOUNCE;

static led_strip_handle_t led_strip;

static uint8_t current_led_size = DEFAULT_LED_SIZE;
static uint8_t selectedPins[LED_COUNT];

struct rgb_state
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int debounce;
};
static struct rgb_state rgb_states[LED_COUNT];

static void init_specific_led(uint8_t pin_index)
{
    rgb_states[pin_index].r = esp_random() % 150;
    rgb_states[pin_index].g = esp_random() % 150;
    rgb_states[pin_index].b = esp_random() % 150;
}

static bool pin_already_selected(uint8_t selected_pin)
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        if (selectedPins[i] == selected_pin)
        {
            return true;
        }
    }

    return false;
}

static void remove_led_from_index(uint8_t index_to_remove)
{
    selectedPins[index_to_remove] = DEACTIVATED_SLOT;
}

static void pick_new_led()
{
    int free_index = -1;
    for (int i = 0; i < LED_COUNT; i++)
    {
        if (selectedPins[i] == DEACTIVATED_SLOT)
        {
            free_index = i;
            break;
        }
    }

    if (free_index != -1)
    {
        uint8_t selected_number;
        bool already_exists = false;
        do
        {
            selected_number = (uint8_t)(esp_random() % 25);
            already_exists = pin_already_selected(selected_number);
        } while (already_exists);

        selectedPins[free_index] = selected_number;

        init_specific_led(selected_number);
    }
}

static void next_step(void)
{
    int curr_button_debounce = button_debounce;
    led_strip_clear(led_strip);

    int leftButton = gpio_get_level(LEFT_BUTTON_GPIO);
    int rightButton = gpio_get_level(RIGHT_BUTTON_GPIO);

    // check buttons
    if (!leftButton || !rightButton || button_debounce != 0)
    {
        if (curr_button_debounce == 0)
        {
            if (leftButton == 0 && rightButton != 0)
            {
                // left clicked
                if (current_led_size > 0)
                {
                    current_led_size--;
                    button_debounce = BUTTON_DEBOUNCE;
                    curr_button_debounce = button_debounce;
                }
            }
            else if (rightButton == 0 && leftButton != 0)
            {
                // right clicked
                if (current_led_size < LED_COUNT)
                {
                    current_led_size++;
                    button_debounce = BUTTON_DEBOUNCE;
                    curr_button_debounce = button_debounce;
                }
            }
        }
        if (curr_button_debounce != 0)
        {
            button_debounce--;

            for (int i = 0; i < LED_COUNT; i++)
            {
                if (i < current_led_size)
                {
                    // active
                    led_strip_set_pixel(led_strip, i, 255, 0, 0);
                }
                else
                {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
        }
    }

    if (curr_button_debounce == 0)
    {
        int still_activated_leds = 0;
        for (int i = 0; i < LED_COUNT; i++)
        {
            int curr_led = selectedPins[i];

            // is the current slot an active led?
            if (curr_led != DEACTIVATED_SLOT)
            {
                // get the state
                struct rgb_state *curr_state = &rgb_states[curr_led];

                led_strip_set_pixel(led_strip, curr_led, curr_state->r, curr_state->g, curr_state->b);

                // start updating just after some debounce time
                if (curr_state->debounce > 0)
                {
                    curr_state->debounce = curr_state->debounce - 1;
                    still_activated_leds++;
                }
                else
                {
                    if (curr_state->r > 0)
                    {
                        curr_state->r = curr_state->r - 1;
                    }

                    if (curr_state->g > 0)
                    {
                        curr_state->g = curr_state->g - 1;
                    }

                    if (curr_state->b > 0)
                    {
                        curr_state->b = curr_state->b - 1;
                    }

                    if (curr_state->r == 0 && curr_state->g == 0 && curr_state->b == 0)
                    {
                        remove_led_from_index(i);
                    }
                    else
                    {
                        still_activated_leds++;
                    }
                }
            }
        }

        int missing_leds = current_led_size - still_activated_leds;
        for (int i = 0; i < missing_leds; i++)
        {
            pick_new_led();
        }
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

static void init_led_states(void)
{
    // write manager for all as deactivated
    for (int i = 0; i < LED_COUNT; i++)
    {
        selectedPins[i] = DEACTIVATED_SLOT;
    }

    // search random led to start
    for (int i = 0; i < current_led_size; i++)
    {
        uint8_t selected_number;
        bool already_exists = false;
        do
        {
            selected_number = (uint8_t)(esp_random() % 25);
            already_exists = pin_already_selected(selected_number);
        } while (already_exists);

        selectedPins[i] = selected_number;
    }

    int debounce = 0;
    for (int i = 0; i < LED_COUNT; i++)
    {
        if (selectedPins[i] != DEACTIVATED_SLOT)
        {
            init_specific_led(selectedPins[i]);
            rgb_states[selectedPins[i]].debounce = debounce;
            debounce += 30;
        }
    }
}

void app_main(void)
{
    configure_led_and_buttons();
    init_led_states();

    while (1)
    {
        next_step();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}