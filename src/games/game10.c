// Paint.NOT - a finger painting pad

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

typedef struct {
	GPU_Color _selected_color;
	uint8_t _brush;          /* index into _brushSizes */
	uint8_t _hasLast;
	uint16_t _lastX;
	uint16_t _lastY;
	uint8_t _rainbow;      /* rainbow brush mode */
	uint8_t _hue;
} Game10Memory;

static Game10Memory * const memory = (Game10Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_COLORS 9
#define PALETTE_W 24
#define PALETTE_H (GPU_Y-48)
#define SWATCH_H (PALETTE_H/NUM_COLORS)
#define BUTTON_Y (GPU_Y-44)
#define DRAW_X (PALETTE_W+2)

static const GPU_Color _color_list[NUM_COLORS] = {
	C_WHITE, C_BLACK, C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_C1, C_BLUE, C_MAGENTA
};
#define RAINBOW_INDEX 8
static const GPU_Color _rainbowColors[6] = { C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_C1, C_MAGENTA };
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
	memory->_hasLast = 0;
	memory->_rainbow = 0;
	memory->_hue = 0;

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
		if (i == RAINBOW_INDEX)
		{
			/* the rainbow swatch is drawn as colour stripes */
			for (uint8_t s = 0; s < 6; s++)
				GPU_DrawFilledSquare(_rainbowColors[s], 2, (int16_t)(2 + i*SWATCH_H + s*(SWATCH_H-2)/6), (int16_t)(PALETTE_W-4), (int16_t)((SWATCH_H-2)/6 + 1));
			if (memory->_rainbow)
				GPU_DrawEmptySquare(C_WHITE, 1, (int16_t)(1 + i*SWATCH_H), (int16_t)(PALETTE_W-2), (int16_t)(SWATCH_H));
			continue;
		}
		GPU_DrawFilledSquare(_color_list[i], 2, (int16_t)(2 + i*SWATCH_H), (int16_t)(PALETTE_W-4), (int16_t)(SWATCH_H-2));
		if (!memory->_rainbow && _color_list[i] == memory->_selected_color)
			GPU_DrawEmptySquare(C_WHITE, 1, (int16_t)(1 + i*SWATCH_H), (int16_t)(PALETTE_W-2), (int16_t)(SWATCH_H));
	}
	GPU_DrawButton(C_GRAY_MEDIUM, C_WHITE, f, "CLR", CLEAR_BUTTON_X, BUTTON_Y, CLEAR_BUTTON_W, 20);
	char size[4] = { 'B', (char)('0' + _brushSizes[memory->_brush]), 0 };
	GPU_DrawButton(C_GRAY_MEDIUM, C_WHITE, f, size, SIZE_BUTTON_X, BUTTON_Y, SIZE_BUTTON_W, 20);
}

/* the colour for the next dab of the stroke; the rainbow brush cycles */
static GPU_Color StrokeColor(void)
{
	if (memory->_rainbow)
		return _rainbowColors[(memory->_hue++) % 6];
	return memory->_selected_color;
}

/* paints a continuous stroke: the circles are interpolated between the last
   and the current touch position so that a fast finger does not leave holes */
static void PaintStroke(uint16_t x, uint16_t y)
{
	int16_t r = (int16_t)_brushSizes[memory->_brush];
	if (memory->_hasLast)
	{
		int32_t dx = (int32_t)x - (int32_t)memory->_lastX;
		int32_t dy = (int32_t)y - (int32_t)memory->_lastY;
		int32_t ax = (dx < 0) ? -dx : dx;
		int32_t ay = (dy < 0) ? -dy : dy;
		int32_t dist = (ax > ay) ? ax : ay;
		int32_t steps = dist/(r/2 + 1);
		if (steps > 128) steps = 128;
		for (int32_t i = 1; i < steps; i++)
			GPU_DrawFilledCircle(StrokeColor(),
			                     (int16_t)((int32_t)memory->_lastX + dx*i/steps),
			                     (int16_t)((int32_t)memory->_lastY + dy*i/steps), r);
	}
	GPU_DrawFilledCircle(StrokeColor(), (int16_t)x, (int16_t)y, r);
	memory->_lastX = x;
	memory->_lastY = y;
	memory->_hasLast = 1;
}

static void G_Update(void)
{
    if (mainMemory.touchPressedNew)
    {
    	memory->_hasLast = 0;
    	/* the tool bar: the hit areas match the drawn buttons */
    	if (mainMemory.touch_Y >= BUTTON_Y)
    	{
    		if (mainMemory.touch_X >= CLEAR_BUTTON_X && mainMemory.touch_X < CLEAR_BUTTON_X + CLEAR_BUTTON_W)
    		{
    			G_Init();
    			return;
    		}
    		if (mainMemory.touch_X >= SIZE_BUTTON_X && mainMemory.touch_X < SIZE_BUTTON_X + SIZE_BUTTON_W)
    		{
    			memory->_brush = (uint8_t)((memory->_brush + 1) % 3);
    			return;
    		}
    	}
    	else if (mainMemory.touch_X < PALETTE_W && mainMemory.touch_Y < PALETTE_H)
    	{
    		uint8_t idx = (uint8_t)(mainMemory.touch_Y/SWATCH_H);
    		if (idx >= NUM_COLORS) idx = NUM_COLORS-1;
    		if (idx == RAINBOW_INDEX)
    			memory->_rainbow = 1;
    		else
    		{
    			memory->_rainbow = 0;
    			memory->_selected_color = _color_list[idx];
    		}
    		return;
    	}
    }
    if (mainMemory.touchPressed)
    {
    	if (mainMemory.touch_X >= PALETTE_W && mainMemory.touch_X < GPU_X && mainMemory.touch_Y < BUTTON_Y)
    		PaintStroke(mainMemory.touch_X, mainMemory.touch_Y);
    	else
    		memory->_hasLast = 0;
    }
    else
    	memory->_hasLast = 0;
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
