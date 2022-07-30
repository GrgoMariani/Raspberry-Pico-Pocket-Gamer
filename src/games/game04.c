// MATH

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    GR_NO_GUESS,
    GR_CORRECT,
    GR_WRONG
} GuessResult;

typedef enum {
    AO_PLUS,
    AO_MINUS,
    AO_MULTIPLY,
    AO_DIVIDE
} ArithmeticOperation;

typedef struct {
    GuessResult _lastGuess;
    uint8_t _answer;
    ArithmeticOperation _arithmeticOperation;
    uint8_t _x;
    uint8_t _y;
    uint32_t _guessesCorrect;
    uint32_t _guessesTotal;
} Game4Memory;

static Game4Memory * const memory = (Game4Memory*)(&mainMemory.sharedMemory.memblocks[0]);


static void GenerateAddition(uint8_t num)
{
    if (num == 0)
    {
        memory->_x = 0;
        memory->_y = 0;
        return;
    }
    uint8_t rand = IF_Random() % num;
    memory->_x = num - rand;
    memory->_y = rand;
}

static void GenerateSubtraction(uint8_t num)
{
    uint8_t rand = IF_Random() % (255-num);
    memory->_x = num + rand;
    memory->_y = rand;
}

static void GenerateMultiplication(uint8_t num)
{
    if (!num)
    {
        while (!memory->_answer)
            memory->_answer = IF_Random() % 16;
        num = memory->_answer;
    }
    uint8_t rand = (IF_Random() % num) + 1;
    int i = num;
    while (1)
    {
        if (num % i == 0)
        {
            rand--;
        }
        if (!rand) break;
        i--;
        if (i == 0) i = num;
    }
    memory->_x = i;
    memory->_y = num/i;
}

static void GenerateDivision(uint8_t num)
{
    if (!num)
    {
        while (!memory->_answer)
            memory->_answer = IF_Random() % 16;
        num = memory->_answer;
    }
    memory->_y = (IF_Random() &0x0f) + 1;
    memory->_x = memory->_y * num;
}

static void GenerateNewAnswer()
{
    uint32_t rand = IF_GetCurrentTime();
    memory->_answer = rand & 0x0f;
    switch ((rand & 0x30)>>4)
    {
    case 0:
        memory->_arithmeticOperation = AO_PLUS;
        GenerateAddition(memory->_answer);
        break;
    case 1:
        memory->_arithmeticOperation = AO_MINUS;
        GenerateSubtraction(memory->_answer);
        break;
    case 2:
        memory->_arithmeticOperation = AO_DIVIDE;
        GenerateDivision(memory->_answer);
        break;
    case 3:
        memory->_arithmeticOperation = AO_MULTIPLY;
        GenerateMultiplication(memory->_answer);
        break;
    default:
        break;
    }
}

#define NUM_TICKS 3

static inline uint32_t CalculateTick(long long timeNow)
{
	return timeNow/(ONE_SECOND/NUM_TICKS);
}

static void G_Init(void)
{
    memory->_lastGuess = GR_NO_GUESS;
    memory->_guessesCorrect = 0;
    memory->_guessesTotal = 0;
    GenerateNewAnswer();
	mainMemory._lastRenderedTick = CalculateTick(IF_GetCurrentTime());
}


#define BOX_W ((GPU_X/2)/4)
#define BOX_H (GPU_Y/4)


static void G_Update(void)
{
	uint32_t tickNow = CalculateTick(IF_GetCurrentTime());
	KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
	if (keyboard & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}

    while (mainMemory._lastRenderedTick < tickNow)
    {
        if (mainMemory.touchPressed && mainMemory.touch_X >= GPU_X/2 && mainMemory.touch_X <= GPU_X && mainMemory.touch_Y >= 0 && mainMemory.touch_Y <= GPU_Y)
        {
            uint8_t x = (mainMemory.touch_X - GPU_X/2)/BOX_W;
            uint8_t y = mainMemory.touch_Y/BOX_H;
            uint8_t answer = 4*y+x;
            if (answer == memory->_answer)
            {
                memory->_lastGuess = GR_CORRECT;
                memory->_guessesCorrect++;
                memory->_guessesTotal++;
            }
            else
            {
                memory->_lastGuess = GR_WRONG;
                memory->_guessesTotal++;
            }
            GenerateNewAnswer();
        }
        mainMemory._lastRenderedTick++;
    }
}


#define MAX_FRAMERATE 2
#define TIME_PERIOD (ONE_SECOND/MAX_FRAMERATE)

