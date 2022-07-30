#ifndef ILI_9341_H_
#define ILI_9341_H_

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"


#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define SCREEN_TOTAL_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)
#define BUFFER_SIZE (SCREEN_TOTAL_PIXELS * 2)


void ili9341_init_display();
void ili9341_init_SPI();
void ili9341_init_drawing();

// add interface for buffer
void ili9341_write_buffer(uint8_t * buffer, uint32_t size_of_buffer);


#endif//ILI_9341_H_
