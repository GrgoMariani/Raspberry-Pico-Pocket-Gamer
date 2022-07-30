#ifndef GAMESHARED_H_
#define GAMESHARED_H_

#include "interface.h"
#include "gpu.h"
#include <stdint.h>

typedef enum {
    GS_NOT_STARTED = 0,
    GS_RUNNING     = 1,
    GS_DONE        = 2,
    GS_ANIMATING   = 3
} GameState;

typedef struct {
    uint32_t memblocks[16*1024];
} SharedMemory;

typedef struct {
    SharedMemory sharedMemory;
    uint8_t touchPressed;
	uint16_t touch_X;
	uint16_t touch_Y;
    // vars starting with _ should be handled by each game
    GameState _gameState;
	uint32_t _lastRenderedTime;
    uint32_t _lastRenderedTick;
} MainMemory;

extern MainMemory mainMemory;

void Key_Down(KeyPressedEnum key);
void Key_Up(KeyPressedEnum key);
KeyPressedEnum Keyboard_GetPressedKeys();
KeyPressedEnum Keyboard_GetHeldKeys();

#endif//GAMESHARED_H_
