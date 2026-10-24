// Calculator

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <stdio.h>
#include <string.h>

typedef enum {
	OP_NONE      = 0,
	OP_ADD       = 1,
	OP_SUBSTRACT = 2,
	OP_MULTIPLY  = 3,
	OP_DIVIDE    = 4,
	OP_EQUAL     = 5
} Operation;

typedef struct {
    long long current_input;
    uint8_t current_input_num;
    long long previous_result;
    Operation operation;
	uint8_t is_touch_held;
	uint8_t _flashKey;
} Game12Memory;

static Game12Memory * const memory = (Game12Memory*)(&mainMemory.sharedMemory.memblocks[0]);


#define MAX_NUMS 8
#define RESULT_MAX 999999999999LL

static void ClampResult(long long * value)
{
	if (*value > RESULT_MAX) *value = RESULT_MAX;
	if (*value < -RESULT_MAX) *value = -RESULT_MAX;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    memory->operation = OP_NONE;
    memory->current_input = 0;
    memory->current_input_num = 0;
    memory->previous_result = 0;
	memory->is_touch_held = 0;
	memory->_flashKey = 0;
}

static void NumPressed(uint8_t num)
{
	if (memory->current_input_num < MAX_NUMS)
	{
		memory->current_input_num++;
		memory->current_input *= 10;
		memory->current_input += num;
	}
}

static void OperationPressed(Operation op)
{
	// first calculate whatever it was
	switch(memory->operation)
	{
	case OP_ADD:
		memory->previous_result += memory->current_input;
		break;
	case OP_SUBSTRACT:
		memory->previous_result -= memory->current_input;
		break;
	case OP_MULTIPLY:
		memory->previous_result *= memory->current_input;
		break;
	case OP_DIVIDE:
		if (memory->current_input == 0)
			memory->current_input = 1;
		memory->previous_result /= memory->current_input;
		break;
	case OP_EQUAL:
		memory->operation = OP_NONE;
		break;
	case OP_NONE:
	default:
		memory->previous_result = memory->current_input;
		memory->operation = OP_NONE;
		break;
	}
	ClampResult(&memory->previous_result);
	memory->operation = op;
	memory->current_input = 0;
	memory->current_input_num = 0;
}

#define BOX_W ((GPU_X/2)/4)
#define BOX_H (GPU_Y/4)

static void PressKey(uint8_t pressed)
{
	memory->_flashKey = (uint8_t)(pressed + 1);
	switch (pressed)
	{
	case 0:  NumPressed(1); break;
	case 1:  NumPressed(2); break;
	case 2:  NumPressed(3); break;
	case 3:  OperationPressed(OP_ADD); break;
	case 4:  NumPressed(4); break;
	case 5:  NumPressed(5); break;
	case 6:  NumPressed(6); break;
	case 7:  OperationPressed(OP_SUBSTRACT); break;
	case 8:  NumPressed(7); break;
	case 9:  NumPressed(8); break;
	case 10: NumPressed(9); break;
	case 11: OperationPressed(OP_MULTIPLY); break;
	case 12: G_Init(); break;
	case 13: NumPressed(0); break;
	case 14: OperationPressed(OP_DIVIDE); break;
	case 15: OperationPressed(OP_EQUAL); break;
	default: break;
	}
}

static void G_Update(void)
{
	if (memory->_flashKey)
		memory->_flashKey--;
    // get input
	if (mainMemory.touchPressedNew)
    {
        if (mainMemory.touch_X < GPU_X/2 && mainMemory.touch_Y < GPU_Y)
		{
			uint8_t x = (uint8_t)(mainMemory.touch_X/BOX_W);
			uint8_t y = (uint8_t)(mainMemory.touch_Y/BOX_H);
			if (x < 4 && y < 4)
				PressKey((uint8_t)(4*y + x));
		}
    }
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
	const FontDescription * f_med = &fontDescription[2];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,14,40), C_BLACK, GPU_X/2, 0, GPU_X/2, GPU_Y);
    const char nums[16] = "123+456-789*C0/=";
    const char operation_c[7] = " +-*/=";

    for (uint8_t i=0; i<4; i++)
    	for (uint8_t j=0; j<4; j++)
    	{
			uint8_t index = (uint8_t)(4*j+i);
			uint8_t isOperator = (index == 3 || index == 7 || index == 11 || index == 14 || index == 15);
			GPU_Color key = isOperator ? C_C1 : C_WHITE;
			if (memory->_flashKey == index+1)
				key = isOperator ? C_C3 : C_GRAY_LIGHT;
			GPU_DrawBevelSquare(key, (int16_t)(i*BOX_W+2), (int16_t)(j*BOX_H+2),
			                    (int16_t)(BOX_W-4), (int16_t)(BOX_H-4));
			char label[2] = { nums[index], 0 };
			GPU_DrawTextCentered(isOperator ? C_WHITE : C_BLACK, f_big,
			                     (int16_t)(i*BOX_W+BOX_W/2), (int16_t)(j*BOX_H+BOX_H/2-f_big->height/2), label);
    	}

    /* the display */
    GPU_DrawPanel(RGB(16,20,56), C_C1, (int16_t)(GPU_X/2+8), 8, (int16_t)(GPU_X/2-16), 110);
    char buffer[24];
    sprintf(buffer, "%lld", memory->previous_result);
    GPU_DrawText(C_GRAY_MEDIUM, f_med, (int16_t)(GPU_X-16-(uint16_t)strlen(buffer)*f_med->width), 20, buffer, (uint16_t)strlen(buffer));
    GPU_DrawLetter(C_YELLOW, f_big, (int16_t)(GPU_X/2+16), 52, operation_c[(uint8_t)memory->operation]);
    sprintf(buffer, "%lld", memory->current_input);
    GPU_DrawText(C_WHITE, f_big, (int16_t)(GPU_X-16-(uint16_t)strlen(buffer)*f_big->width), 76, buffer, (uint16_t)strlen(buffer));

    const char * hint = "tap the keys";
    GPU_DrawTextCentered(C_GRAY_DARK, f_med, (int16_t)(GPU_X*3/4), 140, hint);
}

static void G_Deinit(void)
{

}

const Callbacks G12_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
