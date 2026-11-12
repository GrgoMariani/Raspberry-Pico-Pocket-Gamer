// Lights Out

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef enum {
	LO_OFF = 0,
	LO_ON = 1
} LO_Light;

typedef struct {
    LO_Light _lights[5][5];
    unsigned int _stepsCount;
	uint8_t is_touch_pressed;
	uint32_t _startTime;
	uint8_t _cursorX, _cursorY;   /* keyboard cursor */
} Game3Memory;

/* the record lives outside of the shared memory so that it survives a visit
   to another game (which reuses the same memory block) */
static unsigned int _bestSteps = 0;   /* fewest steps of a solved board, 0 = none yet */


static Game3Memory * const memory = (Game3Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static LO_Light Toggle(LO_Light light)
{
	return light == LO_OFF ? LO_ON : LO_OFF;
}

static void ToggleLight(uint8_t x, uint8_t y)
{
	if ( x > 4 || y > 4)
		return;
	memory->_lights[x][y] = Toggle(memory->_lights[x][y]);
}

static void TogglePosition(uint8_t x, uint8_t y)
{
	if (mainMemory._gameState == GS_DONE || x > 4 || y > 4)
		return;
	ToggleLight(x, y);
	ToggleLight((uint8_t)(x-1), y);
	ToggleLight((uint8_t)(x+1), y);
	ToggleLight(x, (uint8_t)(y-1));
	ToggleLight(x, (uint8_t)(y+1));
	++memory->_stepsCount;
}

static uint8_t IsGameFinished()
{
	for (uint8_t i=0; i<5; i++)
		for (uint8_t j=0; j<5; j++)
			if (memory->_lights[i][j] == LO_ON)
				return 0;
	return 1;
}

static void G_Init(void)
{
	mainMemory._gameState = GS_RUNNING;
	memory->_stepsCount = 0;
	for (uint8_t i=0; i<5; i++)
		for (uint8_t j=0; j<5; j++)
		{
			memory->_lights[i][j] = LO_OFF;
		}
	/* scramble by pressing random cells, which guarantees solvability.
	   Repeating until the board is not already solved avoids an instant win. */
	do
	{
		for (uint8_t i=0; i<5; i++)
			for (uint8_t j=0; j<5; j++)
				memory->_lights[i][j] = LO_OFF;
		for (uint8_t n=0; n<10; n++)
		{
			uint8_t x = (uint8_t)Random_Range(5);
			uint8_t y = (uint8_t)Random_Range(5);
			ToggleLight(x, y);
			ToggleLight((uint8_t)(x-1), y);
			ToggleLight((uint8_t)(x+1), y);
			ToggleLight(x, (uint8_t)(y-1));
			ToggleLight(x, (uint8_t)(y+1));
		}
	} while (IsGameFinished());
	memory->_stepsCount = 0;
	memory->is_touch_pressed = 0;
	memory->_cursorX = 2;
	memory->_cursorY = 2;
	memory->_startTime = IF_GetCurrentTime();
	Keyboard_GetPressedKeys();
	Touch_ClearTap();
}

#define START_X (0)
#define START_Y (0)
#define END_X (GPU_Y)
#define END_Y (GPU_Y)
#define HW_BOX (END_X-START_X)
#define BOX_WIDTH (HW_BOX/5)

#define RESET_BUTTON_W (80)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 40)

static void G_Update(void)
{
	if (IsGameFinished())
	{
		if (mainMemory._gameState == GS_RUNNING)
		{
			/* remember the best solve of this session */
			if (_bestSteps == 0 || memory->_stepsCount < _bestSteps)
				_bestSteps = memory->_stepsCount;
		}
		mainMemory._gameState = GS_DONE;
	}
	else if (mainMemory._gameState == GS_RUNNING)
	{
		/* switches: arrows move a cursor, UP+DOWN together toggles a lamp */
		KeyPressedEnum keys = Keyboard_GetPressedKeys();
		if ((keys & KEY_UP) && (keys & KEY_DOWN))
			TogglePosition(memory->_cursorX, memory->_cursorY);
		else
		{
			if (keys & KEY_LEFT)  memory->_cursorX = (uint8_t)((memory->_cursorX + 4) % 5);
			if (keys & KEY_RIGHT) memory->_cursorX = (uint8_t)((memory->_cursorX + 1) % 5);
			if (keys & KEY_UP)    memory->_cursorY = (uint8_t)((memory->_cursorY + 4) % 5);
			if (keys & KEY_DOWN)  memory->_cursorY = (uint8_t)((memory->_cursorY + 1) % 5);
		}
		if (mainMemory.touchPressedNew)
		{
			if (mainMemory.touch_X <= END_X && mainMemory.touch_Y <= END_Y)
			{
				uint8_t x = (uint8_t)(mainMemory.touch_X / BOX_WIDTH);
				uint8_t y = (uint8_t)(mainMemory.touch_Y / BOX_WIDTH);
				if (x < 5 && y < 5)
				{
					memory->_cursorX = x;
					memory->_cursorY = y;
					TogglePosition(x, y);
				}
			}
		}
	}
	if (mainMemory._gameState != GS_RUNNING)
	{
		if (mainMemory.touchPressedNew)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
	}
}


#define CENTER_S2_X ((GPU_X-END_X)/2 + END_X)
#define S2_W (GPU_X-END_X)

static void DrawLamp(int16_t x, int16_t y, int16_t size, uint8_t on, uint8_t flash)
{
	int16_t cx = (int16_t)(x + size/2);
	int16_t cy = (int16_t)(y + size/2);
	int16_t r = (int16_t)(size/2 - 3);
	if (on)
	{
		GPU_DrawGlow(C_YELLOW, cx, cy, (int16_t)(r+4));
		GPU_DrawBall(flash ? C_WHITE : C_YELLOW, cx, cy, r);
	}
	else
	{
		GPU_DrawFilledCircle(RGB(40,40,48), cx, cy, r);
		GPU_DrawEmptyCircle(C_GRAY_DARK, cx, cy, r);
		GPU_DrawEmptyCircle(RGB(24,24,30), cx, cy, (int16_t)(r-3));
	}
}

static void G_Draw(void)
{
	const FontDescription * f = &fontDescription[0];
	const FontDescription * f_med = &fontDescription[2];
	const FontDescription * f_big = &fontDescription[3];
	GPU_ClearFramebuffers();
	GPU_DrawVerticalGradient(RGB(12,12,36), C_BLACK, 0, 0, GPU_X, GPU_Y);

	if (mainMemory._gameState == GS_RUNNING)
	{
		uint8_t lights_on = 0;
		for(uint8_t i=0; i<5; i++)
		{
			for(uint8_t j=0; j<5; j++)
			{
				if (memory->_lights[i][j] == LO_ON)
					lights_on++;
				DrawLamp((int16_t)(START_X + i*BOX_WIDTH + 2), (int16_t)(START_Y + j*BOX_WIDTH + 2),
				         (int16_t)(BOX_WIDTH - 4), (uint8_t)(memory->_lights[i][j] == LO_ON), 0);
				if (memory->_cursorX == i && memory->_cursorY == j)
					GPU_DrawEmptySquare(C_YELLOW, (int16_t)(START_X + i*BOX_WIDTH + 1),
					                    (int16_t)(START_Y + j*BOX_WIDTH + 1), (int16_t)(BOX_WIDTH-2), (int16_t)(BOX_WIDTH-2));
			}
		}
		GPU_DrawPanel(RGB(16,16,48), C_GRAY_DARK, (int16_t)(GPU_X-S2_W+6), 8, (int16_t)(S2_W-12), 140);
		GPU_DrawTextCentered(C_YELLOW, f, CENTER_S2_X, 16, "STEPS");
		char number[12];
		sprintf(number, "%u", memory->_stepsCount);
		GPU_DrawTextCentered(C_WHITE, f_med, CENTER_S2_X, 30, number);
		GPU_DrawTextCentered(C_YELLOW, f, CENTER_S2_X, 62, "LIGHTS ON");
		sprintf(number, "%u", lights_on);
		GPU_DrawTextCentered(C_WHITE, f_med, CENTER_S2_X, 74, number);
		GPU_DrawTextCentered(C_GRAY_MEDIUM, f, CENTER_S2_X, 100, "BEST");
		if (_bestSteps)
			sprintf(number, "%u", _bestSteps);
		else
			sprintf(number, "-");
		GPU_DrawTextCentered(C_GRAY_LIGHT, f, CENTER_S2_X, 112, number);
		sprintf(number, "TIME %u", (unsigned)((IF_GetCurrentTime()-memory->_startTime)/ONE_SECOND));
		GPU_DrawTextCentered(C_GRAY_MEDIUM, f, CENTER_S2_X, 132, number);
	}
	else
	{
		uint32_t nowTime = IF_GetCurrentTime();
		GPU_DrawTextCentered(C_C3, f_big, CENTER_S2_X, 20, "WELL DONE");
		char number[16];
		sprintf(number, "%u steps", memory->_stepsCount);
		GPU_DrawTextCentered(C_YELLOW, f, CENTER_S2_X, 60, number);
		if (_bestSteps)
		{
			sprintf(number, "best %u", _bestSteps);
			GPU_DrawTextCentered(C_GRAY_LIGHT, f, CENTER_S2_X, 76, number);
		}
		for(uint8_t i=0; i<5; i++)
		{
			for(uint8_t j=0; j<5; j++)
			{
				/* 1u: bit 31 of a signed int would be undefined behaviour */
				uint8_t flash = (nowTime & (1u<<(i+5*j+7))) ? 1 : 0;
				DrawLamp((int16_t)(START_X + i*BOX_WIDTH + 2), (int16_t)(START_Y + j*BOX_WIDTH + 2),
				         (int16_t)(BOX_WIDTH - 4), (uint8_t)(!flash), 0);
			}
		}
		GPU_DrawButton(C_YELLOW, C_BLACK, f_big, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
		               RESET_BUTTON_W, RESET_BUTTON_H);
	}
}

static void G_Deinit(void)
{

}

const Callbacks G3_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit};
