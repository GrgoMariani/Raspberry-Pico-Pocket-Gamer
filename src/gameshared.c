#include "gameshared.h"

MainMemory mainMemory;

#define ALL_KEYS (KEY_MENU | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)


#define NUM_KEYS 5

static volatile KeyPressedEnum _newKeyDown = KEY_NONE;
static volatile KeyPressedEnum _newKeyUp = KEY_NONE;
static volatile KeyPressedEnum _heldKeys = KEY_NONE;

static uint8_t  _rawDown[NUM_KEYS];
static uint8_t  _stable[NUM_KEYS];
static uint32_t _rawSince[NUM_KEYS];

static uint8_t _keyIndex(KeyPressedEnum key)
{
	switch (key)
	{
	case KEY_UP:    return 0;
	case KEY_DOWN:  return 1;
	case KEY_LEFT:  return 2;
	case KEY_RIGHT: return 3;
	case KEY_MENU:  return 4;
	default:        return 0;
	}
}

void Key_Down(KeyPressedEnum key)
{
	uint8_t idx = _keyIndex(key);
	uint32_t now = IF_GetCurrentTime();
	if (!_rawDown[idx])
	{
		_rawDown[idx] = 1;
		_rawSince[idx] = now;
		return;
	}
	if (!_stable[idx] && (now - _rawSince[idx]) >= KEY_DEBOUNCE_US)
	{
		_stable[idx] = 1;
		_heldKeys |= key;
		_newKeyDown |= key;
	}
}

void Key_Up(KeyPressedEnum key)
{
	uint8_t idx = _keyIndex(key);
	uint32_t now = IF_GetCurrentTime();
	if (_rawDown[idx])
	{
		_rawDown[idx] = 0;
		_rawSince[idx] = now;
	}
	if (_stable[idx])
		_newKeyUp |= key;
	_stable[idx] = 0;
	_heldKeys &= (ALL_KEYS^key);
}

uint8_t Keyboard_TakeKeyRelease(KeyPressedEnum key)
{
	if (!(_newKeyUp & key))
		return 0;
	_newKeyUp &= (ALL_KEYS^key);
	return 1;
}

KeyPressedEnum Keyboard_GetPressedKeys()
{
	KeyPressedEnum result = _newKeyDown;
	_newKeyDown = KEY_NONE;
	return result;
}

void Keyboard_ClearReleases(void)
{
	_newKeyUp = KEY_NONE;
}

uint8_t Keyboard_TakeKey(KeyPressedEnum key)
{
	if (!(_newKeyDown & key))
		return 0;
	_newKeyDown &= (ALL_KEYS^key);
	return 1;
}

KeyPressedEnum Keyboard_GetHeldKeys()
{
	return _heldKeys;
}

///////////////////////////////////////////////
// Touchscreen state shared by all games
///////////////////////////////////////////////

/* the finger has to move further than this before the filter snaps to it */
#define TOUCH_SNAP_DISTANCE 10

void Touch_Update(uint8_t pressed, uint16_t x, uint16_t y)
{
	mainMemory.touchReleasedNew = (mainMemory.touchPressed && !pressed) ? 1 : 0;
	mainMemory.touchPressedNew = (!mainMemory.touchPressed && pressed) ? 1 : 0;
	mainMemory.touchPressed = pressed;
	if (!pressed)
		return;
	if (mainMemory.touchPressedNew)
	{
		// a new touch has to land exactly where the finger is
		mainMemory.touch_X = x;
		mainMemory.touch_Y = y;
		return;
	}
	// low pass filter against jitter, but never lag behind a real movement
	int32_t dx = (int32_t)x - (int32_t)mainMemory.touch_X;
	int32_t dy = (int32_t)y - (int32_t)mainMemory.touch_Y;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	if (dx > TOUCH_SNAP_DISTANCE || dy > TOUCH_SNAP_DISTANCE)
	{
		mainMemory.touch_X = x;
		mainMemory.touch_Y = y;
	}
	else
	{
		mainMemory.touch_X = (uint16_t)((3*(int32_t)mainMemory.touch_X + x)/4);
		mainMemory.touch_Y = (uint16_t)((3*(int32_t)mainMemory.touch_Y + y)/4);
	}
}

///////////////////////////////////////////////
// xorshift32 pseudo random generator
///////////////////////////////////////////////

static uint32_t _randomState = 0xdeadb0d1;

void Random_Seed(uint32_t seed)
{
	// the generator must never be seeded with 0
	_randomState = seed ? seed : 0xdeadb0d1;
	// warm it up so that a small seed still spreads over all the bits
	for (uint8_t i = 0; i < 8; i++)
		Random_Next();
}

uint32_t Random_Next(void)
{
	uint32_t x = _randomState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	_randomState = x;
	return x;
}

uint32_t Random_Range(uint32_t modulo)
{
	if (modulo == 0)
		return 0;
	return Random_Next() % modulo;
}
