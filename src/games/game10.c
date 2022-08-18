// Paint.NOT

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

typedef struct {
	GPU_Color _selected_color;
} Game10Memory;

static Game10Memory * const memory = (Game10Memory*)(&mainMemory.sharedMemory.memblocks[0]);


#define NUM_TICKS 50
#define MAX_FRAMERATE 30
#define TIME_PERIOD (ONE_SECOND/MAX_FRAMERATE)

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}


static const GPU_Color _color_list[10] = {C_WHITE, C_BLACK, C_GRAY_LIGHT, C_BLUE, C_GRAY_DARK, C_RED, C_GRAY_MEDIUM, C_GREEN, C_C1, C_C2};

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTime = 0;
    mainMemory.touch_X = 0;
    mainMemory.touch_Y = 0;
	memory->_selected_color = C_WHITE;

    GPU_ClearFramebuffers();

    for (uint8_t i=0; i<10; i++)
    {
    	GPU_DrawFilledSquare(_color_list[i], 0, i*24, 20, 20);
    }
	mainMemory._lastRenderedTime = 0;
}


static void G_Update(void)
{
	KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
	if (keyboard & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}


    if (mainMemory.touchPressed)
    {
    	if (mainMemory.touch_X >= 0 && mainMemory.touch_X < 24 && mainMemory.touch_Y >= 0 && mainMemory.touch_Y < 240)
		{
    		memory->_selected_color = _color_list[mainMemory.touch_Y/24];
		}
    	if (mainMemory.touch_X >= 26 && mainMemory.touch_X < 360 && mainMemory.touch_Y >= 0 && mainMemory.touch_Y < 240)
    	{
    		GPU_DrawFilledCircle(memory->_selected_color, mainMemory.touch_X, mainMemory.touch_Y, 3);
    	}
    }
}

static void G_Draw(void)
{
	// only render, everything is already in the framebuffer, limit to 3FPS
	// GPU_ClearFramebuffers(); - use framebuffer as memory
	uint32_t time_now = IF_GetCurrentTime();
	if (time_now - mainMemory._lastRenderedTime > TIME_PERIOD)
	{
		mainMemory._lastRenderedTime = IF_GetCurrentTime();
	}
}

static void G_Deinit(void)
{

}

const Callbacks G10_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
