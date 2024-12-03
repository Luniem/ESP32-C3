#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "portmacro.h"
#include "led_strip.h"
#include "math.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_random.h"

spi_device_interface_config_t spiDeviceConfig = { 0 };
spi_device_handle_t spiDeviceHandle;
static led_strip_handle_t led_strip;

bool isRolling = false;

uint16_t meanX = 0;
uint16_t meanY = 0;
uint16_t meanZ = 0;

static int Even[25] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
static int Odd[25] = { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};

bool changes[10] = {false, false, false, false, false, false, false, false, false, false};
uint8_t curr_change_index = 0;

uint16_t threshold = 1000;
uint16_t rolling_threshold = 4;

static int dice_values[6][25] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 1
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, // 2
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, // 3
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1}, // 4
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1}, // 5
    {1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1}, // 6
};

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
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

void initSPI(spi_host_device_t spiHost) {
	printf("init SPI\n");
	spi_bus_config_t bus_cfg = {
		.mosi_io_num = 4,
		.miso_io_num = 7,
		.sclk_io_num = 10,
		.quadwp_io_num = GPIO_NUM_NC,
		.quadhd_io_num = GPIO_NUM_NC,
		.max_transfer_sz = 0 // DMA default
	};
	esp_err_t res = spi_bus_initialize(spiHost, &bus_cfg, SPI_DMA_CH_AUTO);
	if (res != ESP_OK) {
		printf("spi_bus_initialize() FAILED\n");
		return;
	} else {
		printf("spi_bus_initialize() ok\n");
	}

    spiDeviceConfig.command_bits = 0;
    spiDeviceConfig.address_bits = 0;
    spiDeviceConfig.dummy_bits = 0;
    spiDeviceConfig.duty_cycle_pos = 128;
    spiDeviceConfig.mode = 0; // CPOL = 0, CPHA = 0
    spiDeviceConfig.clock_source = SPI_CLK_SRC_DEFAULT;
    spiDeviceConfig.clock_speed_hz = 500000; // 500 kbit/s
    spiDeviceConfig.spics_io_num = GPIO_NUM_1;
    spiDeviceConfig.flags = 0; //SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    spiDeviceConfig.queue_size = 3;
    spiDeviceConfig.cs_ena_posttrans = 3;
    spi_bus_add_device(spiHost, &spiDeviceConfig, &spiDeviceHandle);
}

bool read_who_am_i(){
    spi_transaction_t spiTransaction = { 0 };
    uint8_t cmd[16];
    cmd[0] = 0x75 | 0x80; // WHO AM I, Read
    cmd[1] = 0xFF; // second byte, for response transmit
    uint8_t rcv[16];
    rcv[0] = 0xCC;
    rcv[1] = 0xCC;
    spiTransaction.length = 8 * 2; // 8 bit send, 8 bit receive, only the register
    spiTransaction.rxlength = 0;
    spiTransaction.flags = 0; // SPI_TRANS_CS_KEEP_ACTIVE;
    spiTransaction.tx_buffer = &cmd;
    spiTransaction.rx_buffer = &rcv;

    esp_err_t ret = spi_device_transmit(spiDeviceHandle, &spiTransaction);

    if (ret == ESP_OK) {
        printf( "got data from ICM42688: %X %X\n", rcv[0], rcv[1]);
    }

    if (rcv[1] == 0x47) { // see datasheet
        printf("gyro responds, ID ok\n");
        return true;
    } else {
        printf("gyro responds, but wrong ID!\n");
    }

    return false;
}

uint8_t read_accel_data(uint8_t reg) {
    spi_transaction_t spiTransaction = { 0 };
    uint8_t cmd[16];

    cmd[0] = reg | 0x80;
    cmd[1] = 0xFF; // second byte, for response transmit

    uint8_t rcv[16];

    rcv[0] = 0xCC;
    rcv[1] = 0xCC;

    spiTransaction.length = 8 * 2; // 8 bit send, 8 bit receive, only the register
    spiTransaction.rxlength = 0;
    spiTransaction.flags = 0; // SPI_TRANS_CS_KEEP_ACTIVE;
    spiTransaction.tx_buffer = &cmd;
    spiTransaction.rx_buffer = &rcv;

    esp_err_t ret = spi_device_transmit(spiDeviceHandle, &spiTransaction);

    return rcv[1];
}

