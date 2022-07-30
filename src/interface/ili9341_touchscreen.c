#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "ili9341_touchscreen.h"

#define SPI_PORT spi1

#define PIN_MOSI 11
#define PIN_SCK  10
#define PIN_MISO 8
#define PIN_CS   21
#define PIN_IRQ  20

// change depending on screen orientation
#define ILI9341_TOUCH_SCALE_X 320
#define ILI9341_TOUCH_SCALE_Y 240

#define ILI9341_TOUCH_MIN_RAW_X 1500
#define ILI9341_TOUCH_MAX_RAW_X 31000
#define ILI9341_TOUCH_MIN_RAW_Y 3276
#define ILI9341_TOUCH_MAX_RAW_Y 30110

#define READ_X 0x90
#define READ_Y 0xD0


void ILI9341_T_Init()
{
    spi_init(SPI_PORT, 2500000);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);


    printf("Screen initialized\n");
    gpio_init(PIN_IRQ);
    gpio_set_dir(PIN_IRQ, GPIO_IN);
    //gpio_pull_up(PIN_IRQ);
    gpio_pull_down(PIN_IRQ);
}

static inline void ILI9341_TouchSelect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop");
}

static void inline ILI9341_TouchUnselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

static void inline send_bytes(uint8_t * data, uint8_t len) {
    spi_write_blocking(SPI_PORT, data, len);
}

static bool inline ILI9341_TouchPressed() {
    return (gpio_get(PIN_IRQ) == 0);
}

#define MAX_SAMPLES 8

bool ILI9341_T_TouchGetCoordinates(uint16_t* x, uint16_t* y) {

    static const uint8_t cmd_read_x[] = { READ_X };
    static const uint8_t cmd_read_y[] = { READ_Y };
     uint8_t zeroes_tx[] = { 0x00, 0x00 };

    ILI9341_TouchSelect();

    uint32_t avg_x = 0;
    uint32_t avg_y = 0;
    uint8_t nsamples = 0;
    for(uint8_t i = 0; i < MAX_SAMPLES; i++) {
        if(!ILI9341_TouchPressed())
            break;

        nsamples++;

        send_bytes( (uint8_t*)cmd_read_y, 1);
        uint8_t y_raw[2] = {0, 0};
        spi_read_blocking(SPI_PORT, 0, y_raw, 2);

        send_bytes( (uint8_t*)cmd_read_x, 1);
        uint8_t x_raw[2] = {0, 0};
        spi_read_blocking(SPI_PORT, 0, x_raw, 2);

        avg_x += (((uint16_t)x_raw[0]) << 8) | ((uint16_t)x_raw[1]);
        avg_y += (((uint16_t)y_raw[0]) << 8) | ((uint16_t)y_raw[1]);
    }

    ILI9341_TouchUnselect();

    if(nsamples < MAX_SAMPLES)
        return false;

    uint32_t raw_x = (avg_x / MAX_SAMPLES);
    if(raw_x < ILI9341_TOUCH_MIN_RAW_X) raw_x = ILI9341_TOUCH_MIN_RAW_X;
    if(raw_x > ILI9341_TOUCH_MAX_RAW_X) raw_x = ILI9341_TOUCH_MAX_RAW_X;

    uint32_t raw_y = (avg_y / MAX_SAMPLES);
    if(raw_y < ILI9341_TOUCH_MIN_RAW_X) raw_y = ILI9341_TOUCH_MIN_RAW_Y;
    if(raw_y > ILI9341_TOUCH_MAX_RAW_Y) raw_y = ILI9341_TOUCH_MAX_RAW_Y;

    *x = (raw_x - ILI9341_TOUCH_MIN_RAW_X) * ILI9341_TOUCH_SCALE_X / (ILI9341_TOUCH_MAX_RAW_X - ILI9341_TOUCH_MIN_RAW_X);
    *y = (raw_y - ILI9341_TOUCH_MIN_RAW_Y) * ILI9341_TOUCH_SCALE_Y / (ILI9341_TOUCH_MAX_RAW_Y - ILI9341_TOUCH_MIN_RAW_Y);

    if (*y > ILI9341_TOUCH_SCALE_Y)
        *y = 0;
    if (*x > ILI9341_TOUCH_SCALE_X)
        *x = 0;
    return true;
}