static void G_Draw(void)
{
	uint32_t time_now = IF_GetCurrentTime();
	if (time_now - mainMemory._lastRenderedTime > TIME_PERIOD)
	{
		GPU_Render();
		mainMemory._lastRenderedTime = IF_GetCurrentTime();
	}
    const FontDescription * f = &fontDescription[0];
    const FontDescription * f_medium = &fontDescription[1];
    const FontDescription * f_big = &fontDescription[2];
    const FontDescription * f_huge = &fontDescription[3];
    GPU_ClearFramebuffers();
    char buffer[8];
    uint8_t ll;
    {
        sprintf(buffer, "%d", memory->_x);
        ll = strlen(buffer);
        GPU_DrawText(C_GREEN, f_huge, GPU_X/4 - (ll - 1)*f_huge->width/2, GPU_Y/2-4*f_huge->height/2, buffer, ll);
        char symbol;
        switch (memory->_arithmeticOperation)
        {
        case AO_PLUS:
            symbol = '+';
            break;
        case AO_MINUS:
            symbol = '-';
            break;
        case AO_DIVIDE:
            symbol = '/';
            break;
        case AO_MULTIPLY:
            symbol = '*';
            break;
        default:
            symbol = ' ';
            break;
        }
        GPU_DrawLetter(C_GREEN, f_huge, GPU_X/4, GPU_Y/2-f_huge->height/2, symbol);
        sprintf(buffer, "%d", memory->_y);
        ll = strlen(buffer);
        GPU_DrawText(C_GREEN, f_huge, GPU_X/4 - (ll - 1)*f_huge->width/2, GPU_Y/2+2*f_huge->height/2, buffer, ll);
    }

    if (memory->_lastGuess == GR_CORRECT)
    {
        const char * message = "CORRECT ANSWER";
        ll = strlen(message);
        GPU_DrawText(C_BLUE, f_big, S1_CENTER_X - (ll/2)*f_big->width, GPU_Y/8, message, ll);
    }
    else if (memory->_lastGuess == GR_WRONG)
    {
        const char * message = "WRONG ANSWER";
        ll = strlen(message);
        GPU_DrawText(C_RED, f_big, S1_CENTER_X - (ll/2)*f_big->width, GPU_Y/8, message, ll);
    }

    {
        const char * message = "Correct:";
        uint8_t len = strlen(message);
        GPU_DrawText(C_GREEN, f, 8, 3*GPU_Y/4, message, len);
        len = sprintf(buffer, "%d", memory->_guessesCorrect);
        GPU_DrawText(C_GREEN, f, 60, 3*GPU_Y/4+f->height, buffer, len);
    }
    {
        const char * message = "Total:";
        uint8_t len = strlen(message);
        GPU_DrawText(C_WHITE, f, 8, 3*GPU_Y/4 + 2*f->height, message, len);
        len = sprintf(buffer, "%d", memory->_guessesTotal);
        GPU_DrawText(C_WHITE, f, 60, 3*GPU_Y/4+3*f->height, buffer, len);
    }

    if (memory->_lastGuess != GR_NO_GUESS)
    {
        uint8_t len = sprintf(buffer, "%d%%", (memory->_guessesCorrect*100/memory->_guessesTotal));
        GPU_DrawText(C_YELLOW, f, GPU_X/2 - len*f->width, f->height, buffer, len);
    }
    
    const char nums[16] = "0123456789ABCDEF";
    for (uint8_t i=0; i<4; i++)
		for (uint8_t j=0; j<4; j++)
		{
			GPU_DrawFilledSquare(C_WHITE, GPU_X/2+i*BOX_W+1, j*BOX_H+1, BOX_W-2, BOX_H-2);
			uint8_t value = 4*j+i;
			if (value < 10)
			{
				GPU_DrawLetter(C_BLACK, f_huge, GPU_X/2+i*BOX_W + BOX_W/2 - f_huge->width/2, j*BOX_H+BOX_H/2-f_huge->height/2, nums[value]);
			}
			else
			{
				value -= 10;
				GPU_DrawLetter(C_BLACK, f_huge, GPU_X/2+i*BOX_W + BOX_W/2 - f_huge->width, j*BOX_H+BOX_H/2-f_huge->height/2, '1');
				GPU_DrawLetter(C_BLACK, f_huge, GPU_X/2+i*BOX_W + BOX_W/2, j*BOX_H+BOX_H/2-f_huge->height/2, nums[value]);
			}
		}
    GPU_Render();
}

static void G_Deinit(void)
{

}

const Callbacks G4_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
