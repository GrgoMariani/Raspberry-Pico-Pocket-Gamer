#ifndef INTERFACE_H_
#define INTERFACE_H_

#define ONE_SECOND (1000*1000)

#include <stdio.h>
#include <stdint.h>

typedef enum
{
    KEY_NONE = 0x0000,
	KEY_UP = 0x0001,
	KEY_DOWN = 0x0002,
	KEY_LEFT = 0x0004,
	KEY_RIGHT = 0x0008,
	KEY_MENU = 0x0010,
} KeyPressedEnum;

void IF_Setup();
char IF_KeyPressed();
uint32_t IF_GetCurrentTime();
uint32_t IF_Random();

void IF_Keyboard(void);
void IF_DrawScreen(uint8_t *_framebuffer, size_t _framebufferSize);

uint8_t IF_Touchscreen();

#endif //INTERFACE_H_
