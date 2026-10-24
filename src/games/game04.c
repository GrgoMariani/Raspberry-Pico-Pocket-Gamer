// MATH - answer the sum by tapping the result

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
    uint8_t is_touch_held;
    uint8_t _flash;
    uint8_t _flashAnswer;
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
    uint8_t rand = (uint8_t)Random_Range(num);
    memory->_x = (uint8_t)(num - rand);
    memory->_y = rand;
}

static void GenerateSubtraction(uint8_t num)
{
    uint8_t rand = (uint8_t)Random_Range(255-num);
    memory->_x = (uint8_t)(num + rand);
    memory->_y = rand;
}

static void GenerateMultiplication(uint8_t num)
{
    if (!num)
    {
        while (!memory->_answer)
            memory->_answer = (uint8_t)Random_Range(16);
        num = memory->_answer;
    }
    uint8_t rand = (uint8_t)(Random_Range(num) + 1);
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
    memory->_x = (uint8_t)i;
    memory->_y = (uint8_t)(num/i);
}

static void GenerateDivision(uint8_t num)
{
    if (!num)
    {
        while (!memory->_answer)
            memory->_answer = (uint8_t)Random_Range(16);
        num = memory->_answer;
    }
    memory->_y = (uint8_t)((Random_Next() & 0x0f) + 1);
    memory->_x = (uint8_t)(memory->_y * num);
}

static void GenerateNewAnswer()
{
    uint32_t rand = Random_Next();
    memory->_answer = (uint8_t)(rand & 0x0f);
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
    default:
        memory->_arithmeticOperation = AO_MULTIPLY;
        GenerateMultiplication(memory->_answer);
        break;
    }
}

static void G_Init(void)
{
    memory->_lastGuess = GR_NO_GUESS;
    memory->_guessesCorrect = 0;
    memory->_guessesTotal = 0;
    memory->is_touch_held = 0;
    memory->_flash = 0;
    GenerateNewAnswer();
    mainMemory._gameState = GS_RUNNING;
}


#define BOX_W ((GPU_X/2)/4)
#define BOX_H (GPU_Y/4)

static void Answer(uint8_t answer)
{
    if (answer == memory->_answer)
    {
        memory->_lastGuess = GR_CORRECT;
        memory->_guessesCorrect++;
    }
    else
    {
        memory->_lastGuess = GR_WRONG;
    }
    memory->_guessesTotal++;
    memory->_flash = 8;
    memory->_flashAnswer = answer;
    GenerateNewAnswer();
}

static void G_Update(void)
{
    if (memory->_flash)
        memory->_flash--;
    if (mainMemory.touchPressedNew)
    {
        if (mainMemory.touch_X >= GPU_X/2 && mainMemory.touch_X < GPU_X &&
            mainMemory.touch_Y < GPU_Y)
        {
            uint8_t x = (uint8_t)((mainMemory.touch_X - GPU_X/2)/BOX_W);
            uint8_t y = (uint8_t)(mainMemory.touch_Y/BOX_H);
            if (x < 4 && y < 4)
                Answer((uint8_t)(4*y + x));
        }
    }
}


static void G_Draw(void)
{
    const FontDescription * f = &fontDescription[0];
    const FontDescription * f_big = &fontDescription[2];
    const FontDescription * f_huge = &fontDescription[3];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,14,40), C_BLACK, 0, 0, GPU_X/2, GPU_Y);
    char buffer[16];
    uint8_t ll;

    /* the sum */
    GPU_DrawPanel(RGB(16,20,56), C_C1, 8, 8, (int16_t)(GPU_X/2-16), 150);
    {
        sprintf(buffer, "%u", memory->_x);
        ll = (uint8_t)strlen(buffer);
        GPU_DrawTextCentered(C_GREEN, f_huge, GPU_X/4, 24, buffer);
        char symbol;
        switch (memory->_arithmeticOperation)
        {
        case AO_PLUS:     symbol = '+'; break;
        case AO_MINUS:    symbol = '-'; break;
        case AO_DIVIDE:   symbol = '/'; break;
        case AO_MULTIPLY: symbol = '*'; break;
        default:          symbol = ' '; break;
        }
        GPU_DrawLetter(C_YELLOW, f_huge, (int16_t)(GPU_X/4 - f_huge->width/2), 66, symbol);
        sprintf(buffer, "%u", memory->_y);
        GPU_DrawTextCentered(C_GREEN, f_huge, GPU_X/4, 108, buffer);
        GPU_DrawTextCentered(C_WHITE, f_huge, GPU_X/4, 150, "= ?");
    }

    /* feedback */
    if (memory->_flash)
    {
        if (memory->_lastGuess == GR_CORRECT)
            GPU_DrawTextCentered(C_C3, f_big, GPU_X/4, 176, "CORRECT!");
        else
            GPU_DrawTextCentered(C_RED, f_big, GPU_X/4, 176, "WRONG!");
    }

    /* score */
    {
        GPU_DrawPanel(RGB(16,20,56), C_GRAY_DARK, 8, 200, (int16_t)(GPU_X/2-16), 32);
        ll = (uint8_t)sprintf(buffer, "%u/%u", (unsigned)memory->_guessesCorrect, (unsigned)memory->_guessesTotal);
        GPU_DrawText(C_WHITE, f, 14, 206, buffer, ll);
        if (memory->_guessesTotal)
        {
            ll = (uint8_t)sprintf(buffer, "%u%%", (unsigned)(memory->_guessesCorrect*100/memory->_guessesTotal));
            GPU_DrawText(C_YELLOW, f, 90, 206, buffer, ll);
        }
    }

    /* the keypad */
    const char nums[16] = "0123456789ABCDEF";
    for (uint8_t i=0; i<4; i++)
		for (uint8_t j=0; j<4; j++)
		{
			uint8_t value = (uint8_t)(4*j+i);
			GPU_Color key = (memory->_flash && value == memory->_flashAnswer)
			                ? ((memory->_lastGuess == GR_CORRECT) ? C_C3 : C_RED) : C_WHITE;
			GPU_DrawBevelSquare(key, (int16_t)(GPU_X/2+i*BOX_W+2), (int16_t)(j*BOX_H+2),
			                    (int16_t)(BOX_W-4), (int16_t)(BOX_H-4));
			char label[2] = { nums[value], 0 };
			if (value < 10)
			{
				GPU_DrawTextCentered(C_BLACK, f_huge, (int16_t)(GPU_X/2+i*BOX_W+BOX_W/2),
				                     (int16_t)(j*BOX_H+BOX_H/2-f_huge->height/2), label);
			}
			else
			{
				GPU_DrawTextCentered(C_BLACK, f_big, (int16_t)(GPU_X/2+i*BOX_W+BOX_W/2),
				                     (int16_t)(j*BOX_H+BOX_H/2-f_big->height/2), label);
			}
		}
}

static void G_Deinit(void)
{

}

const Callbacks G4_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
