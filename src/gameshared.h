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
    uint8_t touchPressedNew;    // 1 during the single cycle a touch starts
    uint8_t touchReleasedNew;   // 1 during the single cycle a touch ends
	uint16_t touch_X;
	uint16_t touch_Y;
    // vars starting with _ should be handled by each game
    GameState _gameState;
	uint32_t _lastRenderedTime;
    uint32_t _lastRenderedTick;
} MainMemory;

extern MainMemory mainMemory;

/*
 * Keyboard
 * --------
 * Key_Down()/Key_Up() are fed with the RAW level of the switches by the
 * platform specific IF_Keyboard(). Debouncing happens here so that every
 * consumer (games and the main loop) sees one clean press per physical press.
 */
/* how long a switch has to keep its level before a press is reported */
#define KEY_DEBOUNCE_US 15000

void Key_Down(KeyPressedEnum key);
void Key_Up(KeyPressedEnum key);
/* returns and clears every key pressed since the previous call */
KeyPressedEnum Keyboard_GetPressedKeys();
/* returns and clears a single key of the press latch */
uint8_t Keyboard_TakeKey(KeyPressedEnum key);
/* returns and clears a single key of the release latch (fires once when a key
   that was held long enough to be debounced is let go again) */
uint8_t Keyboard_TakeKeyRelease(KeyPressedEnum key);
void Keyboard_ClearReleases(void);
KeyPressedEnum Keyboard_GetHeldKeys();

/*
 * Touchscreen
 * -----------
 * Touch_Update() is called once per main loop iteration by IF_Touchscreen()
 * with the (already calibrated) coordinates. It keeps the press/release edges
 * and lightly filters jitter while the finger keeps touching the panel.
 */
void Touch_Update(uint8_t pressed, uint16_t x, uint16_t y);

/*
 * Pseudo random numbers
 * ---------------------
 * The old implementation derived the value from a quadratic function of the
 * microsecond counter, which made the low bits alternate with a fixed pattern
 * (bit0 == parity of the counter). Games using "IF_Random() & 1" or
 * "IF_Random() % n" therefore got a predictable sequence. A xorshift32 seeded
 * with real entropy is used instead.
 */
void Random_Seed(uint32_t seed);
uint32_t Random_Next(void);
/* returns a value in [0, modulo) - modulo must be > 0 */
uint32_t Random_Range(uint32_t modulo);

#endif//GAMESHARED_H_
