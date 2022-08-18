#include "mainLoop.h"
#include "interface.h"
#include "gameshared.h"
#include "gpu.h"

#include <math.h>

#include "textures.h"

typedef struct {
    uint8_t _choiceStart;
    uint8_t _choiceCurrent;
    uint8_t _choiceEnd;
    uint8_t screenLines;
} MainScreenMemory;

static MainScreenMemory * const memory = (MainScreenMemory*)(&mainMemory.sharedMemory.memblocks[0]);

typedef struct {
    const char name[12];
    const Callbacks * callbacks;
} GameItem;

#define NUM_TICKS 10

static const FontDescription * f_big = &fontDescription[2];
static const FontDescription * f_huge = &fontDescription[3];

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

#define NO_OF_GAMES 12
static const GameItem listOfGames[NO_OF_GAMES] = {
    {"TicTacToe1P\0", &G1_Callbacks},
    {"TicTacToe2P\0", &G2_Callbacks},
    {"Lights Out \0", &G3_Callbacks},
    {"Math       \0", &G4_Callbacks},
    {"Tetramino  \0", &G5_Callbacks},
    {"Labirinth  \0", &G6_Callbacks},
    {"Connect4 1P\0", &G7_Callbacks},
    {"Connect4 2P\0", &G8_Callbacks},
    {"2048       \0", &G9_Callbacks},
    {"Paint.NOT  \0", &G10_Callbacks},
    {"Snake      \0", &G11_Callbacks},
	{"Calculator \0", &G12_Callbacks},
};

static void MM_Init(void)
{
    mainMemory._gameState = GS_NOT_STARTED;
	mainMemory._lastRenderedTime = 0;
	mainMemory.touch_X = 0;
	mainMemory.touch_Y = 0;
    memory->_choiceCurrent = 0;
    memory->_choiceStart = 0;
    memory->screenLines = GPU_Y/f_big->height;
    memory->_choiceEnd = (memory->screenLines - 1) < (NO_OF_GAMES-1) ? (memory->screenLines - 1) : (NO_OF_GAMES -1);
    Texture_CopyFrom(texture_rpi);
}

static void MM_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
    if (keyboard == KEY_NONE)
        return;
    if (keyboard & KEY_UP)
    {
        memory->_choiceCurrent = memory->_choiceCurrent - 1;
        if (memory->_choiceCurrent >= NO_OF_GAMES)
        {
            memory->_choiceCurrent = NO_OF_GAMES - 1;
            memory->_choiceEnd = NO_OF_GAMES -1;
            memory->_choiceStart = NO_OF_GAMES - memory->screenLines;
            if ( memory->_choiceStart > memory->_choiceEnd)
                memory->_choiceStart = 0;
        }
        else
        {
            if (memory->_choiceCurrent < memory->_choiceStart)
            {
                memory->_choiceStart--;
                memory->_choiceEnd--;
            }
        }
    }
    if (keyboard & KEY_DOWN)
    {
        memory->_choiceCurrent = memory->_choiceCurrent + 1;
        if (memory->_choiceCurrent >= NO_OF_GAMES)
        {
            memory->_choiceCurrent = 0;
            memory->_choiceStart = 0;
            memory->_choiceEnd = (memory->screenLines - 1) < (NO_OF_GAMES-1) ? (memory->screenLines - 1) : (NO_OF_GAMES -1);
        }
        else 
        {
            if (memory->_choiceCurrent > memory->_choiceEnd)
            {
                memory->_choiceStart++;
                memory->_choiceEnd++;
            }
        }
    }
    if (keyboard & KEY_RIGHT)
    {
        const GameItem * selectedGame = &listOfGames[memory->_choiceCurrent];
        if (selectedGame->callbacks)
            SetupCallbacks(selectedGame->callbacks);
    }
}


static void MM_Draw(void)
{
    GPU_ClearFramebuffers();
    
    for ( uint8_t i=memory->_choiceStart; i<=memory->_choiceEnd; i++ )
    {
        int j = i - memory->_choiceStart;
        if ( memory->_choiceCurrent == i )
        {
            GPU_DrawFilledSquare(C_YELLOW, 0, j*f_big->height, GPU_X/2, f_big->height + 2);
            GPU_DrawText(C_RED, f_big, GPU_X/4-(11)*f_big->width/2, j*f_big->height+2, listOfGames[i].name, 11);
        }
        else
        {
            GPU_DrawText(C_WHITE, f_big, 3, j*f_big->height+2, listOfGames[i].name, 11);
        }
    }
    // Texture already copied during reset
    //Texture_CopyFrom(texture_rpi);
    uint32_t tick = CalculateTick();
    double s = sin(tick*0.02);
    GPU_DrawRotatedTexture(GPU_X/2+GPU_X/4, GPU_Y/2, 0.7+s*s, tick*0.02);
}

static void MM_Deinit(void)
{

}

const Callbacks MM_Callbacks = { &MM_Init, &MM_Update, &MM_Draw, &MM_Deinit };
