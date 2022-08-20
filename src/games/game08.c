// Connect4 2P

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>

typedef enum {
    C4_EMPTY,
    C4_PLAYER_1,
    C4_PLAYER_2,
    C4_PLAYER_DRAW
} C4Enum;

typedef struct {
    C4Enum _field[7][6];
    C4Enum _playerNext;
    uint8_t _choicePosition;
} Game8Memory;

static Game8Memory * const memory = (Game8Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static C4Enum TogglePlayer(C4Enum player)
{
    return player == C4_PLAYER_1 ? C4_PLAYER_2 : C4_PLAYER_1;
}

#define NUM_TICKS 4

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    for (uint8_t i=0; i<7; i++)
        for (uint8_t j=0; j<6; j++)
            memory->_field[i][j] = C4_EMPTY;
    uint32_t rand = IF_Random();
    memory->_playerNext = C4_PLAYER_1;
    memory->_choicePosition = 0;
    mainMemory._lastRenderedTick = CalculateTick();
}

static uint8_t Drop(C4Enum player, uint8_t row)
{
    if (memory->_field[row][0] != C4_EMPTY)
        return -1;
    uint8_t max = 0;
    for (uint8_t i=0; i<6; i++)
        if(memory->_field[row][i] == C4_EMPTY)
        {
            max = i;
        }
        else
        {
            break;
        }
    memory->_field[row][max] = player;
    return max;
}

static C4Enum CheckWinner()
{
    C4Enum result = C4_PLAYER_DRAW;
    // Check horizontal
    for (uint8_t i=0; i<7-3; i++)
        for (uint8_t j=0; j<6; j++)
        {
            C4Enum r = memory->_field[i][j];
            if (r == C4_EMPTY)
                continue;
            uint8_t k=1;
            for (; k<4; k++)
                if (memory->_field[i+k][j] != r)
                    break;
            if (k == 4)
            {
                return r;
            }
                
        }
    // Check vertical
    for (uint8_t i=0; i<7; i++)
        for (uint8_t j=0; j<6-3; j++)
        {
            C4Enum r = memory->_field[i][j];
            if (r == C4_EMPTY)
                continue;
            uint8_t k=1;
            for (; k<4; k++)
                if (memory->_field[i][j+k] != r)
                    break;
            if (k == 4)
            {
                return r;
            }
        }
    // Check diagonal 1
    for (uint8_t i=0; i<7-3; i++)
        for (uint8_t j=0; j<6-3; j++)
        {
            C4Enum r = memory->_field[i][j];
            if (r == C4_EMPTY)
                continue;
            uint8_t k=1;
            for (; k<4; k++)
                if (memory->_field[i+k][j+k] != r)
                    break;
            if (k == 4)
            {
                return r;
            }
        }
    // Check diagonal 2
    for (uint8_t i=3; i<7; i++)
        for (uint8_t j=0; j<6-3; j++)
        {
            C4Enum r = memory->_field[i][j];
            if (r == C4_EMPTY)
                continue;
            uint8_t k=1;
            for (; k<4; k++)
                if (memory->_field[i-k][j+k] != r)
                    break;
            if (k == 4)
            {
                return r;
            }
        }
    // Check is draw or still running
    for (uint8_t i=0; i<7; i++)
        for (uint8_t j=0; j<6; j++)
        {
            if (memory->_field[i][j] == C4_EMPTY)
            {
                result = C4_EMPTY;
                break;
            }
        }
    return result;
}


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        if (mainMemory._gameState == GS_DONE)
            break;
        C4Enum gameOver;
	    KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
        if (keyboard != KEY_NONE)
        {
            if (keyboard & KEY_LEFT)
            {
                if (memory->_choicePosition > 0)
                    memory->_choicePosition--;
            }
            if (keyboard & KEY_RIGHT)
            {
                if (memory->_choicePosition < 6)
                    memory->_choicePosition++;
            }
            if (keyboard & KEY_DOWN)
            {
                // drop and change player
                if (Drop(memory->_playerNext, memory->_choicePosition) != -1)
                {
                    memory->_playerNext = TogglePlayer(memory->_playerNext);
                }
                gameOver = CheckWinner();
                if (gameOver != C4_EMPTY)
                {
                    mainMemory._gameState = GS_DONE;
                }
            }
            keyboard = KEY_NONE;
        }
        mainMemory._lastRenderedTick++;
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

