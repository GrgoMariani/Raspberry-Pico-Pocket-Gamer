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
} Game3Memory;


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
	ToggleLight(x-1, y);
	ToggleLight(x+1, y);
	ToggleLight(x, y-1);
	ToggleLight(x, y+1);
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

#define NUM_TICKS 3

static inline uint32_t CalculateTick(long long timeNow)
{
	return timeNow/(ONE_SECOND/NUM_TICKS);
}

static void G_Init(void)
{
	mainMemory._gameState = GS_RUNNING;
	uint32_t rand = IF_Random();
	memory->_stepsCount = 0;
	for (uint8_t i=0; i<5; i++)
		for (uint8_t j=0; j<5; j++)
		{
			memory->_lights[i][j] = LO_OFF;
		}
	for (uint8_t i=0; i<5; i++)
		for (uint8_t j=0; j<5; j++)
		{
			if (rand & (1<<(i*5+j)))
			{
				TogglePosition(i, j);
				TogglePosition(i+1, j+2);
				TogglePosition(i-2, j);
			}
		}
	memory->_stepsCount = 0;
	mainMemory._lastRenderedTick = CalculateTick(IF_GetCurrentTime());
}

/*
#define START_X (GPU_BORDER_X)
#define START_Y (GPU_BORDER_Y)
#define END_X (GPU_X/2-GPU_BORDER_X)
#define END_Y (GPU_Y-GPU_BORDER_Y)
#define HW_BOX (END_X-START_X)
#define BOX_WIDTH (HW_BOX/5)
*/

#define START_X (0)
#define START_Y (0)
#define END_X (GPU_Y)
#define END_Y (GPU_Y)
#define HW_BOX (END_X-START_X)
#define BOX_WIDTH (HW_BOX/5)


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)


static void G_Update(void)
{
	uint32_t tickNow = CalculateTick(IF_GetCurrentTime());
	KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
	if (keyboard & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}

	if (IsGameFinished())
	{
		mainMemory._gameState = GS_DONE;
	}
	else
	{
		while (mainMemory._lastRenderedTick < tickNow && mainMemory._gameState == GS_RUNNING)
		{
			if (mainMemory.touchPressed)
			{
				uint8_t x=5,y=5;
				if (mainMemory.touch_X < START_X || mainMemory.touch_X > END_X || mainMemory.touch_Y < START_Y || mainMemory.touch_Y > END_Y)
					return;
				mainMemory.touch_X -= START_X;
				mainMemory.touch_Y -= START_Y;
				x = mainMemory.touch_X / (BOX_WIDTH);
				y = mainMemory.touch_Y / (BOX_WIDTH);
				if (x < 5 && y < 5)
					TogglePosition(x, y);
			}
			mainMemory._lastRenderedTick++;
		}
	}
	if (mainMemory._gameState != GS_RUNNING)
	{
		if (mainMemory.touchPressed)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
	}
}


#define CENTER_S2_X ((GPU_X-END_X)/2 + END_X)
#define CENTER_S2_Y ((GPU_Y/2)
#define S2_W (GPU_X-END_X)
#define S2_H (GPU_X-END_X)

static void G_Draw(void)
{
	const FontDescription * f = &fontDescription[0];
	const FontDescription * f_med = &fontDescription[2];
	const FontDescription * f_big = &fontDescription[3];
	GPU_ClearFramebuffers();

	if (mainMemory._gameState == GS_RUNNING)
	{
		uint8_t lights_on = 0;
		for(uint8_t i=0; i<5; i++)
		{
			for(uint8_t j=0; j<5; j++)
			{
				if (memory->_lights[i][j] == LO_OFF)
				{
					GPU_DrawEmptySquare(C_YELLOW, START_X + i*BOX_WIDTH, START_Y+j*BOX_WIDTH, BOX_WIDTH-1, BOX_WIDTH-1);
				}
				else
				{
					lights_on++;
					GPU_DrawFilledSquare(C_YELLOW, START_X + i*BOX_WIDTH, START_Y+j*BOX_WIDTH, BOX_WIDTH-1, BOX_WIDTH-1);
				}
			}
		}
		const char * message1 = "Steps counter";
		uint8_t len = strlen(message1);
		GPU_DrawText(C_YELLOW, f, CENTER_S2_X-f->width*len/2, GPU_Y/8-f->height/2, message1, len);
		GPU_DrawFilledSquare(C_WHITE, GPU_X-S2_W, GPU_Y/4, S2_W, GPU_Y/2);
		char number[8];
		len = sprintf(number, "%d", memory->_stepsCount);
		GPU_DrawText(C_BLACK, f_med, CENTER_S2_X-len*f_med->width/2, GPU_Y/2-f_med->height/2, number, strlen(number));
		len = sprintf(number, "%d", lights_on);
		GPU_DrawText(C_YELLOW, f, CENTER_S2_X - S2_W/2, GPU_Y*3/4+8+f->height/2, number, strlen(number));
		const char * message2 = "lights on";
		GPU_DrawText(C_YELLOW, f, CENTER_S2_X - S2_W/2 + f->width*3, GPU_Y*3/4+8+f->height/2, message2, strlen(message2));
	}
	else
	{
		uint32_t nowTime = IF_GetCurrentTime();
		const char * message1 = "WELL DONE";
		uint8_t len = strlen(message1);
		GPU_DrawText(C_BLUE, f, CENTER_S2_X-f->width*len/2, GPU_Y/8, message1, len);
		const char * message2 = "Steps";
		len = strlen(message2);
		GPU_DrawText(C_YELLOW, f, CENTER_S2_X-f->width*len/2, S2_CENTER_Y, message2, len);
		char number[8];
		len = sprintf(number, "%d", memory->_stepsCount);
		GPU_DrawText(C_YELLOW, f, CENTER_S2_X-f->width*len/2, S2_CENTER_Y+f->height, number, len);
		for(uint8_t i=0; i<5; i++)
		{
			for(uint8_t j=0; j<5; j++)
			{
				if (nowTime & (1<<(i+5*j+7)))
				{
					GPU_DrawEmptySquare(C_WHITE, START_X + i*BOX_WIDTH, START_Y+j*BOX_WIDTH, BOX_WIDTH-1, BOX_WIDTH-1);
				}
				else
				{
					GPU_DrawFilledSquare(C_YELLOW, START_X + i*BOX_WIDTH, START_Y+j*BOX_WIDTH, BOX_WIDTH-1, BOX_WIDTH-1);
				}
			}
		}
		// Draw reset button
		{
			const char * textToDisplay = "RESET";
			int8_t len = strlen(textToDisplay);
			GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
			GPU_DrawText(C_BLACK, f_big, RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
		}
	}
	GPU_Render();
}

static void G_Deinit(void)
{
	
}

const Callbacks G3_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit};
