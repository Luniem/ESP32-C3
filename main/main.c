#include <stdio.h>
#include <inttypes.h>
#include "led_strip.h"
#include "portmacro.h"
#include "math.h"
#include "driver/gpio.h"

#define CYCLE_TIME 50
#define LED_GPIO 8
#define LEFT_BUTTON_GPIO 9
#define RIGHT_BUTTON_GPIO 2
#define BUTTON_DEBOUNCE 5


static int current_button_debounce = 0;

static led_strip_handle_t led_strip;

static int OK[25] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0};
static int BAD[25] = { 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1};
static int* mode = OK;

#define SQUARE(x) ((x) * (x))

#define RNG_BASE 0x60026000
#define RNG_DATA_REG_OFFS 0xB0

volatile uint32_t *pRngDataReg = (volatile uint32_t *)(RNG_BASE | RNG_DATA_REG_OFFS);

inline uint32_t nextRand()
{
    return *pRngDataReg;
}

bool equalDistChi2(const uint32_t n[], uint32_t m, uint32_t n0, uint32_t chi2)
{
    uint32_t squaresum = 0;
    for (int i = 0; i < m; i += 1)
    {
        squaresum += SQUARE(n[i] - n0);
    }

    uint32_t x2 = squaresum / n0;
    return (x2 <= chi2);
}

static bool testRandomness(uint32_t dice_value) {
    uint32_t *n = calloc(dice_value, sizeof(uint32_t));
    if (n == NULL)
    {
        printf("No memory :( \n");
    }
    else
    {

        for (int i = 0; i < 10000000; i++)
        {
            n[nextRand() % dice_value] += 1;
        }
        for (int i = 0; i < dice_value; i += 1)
        {
            printf("%d : %" PRIu32 "\n", i + 1, n[i]);
        }

        bool ok = equalDistChi2(n, dice_value, 10000000 / dice_value, 9);
        free(n);
        n = NULL;
        return ok;
    }

    return false;
}

static void configure_LED() {
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
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&gpioConfigIn);
}

void app_main(void)
{
    configure_LED();

    while (1) {
        int isClicked = gpio_get_level(LEFT_BUTTON_GPIO) == 0;
        if(isClicked) {
            for(int i =0; i< 25; i++) {
                led_strip_set_pixel(led_strip, i, 0, 10, 0);
            }
            led_strip_refresh(led_strip);

            bool ok = testRandomness(6);
            if(ok) {
                mode = OK;
            } else {
                mode = BAD;
            }

            for (int i = 0; i < 25; i++) {
                
                if(mode[i] == 0) {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                } else {
                    led_strip_set_pixel(led_strip, i, 0, 0, 50);
                }
            }
            led_strip_refresh(led_strip);
        }

        vTaskDelay(CYCLE_TIME / portTICK_PERIOD_MS);
    }

    
}