// int16_t read16BitRegister(uint8_t reg) {
//     uint8_t high = readRegister(reg);
//     uint8_t low = readRegister(reg + 1);
//     return (high << 8) | low;
// }

bool read_accelerometer(){
    uint8_t upper_x = read_accel_data(0x1f);
    uint8_t lower_x = read_accel_data(0x20);
    uint16_t x = ((upper_x << 8) | lower_x);

    uint8_t upper_y = read_accel_data(0x21);
    uint8_t lower_y = read_accel_data(0x22);
    uint16_t y = ((upper_y << 8) | lower_y);

    uint8_t upper_z = read_accel_data(0x23);
    uint8_t lower_z = read_accel_data(0x24);
    uint16_t z = ((upper_z << 8) | lower_z);

    if(meanX == 0 && meanY == 0 && meanZ == 0){
        meanX = x;
        meanY = y;
        meanZ = z;
    } else {
        uint16_t tempMeanX = (meanX + x) / 2;
        uint16_t tempmeanY = (meanY + y) / 2;
        uint16_t tempmeanZ = (meanZ + z) / 2;

        if(abs(meanX - x) > threshold){
            changes[curr_change_index] = true;
        }
        else if(abs(meanY - y) > threshold){
            changes[curr_change_index] = true;
        }
        else if(abs(meanZ - z) > threshold){
            changes[curr_change_index] = true;
        } else {
            changes[curr_change_index] = false;
        }
        curr_change_index = (curr_change_index + 1) % 10;

        meanX = tempMeanX;
        meanY = tempmeanY;
        meanZ = tempmeanZ;

        uint8_t count = 0;
        for (int i = 0; i < 10; i++) {
            if(changes[i]){
                count++;
            }
        }

        if(count > rolling_threshold){
            isRolling = true;
            for (int i = 0; i < 10; i++) {
                changes[i] = false;
            }
            meanX = 0;
            meanY = 0;
            meanZ = 0;
        }
    }

    return true;
}

esp_err_t write_register(uint8_t reg, uint8_t value) {
    spi_transaction_t spiTransaction = { 0 };

    // Command buffer: register address + value
    uint8_t cmd[2];
    cmd[0] = reg & 0x7F;  // Clear MSB for write operation
    cmd[1] = value;       // Data byte to write

    spiTransaction.length = 8 * 2;          // Two bytes: 1 address, 1 data
    spiTransaction.tx_buffer = cmd;        // Send the command and data buffer
    spiTransaction.rx_buffer = NULL;       // No data expected to be received

    // Transmit the SPI transaction
    esp_err_t ret = spi_device_transmit(spiDeviceHandle, &spiTransaction);
    if (ret != ESP_OK) {
        printf("SPI write failed: %d\n", ret);
        return ret;
    }

    printf("Wrote 0x%02X to register 0x%02X\n", value, reg);
    return ESP_OK;
}

void app_main(void)
{
    initSPI(SPI2_HOST);
    configure_LED();
    bool isOk = read_who_am_i();
    if(isOk){
        write_register(0x4E, 0x0F);
        write_register(0x50, 0x00);

        while(true) {

            if(!isRolling) {
                read_accelerometer();
            } else {
                // rolling animation
                for (int i = 0; i < 5; i++) {
                    for (int i = 0; i < 25; i++)
                    {
                        if(Even[i]){
                            led_strip_set_pixel(led_strip, i, 50, 0, 0);
                        } else {
                            led_strip_set_pixel(led_strip, i, 0, 0, 0);
                        }
                    }
                    led_strip_refresh(led_strip);
                    vTaskDelay(250 / portTICK_PERIOD_MS);
                    for (int i = 0; i < 25; i++)
                    {
                        if(Odd[i]){
                            led_strip_set_pixel(led_strip, i, 50, 0, 0);
                        } else {
                            led_strip_set_pixel(led_strip, i, 0, 0, 0);
                        }
                    }
                    led_strip_refresh(led_strip);
                    vTaskDelay(250 / portTICK_PERIOD_MS);
                }
                led_strip_clear(led_strip);

                // do rolling
                int current_value = esp_random() % 6;
                for (int i = 0; i < 25; i++)
                {
                    if (dice_values[current_value][i] == 1)
                    {
                        led_strip_set_pixel(led_strip, i, 0, 0, 50);
                    }
                }
                led_strip_refresh(led_strip);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                led_strip_clear(led_strip);
                isRolling = false;
            }
            vTaskDelay(250 / portTICK_PERIOD_MS);
        }
    }
}