#include "interface.h"
#include "gameshared.h"
#include "mainLoop.h"
#include "gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>


static uint32_t startTime = 0;

/*
 * Test hooks: the test runner in extras/tests drives the games with a virtual
 * clock so that a run is reproducible. The emulator itself never touches them.
 */
static uint8_t useVirtualTime = 0;
static uint32_t virtualNow = 0;
static uint32_t autoAdvance = 0;

void Host_UseVirtualTime(uint8_t on) { useVirtualTime = on; }
void Host_SetVirtualTime(uint32_t us) { virtualNow = us; }
void Host_AdvanceVirtualTime(uint32_t us) { virtualNow += us; }
uint32_t Host_GetVirtualTime(void) { return virtualNow; }
void Host_SetTimeAutoAdvance(uint32_t usPerCall) { autoAdvance = usPerCall; }

void IF_Setup()
{
    struct timeval stop;
    gettimeofday(&stop, NULL);
    startTime = (uint32_t)(stop.tv_sec * 1000000 + stop.tv_usec);
    Random_Seed(startTime ^ (uint32_t)getpid());
}

uint32_t IF_GetCurrentTime()
{
    if (useVirtualTime)
    {
        uint32_t now = virtualNow;
        virtualNow += autoAdvance;
        return now;
    }
    struct timeval stop;
    gettimeofday(&stop, NULL);
    return (uint32_t)(stop.tv_sec * 1000000 + stop.tv_usec) - startTime;
}

uint32_t IF_Random()
{
    return Random_Next();
}

int GetPressedKey();
void GetPressedMouse(uint8_t * pressed, uint16_t * x, uint16_t * y);

void IF_Keyboard(void)
{
	KeyPressedEnum res = (KeyPressedEnum)GetPressedKey();
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
	uint8_t pressed = 0;
	uint16_t x = 0;
	uint16_t y = 0;
	GetPressedMouse(&pressed, &x, &y);
	Touch_Update(pressed, x, y);
	return pressed;
}


static uint16_t _finishedFramebuffer[GPU_X*GPU_Y];

void IF_DrawScreen(uint8_t * _framebuffer, size_t _framebufferSize)
{
	memcpy(_finishedFramebuffer, _framebuffer, _framebufferSize);
}

GPU_Color GetPixel(uint16_t x, uint16_t y)
{
	GPU_Color result = _finishedFramebuffer[(GPU_X-x-1)*GPU_Y+y];
	// the framebuffer holds byte swapped RGB565, the PGE wants plain RGB565
	result = (GPU_Color)((result >> 8) | ((result & 0xff )<<8));
	return result;
}
