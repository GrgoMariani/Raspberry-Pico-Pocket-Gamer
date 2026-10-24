#ifndef HOST_HOOKS_H_
#define HOST_HOOKS_H_

#include <stdint.h>

/* implemented by extras/interface.c, used by the test runner */
void Host_UseVirtualTime(uint8_t on);
void Host_SetVirtualTime(uint32_t us);
void Host_AdvanceVirtualTime(uint32_t us);
uint32_t Host_GetVirtualTime(void);
/* when set, every call to IF_GetCurrentTime() moves the virtual clock forward,
   which lets the real DoMainLoop() run without a real time source */
void Host_SetTimeAutoAdvance(uint32_t usPerCall);

/* implemented by the platform layer (main.cpp for the emulator, tests.c for
   the test runner): the current state of the five switches and of the touch */
int GetPressedKey(void);
void GetPressedMouse(uint8_t * pressed, uint16_t * x, uint16_t * y);

/* implemented by extras/interface.c: read back one pixel of the last rendered
   framebuffer (plain RGB565) */
typedef uint16_t GPU_Color;
GPU_Color GetPixel(uint16_t x, uint16_t y);

#endif
