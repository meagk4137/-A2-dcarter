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

static pico_fatfs_spi_config_t sd_config = {
    spi0,
    CLK_SLOW_DEFAULT,
    12500 * KHZ,
    20,
    21,
    18,
    19,
    true
};

const float conversion_factor = 3.3f / (1 << 12);

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    printf(" ADC Example, measuring GPIO26\n");
    adc_init();
    adc_gpio_init(PHOTO_CELL_PIN);
    adc_gpio_init(TEMP_PIN);

    spi_init(spi0, 1000 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    uint8_t tx = 0xA5;

    pico_fatfs_set_config(&sd_config);
    static FATFS fs;
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK)
        printf("f_mount: %d\r\n", fr);

    static FIL fil;
    fr = f_open(&fil, "multi_source_cali_ref.csv", FA_OPEN_APPEND | FA_WRITE);
    if (fr != FR_OK)
        printf("f_open: %d\r\n", fr);

    UINT bw;
    if (f_size(&fil) == 0)
    {
        const char *header = "time_s, volts, lux, temp_C\r\n";
        f_write(&fil, header, strlen(header), &bw);
    }

    while (true)
    {
        adc_select_input(PHOTO_CELL_ADC);
        uint16_t result = adc_read();
        uint32_t sum = 0;
        for (int i = 0; i < 16; i++)
            sum += adc_read();

        float volts = result * conversion_factor;
        float r1 = 3.3f * R2_OHMS / volts - R2_OHMS;
        float lux = powf(10.0f, -1.67f * log10f(r1) + LUX_B);
        adc_select_input(TEMP_ADC);
        float t_volts = adc_read() * conversion_factor;
        float temp_lin = (1.8663f - t_volts) / 0.01169f;
        float temp_full = -1481.96f + sqrtf(2.1962e6f + (1.8639f - t_volts) / 3.88e-6f);
        float temp_c = temp_lin;
        float t_s = to_ms_since_boot(get_absolute_time()) / 1000.0;

        char line[64];
        int len = snprintf(line, sizeof line,
                           "%.3f, %.3f, %.7f, %.3f\r\n", t_s, volts, lux, temp_c);
        printf("%s", line);
        f_write(&fil, line, len, &bw);
        f_sync(&fil);

        sleep_ms(1000);
    }
}