#define BLOCK_SIZE ( (GPU_X/2 < GPU_Y) ? GPU_X/14 : GPU_Y/7 )
#define START_X (GPU_X/2 - 7*BLOCK_SIZE)/2
#define START_Y (GPU_Y - 7*BLOCK_SIZE)/2

static void G_Draw(void)
{
    const FontDescription * font_small = &fontDescription[0];
    const FontDescription * font_m = &fontDescription[1];
    const FontDescription * font_big = &fontDescription[2];
    GPU_ClearFramebuffers();
    // Draw Board
    for (uint8_t i=0; i<7; i++)
        for (uint8_t j=0; j<6; j++)
        {
            GPU_DrawEmptySquare(C_BLUE, START_X + i*BLOCK_SIZE, START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1, BLOCK_SIZE-1, BLOCK_SIZE-1);
            if (memory->_field[i][j]==C4_PLAYER_1)
                GPU_DrawFilledCircle(C_YELLOW, START_X+i*BLOCK_SIZE+BLOCK_SIZE/2, START_Y+j*BLOCK_SIZE+BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2-1);
            else if (memory->_field[i][j]==C4_PLAYER_2)
                GPU_DrawFilledCircle(C_RED, START_X+i*BLOCK_SIZE+BLOCK_SIZE/2, START_Y+j*BLOCK_SIZE+BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2-1);
        }
    // Draw Choice position
    if (memory->_playerNext == C4_PLAYER_1)
        GPU_DrawFilledCircle(C_YELLOW, START_X + memory->_choicePosition*BLOCK_SIZE+BLOCK_SIZE/2, START_Y+BLOCK_SIZE/2, BLOCK_SIZE/2-1);
    else if (memory->_playerNext == C4_PLAYER_2)
        GPU_DrawFilledCircle(C_RED, START_X + memory->_choicePosition*BLOCK_SIZE+BLOCK_SIZE/2, START_Y+BLOCK_SIZE/2, BLOCK_SIZE/2-1);
    // Draw on second screen
    GPU_DrawFilledCircle(C_YELLOW, GPU_X/2+BLOCK_SIZE/2, BLOCK_SIZE/2, BLOCK_SIZE/2-1);
    GPU_DrawFilledCircle(C_RED, GPU_X/2+BLOCK_SIZE/2, BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2-1);
    
    const char * txtPlayer1 = "Player 1";
    GPU_DrawText(C_GRAY_MEDIUM, font_small, GPU_X/2+BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2, txtPlayer1, strlen(txtPlayer1));
    const char * txtPlayer2 = "Player 2";
    GPU_DrawText(C_GRAY_MEDIUM, font_small, GPU_X/2+BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2, txtPlayer2, strlen(txtPlayer2));
    if (mainMemory._gameState == GS_DONE)
    {
        if (memory->_playerNext == C4_PLAYER_2)
        {
            const char * txtPlayer1Wins = "PLAYER 1 WINS";
            uint8_t l = strlen(txtPlayer1Wins);
            GPU_DrawText(C_BLUE, font_big, S2_CENTER_X-l*font_big->width/2, S2_CENTER_Y-font_big->height/2, txtPlayer1Wins, strlen(txtPlayer1Wins));
        }
        else
        {
            const char * txtPlayer2Wins = "PLAYER 2 WINS";
            uint8_t l = strlen(txtPlayer2Wins);
            GPU_DrawText(C_BLUE, font_big, S2_CENTER_X-l*font_big->width/2, S2_CENTER_Y-font_big->height/2, txtPlayer2Wins, strlen(txtPlayer2Wins));
        }
        // Draw reset button
		{
			const char * textToDisplay = "RESET";
			int8_t len = strlen(textToDisplay);
			GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
			GPU_DrawText(C_BLACK, font_big, RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
		}
    }
    else
    {
       
    }
}

static void G_Deinit(void)
{

}

const Callbacks G8_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
