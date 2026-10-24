#include "interface.h"

#include <stdio.h>
#include <stdlib.h>

#include "gameshared.h"
#include "interface/ili9341.h"
#include "interface/ili9341_touchscreen.h"

#include "pico/time.h"
#include "hardware/structs/rosc.h"

#define KEY_PIN_MENU 0
#define KEY_PIN_UP 15
#define KEY_PIN_DOWN 27
#define KEY_PIN_LEFT 28
#define KEY_PIN_RIGHT 14

/*
 * The ring oscillator of the RP2040 delivers a few genuinely unpredictable
 * bits. They are mixed into the seed of the random generator together with the
 * (microsecond resolution) boot time, so that a game does not play out the same
 * way on every power up.
 */
static uint32_t _hardwareEntropy(void)
{
    /* the ring oscillator delivers one genuinely random bit per read */
    uint32_t entropy = 0;
    for (uint8_t i = 0; i < 32; i++)
        entropy = (entropy << 1) | (rosc_hw->randombit & 1u);
    return entropy;
}

void IF_Setup()
{
	ili9341_init_SPI();
	ili9341_init_display();
	ili9341_init_drawing();
	ILI9341_T_Init();

    gpio_init(KEY_PIN_MENU);
	gpio_init(KEY_PIN_UP);
	gpio_init(KEY_PIN_DOWN);
	gpio_init(KEY_PIN_LEFT);
	gpio_init(KEY_PIN_RIGHT);
    gpio_set_dir(KEY_PIN_MENU, GPIO_IN);
	gpio_set_dir(KEY_PIN_UP, GPIO_IN);
	gpio_set_dir(KEY_PIN_DOWN, GPIO_IN);
	gpio_set_dir(KEY_PIN_LEFT, GPIO_IN);
	gpio_set_dir(KEY_PIN_RIGHT, GPIO_IN);
    gpio_pull_up(KEY_PIN_MENU);
	gpio_pull_up(KEY_PIN_UP);
	gpio_pull_up(KEY_PIN_DOWN);
	gpio_pull_up(KEY_PIN_LEFT);
	gpio_pull_up(KEY_PIN_RIGHT);

	Random_Seed(IF_GetCurrentTime() ^ _hardwareEntropy() ^ (uint32_t)(uintptr_t)&IF_Setup);
}

uint32_t IF_GetCurrentTime()
{
	return to_us_since_boot(get_absolute_time());
}

#include "gpu.h"

void IF_DrawScreen(uint8_t * _framebuffer, size_t _framebufferSize)
{
	ili9341_write_buffer(_framebuffer, _framebufferSize);
}

uint32_t IF_Random()
{
    return Random_Next();
}

void IF_Keyboard(void)
{
	// Debouncing is done in gameshared, these are the raw switch levels.
	if (gpio_get(KEY_PIN_MENU) == 0)
		Key_Down(KEY_MENU);
	else
		Key_Up(KEY_MENU);
	if (gpio_get(KEY_PIN_LEFT) == 0)
		Key_Down(KEY_LEFT);
	else
		Key_Up(KEY_LEFT);
	if (gpio_get(KEY_PIN_RIGHT) == 0)
		Key_Down(KEY_RIGHT);
	else
		Key_Up(KEY_RIGHT);
	if (gpio_get(KEY_PIN_UP) == 0)
		Key_Down(KEY_UP);
	else
		Key_Up(KEY_UP);
	if (gpio_get(KEY_PIN_DOWN) == 0)
		Key_Down(KEY_DOWN);
	else
		Key_Up(KEY_DOWN);
}

uint8_t IF_Touchscreen()
{
	uint16_t x = 0;
	uint16_t y = 0;
	uint8_t pressed = ILI9341_T_TouchGetCoordinates(&x, &y) ? 1 : 0;
	Touch_Update(pressed, x, y);
	return pressed;
}
