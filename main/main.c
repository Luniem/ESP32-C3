#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "led_strip.h"
#include "portmacro.h"
#include "math.h"
#include "driver/gpio.h"
#include <driver/i2c.h>


static led_strip_handle_t led_strip;
spi_device_handle_t spi;

static int OK[25] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0};
static int BAD[25] = { 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1};
static int* mode = OK;

static uint8_t compare_right = 0x7;

static void configure_LED() {
        /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = 8,
        .max_leds = 25, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

void initI2C(i2c_port_t i2c_num) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 5,
        .scl_io_num = 6,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };

    i2c_param_config(i2c_num, &conf);
    ESP_ERROR_CHECK(i2c_driver_install(i2c_num, conf.mode, 0, 0, 0));
}

void wake_up() {
    uint8_t buf[32] =  {0x35, 0x17};
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, 0x70, buf, 2, pdMS_TO_TICKS(50)));
}

void read_id_register() {
    uint8_t data[2];
    uint8_t buf[2] = {0xEF, 0xC8};
    ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, 0x70, buf, 2, &data, 2, pdMS_TO_TICKS(1000)));
    uint16_t product_code = (data[0] << 8) | data[1];
    printf("Product code: %d\n", product_code);
}

void read_temperature_and_rh() {
    uint8_t data[6];
    uint8_t buf[2] = {0x58, 0xE0};

    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, 0x70, buf, 2, pdMS_TO_TICKS(50)));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_err_t err =i2c_master_read_from_device(I2C_NUM_0, 0x70, data, 6, pdMS_TO_TICKS(50));
        // esp_err_t err = i2c_master_write_read_device(I2C_NUM_0, 0x70, buf, 2, &data, 6, pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            break;
        }
        if (err == ESP_FAIL) {
            printf("Failed to read data\n");
        } else {
            printf("Alles kaputt\n");
        }
    }
    uint16_t rh = data[0] << 8 | data[1];
    uint16_t temp = data[3] << 8 | data[4];

    float_t calc_rhd = (float)rh / (1 << 16) * (float)100;
    float_t calc_temp = (float)temp / (1 << 16) * (float)175 - (float)45;

    printf("Relative Luftfeuchtigkeit: %.2f\n", calc_rhd);
    printf("Temperatur: %.2f\n", calc_temp);
}

void app_main(void)
{
    configure_LED();
    i2c_port_t i2c_num = I2C_NUM_0;
    initI2C(i2c_num);

    while (1) {
        wake_up();

        // read_id_register();
        read_temperature_and_rh();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    

}