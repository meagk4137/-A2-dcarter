#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <string.h>
#include <math.h>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "ff.h"
#include "tf_card.h"

#define PHOTO_CELL_PIN 26
#define PHOTO_CELL_ADC 0
#define R2_OHMS 7500.0f
#define LUX_B 7.2723f
#define TEMP_PIN 27
#define TEMP_ADC 1

#define PIN_SCK 18
#define PIN_TX 19
#define PIN_RX 20
#define PIN_CS 21

#define ADC_VREF_VOLTS 3.3f
#define ADC_RESOLUTION_BITS 12
#define ADC_READING_COUNT 16
#define STARTUP_DELAY_MS 2000
#define SPI_CLOCK_SPEED_HZ 1000 * 1000
#define LINE_BUFFER_SIZE 64
#define SAMPLE_DELAY_MS 1000
#define SD_SPI_CLOCK_SPEED_KHZ 12500
#define MOUNT_MODE 1
#define FILE_OPEN_FLAGS (FA_OPEN_APPEND | FA_WRITE)
#define TEMP_CALIB_A 1.8663f
#define TEMP_CALIB_B 0.01169f
#define TEMP_FULL_OFFSET -1481.96f
#define TEMP_FULL_SCALE 2.1962e6f
#define TEMP_FULL_SLOPE 1.8639f
#define TEMP_FULL_DELTA 3.88e-6f
#define LUX_EXPONENT -1.67f

static pico_fatfs_spi_config_t sd_config = {
    spi0,
    CLK_SLOW_DEFAULT,
    SD_SPI_CLOCK_SPEED_KHZ * KHZ,
    PIN_RX,
    PIN_CS,
    PIN_SCK,
    PIN_TX,
    true
};

const float conversion_factor = ADC_VREF_VOLTS / (1 << ADC_RESOLUTION_BITS);

int main()
{
    stdio_init_all();
    sleep_ms(STARTUP_DELAY_MS);

    printf(" ADC Example, measuring GPIO26\n");
    adc_init();
    adc_gpio_init(PHOTO_CELL_PIN);
    adc_gpio_init(TEMP_PIN);

    spi_init(spi0, SPI_CLOCK_SPEED_HZ);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    uint8_t tx = 0xA5;

    pico_fatfs_set_config(&sd_config);
    static FATFS fatfs;
    FRESULT fatfs_result = f_mount(&fatfs, "", MOUNT_MODE);
    if (fatfs_result != FR_OK)
        printf("f_mount: %d\r\n", fatfs_result);

    static FIL file_handle;
    fatfs_result = f_open(&file_handle, "multi_source_cali_ref.csv", FILE_OPEN_FLAGS);
    if (fatfs_result != FR_OK)
        printf("f_open: %d\r\n", fatfs_result);

    UINT bytes_written;
    if (f_size(&file_handle) == 0)
    {
        const char *header = "time_s, volts, lux, temp_C\r\n";
        f_write(&file_handle, header, strlen(header), &bytes_written);
    }

    while (true)
    {
        adc_select_input(PHOTO_CELL_ADC);
        uint16_t adc_raw = adc_read();
        uint32_t adc_sum = 0;
        for (int i = 0; i < ADC_READING_COUNT; i++)
            adc_sum += adc_read();

        float sensor_voltage_volts = adc_raw * conversion_factor;
        float sensor_resistance_ohms = ADC_VREF_VOLTS * R2_OHMS / sensor_voltage_volts - R2_OHMS;
        float lux_value = powf(10.0f, LUX_EXPONENT * log10f(sensor_resistance_ohms) + LUX_B);
        adc_select_input(TEMP_ADC);
        float temp_voltage_volts = adc_read() * conversion_factor;
        float temp_celsius_linear = (TEMP_CALIB_A - temp_voltage_volts) / TEMP_CALIB_B;
        float temp_celsius_full_range = TEMP_FULL_OFFSET + sqrtf(TEMP_FULL_SCALE + (TEMP_FULL_SLOPE - temp_voltage_volts) / TEMP_FULL_DELTA);
        float temp_celsius = temp_celsius_linear;
        float time_seconds = to_ms_since_boot(get_absolute_time()) / 1000.0;

        char log_line[LINE_BUFFER_SIZE];
        int line_length = snprintf(log_line, sizeof log_line,
                           "%.3f, %.3f, %.7f, %.3f\r\n", time_seconds, sensor_voltage_volts, lux_value, temp_celsius);
        printf("%s", log_line);
        f_write(&file_handle, log_line, line_length, &bytes_written);
        f_sync(&file_handle);

        sleep_ms(SAMPLE_DELAY_MS);
    }
}

