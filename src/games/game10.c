// Paint.NOT - a finger painting pad

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

typedef struct {
	GPU_Color _selected_color;
	uint8_t _brush;          /* index into _brushSizes */
} Game10Memory;

static Game10Memory * const memory = (Game10Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_COLORS 8
#define PALETTE_W 24
#define PALETTE_H (GPU_Y-48)
#define SWATCH_H (PALETTE_H/NUM_COLORS)
#define BUTTON_Y (GPU_Y-44)
#define DRAW_X (PALETTE_W+2)

static const GPU_Color _color_list[NUM_COLORS] = {
	C_WHITE, C_BLACK, C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_C1, C_BLUE
};
static const uint8_t _brushSizes[3] = { 2, 4, 7 };

#define CLEAR_BUTTON_X 2
#define CLEAR_BUTTON_W (PALETTE_W+20)
#define SIZE_BUTTON_X (PALETTE_W+28)
#define SIZE_BUTTON_W (GPU_X - SIZE_BUTTON_X - 4)

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTime = 0;
    mainMemory.touch_X = 0;
    mainMemory.touch_Y = 0;
	memory->_selected_color = C_WHITE;
	memory->_brush = 1;

    /* the drawing is kept in the framebuffer, so clear it and paint the tools */
    GPU_ClearFramebuffers();
    GPU_DrawFilledSquare(RGB(20,20,26), 0, 0, DRAW_X, GPU_Y);
}

static void DrawTools(void)
{
	const FontDescription * f = &fontDescription[0];
	/* the palette is part of the persistent framebuffer so that it survives
	   while painting; redraw it on every frame over the drawing area edge */
	for (uint8_t i = 0; i < NUM_COLORS; i++)
	{
		GPU_DrawFilledSquare(_color_list[i], 2, (int16_t)(2 + i*SWATCH_H), (int16_t)(PALETTE_W-4), (int16_t)(SWATCH_H-2));
		if (_color_list[i] == memory->_selected_color)
			GPU_DrawEmptySquare(C_WHITE, 1, (int16_t)(1 + i*SWATCH_H), (int16_t)(PALETTE_W-2), (int16_t)(SWATCH_H));
	}
	GPU_DrawButton(C_GRAY_MEDIUM, C_WHITE, f, "CLR", CLEAR_BUTTON_X, BUTTON_Y, CLEAR_BUTTON_W, 20);
	char size[4] = { 'B', (char)('0' + _brushSizes[memory->_brush]), 0 };
	GPU_DrawButton(C_GRAY_MEDIUM, C_WHITE, f, size, SIZE_BUTTON_X, BUTTON_Y, SIZE_BUTTON_W, 20);
}

static void G_Update(void)
{
    if (mainMemory.touchPressed)
    {
    	if (mainMemory.touch_X < PALETTE_W)
		{
			if (mainMemory.touchPressedNew)
			{
				if (mainMemory.touch_Y < PALETTE_H)
				{
					memory->_selected_color = _color_list[mainMemory.touch_Y/SWATCH_H];
				}
				else if (mainMemory.touch_X < CLEAR_BUTTON_X + CLEAR_BUTTON_W)
				{
					G_Init();
				}
				else
				{
					memory->_brush = (uint8_t)((memory->_brush + 1) % 3);
				}
			}
		}
    	else if (mainMemory.touch_X < GPU_X && mainMemory.touch_Y < BUTTON_Y)
    	{
    		GPU_DrawFilledCircle(memory->_selected_color, mainMemory.touch_X, mainMemory.touch_Y,
    		                     _brushSizes[memory->_brush]);
    	}
    }
}

static void G_Draw(void)
{
	/* the picture lives in the framebuffer, only the tool bar is repainted */
	DrawTools();
}

static void G_Deinit(void)
{

}

const Callbacks G10_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
