// Calculator

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"


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
} Game12Memory;

static Game12Memory * const memory = (Game12Memory*)(&mainMemory.sharedMemory.memblocks[0]);


#define MAX_NUMS 8

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    memory->operation = OP_NONE;
    memory->current_input = 0;
    memory->current_input_num = 0;
    memory->previous_result = 0;
	memory->is_touch_held = 0;
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
	memory->operation = op;
	memory->current_input = 0;
	memory->current_input_num = 0;
}

#define BOX_W ((GPU_X/2)/4)
#define BOX_H (GPU_Y/4)

static void G_Update(void)
{
    // get input
	if (mainMemory.touchPressed && !memory->is_touch_held)
    {
        memory->is_touch_held=1;
        if (mainMemory.touch_X >= 0 && mainMemory.touch_X <= GPU_X/2 && mainMemory.touch_Y >= 0 && mainMemory.touch_Y <= GPU_Y)
		{
			uint8_t x = mainMemory.touch_X/BOX_W;
			uint8_t y = mainMemory.touch_Y/BOX_H;
			uint8_t pressed = 4*y+x;
			switch (pressed)
			{
			case 0: // 1
				NumPressed(1);
				break;
			case 1: // 2
				NumPressed(2);
				break;
			case 2: // 3
				NumPressed(3);
				break;
			case 3: // +
				OperationPressed(OP_ADD);
				break;
			case 4: // 4
				NumPressed(4);
				break;
			case 5: // 5
				NumPressed(5);
				break;
			case 6: // 6
				NumPressed(6);
				break;
			case 7: // -
				OperationPressed(OP_SUBSTRACT);
				break;
			case 8: // 7
				NumPressed(7);
				break;
			case 9: // 8
				NumPressed(8);
				break;
			case 10: // 9
				NumPressed(9);
				break;
			case 11: // *
				OperationPressed(OP_MULTIPLY);
				break;
			case 12: // C
				G_Init();
				break;
			case 13: // 0
				NumPressed(0);
				break;
			case 14: // /
				OperationPressed(OP_DIVIDE);
				break;
			case 15: // =
				OperationPressed(OP_EQUAL);
				break;
			}
		}
    }
    if (!mainMemory.touchPressed)
    {
        memory->is_touch_held = 0;
    }
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
    GPU_ClearFramebuffers();
    const char nums[16] = "123+456-789*C0/=";
    const char operation_c[6] = " +-*/=";

    for (uint8_t i=0; i<4; i++)
    	for (uint8_t j=0; j<4; j++)
    	{
    		GPU_DrawFilledSquare(C_WHITE, i*BOX_W+1, j*BOX_H+1, BOX_W-2, BOX_H-2);
    		GPU_DrawLetter(C_BLACK, f_big, i*BOX_W + BOX_W/2 - f_big->width/2, j*BOX_H+BOX_H/2-f_big->height/2, nums[4*j+i]);
    	}
    char buffer[12];
    uint8_t len;
    len = sprintf(buffer, "%d", memory->previous_result);
    GPU_DrawText(C_WHITE, f_big, S2_CENTER_X - len*f_big->width/2, S2_CENTER_Y - 4*f_big->height/2, buffer, len);

    GPU_DrawLetter(C_WHITE, f_big, S2_CENTER_X - f_big->width/2, S2_CENTER_Y - f_big->height/2, operation_c[(uint8_t)memory->operation]);

    len = sprintf(buffer, "%d", memory->current_input);
	GPU_DrawText(C_WHITE, f_big, S2_CENTER_X - len*f_big->width/2, S2_CENTER_Y + 2*f_big->height/2, buffer, len);
}

static void G_Deinit(void)
{

}

const Callbacks G12_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
