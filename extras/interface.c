#include "interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>


static uint32_t startTime = 0;

void IF_Setup()
{
    struct timeval stop;
    gettimeofday(&stop, NULL);
    startTime = stop.tv_sec * 1000000 + stop.tv_usec;
}

uint32_t IF_GetCurrentTime()
{
    struct timeval stop;
    gettimeofday(&stop, NULL);
    return stop.tv_sec * 1000000 + stop.tv_usec - startTime;
}

uint32_t IF_Random()
{
    uint32_t now = IF_GetCurrentTime();
    uint32_t result = ((uint32_t)(142*now * now + 13*now + 203) >> 4);
    return result;
}

#include "gameshared.h"
#include "mainLoop.h"


int GetPressedKey();
void GetPressedMouse();

void IF_Keyboard(void)
{
	KeyPressedEnum res = GetPressedKey();
	if (res & KEY_MENU)	Key_Down(KEY_MENU);
	else Key_Up(KEY_MENU);
	
	if (res & KEY_UP)	Key_Down(KEY_UP);
	else Key_Up(KEY_UP);
	
	if (res & KEY_LEFT)	Key_Down(KEY_LEFT);
	else Key_Up(KEY_LEFT);
	
	if (res & KEY_DOWN)	Key_Down(KEY_DOWN);
	else Key_Up(KEY_DOWN);
	
	if (res & KEY_RIGHT) Key_Down(KEY_RIGHT);
	else Key_Up(KEY_RIGHT);
}

uint8_t IF_Touchscreen()
{
	GetPressedMouse();
	return 0;
}

#include "gpu.h"


static uint16_t _finishedFramebuffer[GPU_X*GPU_Y];

void IF_DrawScreen(uint8_t * _framebuffer, size_t _framebufferSize)
{
	memcpy(_finishedFramebuffer, _framebuffer, _framebufferSize);
}

GPU_Color GetPixel(uint16_t x, uint16_t y)
{
	GPU_Color result = _finishedFramebuffer[(GPU_X-x-1)*GPU_Y+y];
	result = (result >> 8) | ((result & 0xff )<<8);
	return result;
}
