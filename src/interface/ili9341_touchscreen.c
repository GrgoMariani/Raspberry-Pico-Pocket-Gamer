#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "ili9341_touchscreen.h"
#include "../touch.h"

#define SPI_PORT spi1

#define PIN_MOSI 11
#define PIN_SCK  10
#define PIN_MISO 8
#define PIN_CS   21
#define PIN_IRQ  20

/*
 * The XPT2046 is specified for fCLK <= 2.5MHz at VCC = 2.7..5.25V. The driver
 * used to run at exactly that limit, which leaves no margin at all for the
 * wiring and shows up as noisy (imprecise) samples. 2MHz is well inside the
 * specification and the extra microseconds are irrelevant: a touch burst is a
 * couple of dozen bytes only.
 */
#define TOUCH_SPI_HZ 2000000

/*
 * Control bytes: S=1, 12 bit mode, differential reference, power down between
 * conversions. The channel that is called "X" here is the one that ends up in
 * the game's x coordinate (see the note in touch.c).
 */
#define READ_X 0x90
#define READ_Y 0xD0


void ILI9341_T_Init()
{
    spi_init(SPI_PORT, TOUCH_SPI_HZ);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    printf("Screen initialized\n");
    gpio_init(PIN_IRQ);
    gpio_set_dir(PIN_IRQ, GPIO_IN);
    // PENIRQ of the XPT2046 is open drain and active low, it needs a pull up
    // (many modules have one on the PCB, the internal one makes it work on
    //  boards without)
    gpio_pull_up(PIN_IRQ);
}

static inline void ILI9341_TouchSelect(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop");
}

static inline void ILI9341_TouchUnselect(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

static inline bool ILI9341_TouchPressed(void) {
    return (gpio_get(PIN_IRQ) == 0);
}

/* sends one control byte and clocks out the 12 bit result (left justified) */
static uint16_t ILI9341_TouchReadChannel(uint8_t channel)
{
    uint8_t command = channel;
    uint8_t result[2] = {0, 0};
    spi_write_blocking(SPI_PORT, &command, 1);
    spi_read_blocking(SPI_PORT, 0, result, 2);
    return (uint16_t)(((uint16_t)result[0] << 8) | (uint16_t)result[1]);
}

bool ILI9341_T_TouchGetCoordinates(uint16_t* x, uint16_t* y)
{
    uint16_t raw_x[TOUCH_SAMPLES_MAX];
    uint16_t raw_y[TOUCH_SAMPLES_MAX];
    uint8_t nsamples = 0;

    if (!ILI9341_TouchPressed())
        return false;

    ILI9341_TouchSelect();

    /*
     * With "power down between conversions" the internal reference is switched
     * off after every conversion, so the first conversion after chip select is
     * made before the reference has settled. It is thrown away.
     */
    ILI9341_TouchReadChannel(READ_Y);
    ILI9341_TouchReadChannel(READ_X);

    for (uint8_t i = 0; i < TOUCH_SAMPLES_MAX; i++)
    {
        if (!ILI9341_TouchPressed())
            break;
        raw_y[nsamples] = ILI9341_TouchReadChannel(READ_Y);
        raw_x[nsamples] = ILI9341_TouchReadChannel(READ_X);
        nsamples++;
    }

    ILI9341_TouchUnselect();

    /*
     * The old code required ALL samples to be taken while the panel was still
     * pressed and threw the whole touch away otherwise, so short or slightly
     * wobbling taps were simply lost. Whatever was sampled is used now.
     */
    return Touch_ComputeCoordinates(raw_x, raw_y, nsamples, x, y) ? true : false;
}
