// Connect4 1P

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
    C4Enum _playerAI;
    uint8_t _choicePosition;
} Game7Memory;

typedef struct {
    C4Enum _field[7][6];
    C4Enum _playerNext;
    uint8_t _choicePosition;
} Game8Memory;


static Game7Memory * const memory = (Game7Memory*)(&mainMemory.sharedMemory.memblocks[0]);

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
    // assign players
    memory->_playerNext = C4_PLAYER_1;
    memory->_playerAI = rand & 0x01 ? C4_PLAYER_1 : C4_PLAYER_2;
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

static void CalculateAIMove()
{
    C4Enum p1 = TogglePlayer(memory->_playerAI);
    // check if win -> play
    for (uint8_t col=0; col<7; col++)
    {
        uint8_t row = Drop(memory->_playerAI, col);
        if (row != -1)
        {
            C4Enum win = CheckWinner();
            if (win == memory->_playerAI || win == C4_PLAYER_DRAW)
            {
                // already dropped
                return;
            }
            else
            {
                memory->_field[col][row] = C4_EMPTY;
            }
        }
    }
    // check if lose -> play
    for (uint8_t col=0; col<7; col++)
    {
        uint8_t row = Drop(p1, col);
        if (row != -1)
        {
            C4Enum win = CheckWinner();
            memory->_field[col][row] = C4_EMPTY;
            if (win == p1)
            {
                Drop(memory->_playerAI, col);
                return;
            }
        }
    }
    // drop a random piece
    uint8_t rand = IF_Random() & 0x07;
    uint8_t pos = 0;
    while (1)
    {
        if (memory->_field[pos][0] == C4_EMPTY)
        {
            if (!rand)
                break;
            rand--;
        }
        pos = (pos+1)%7;
    }
    Drop(memory->_playerAI, pos);
}


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)


static void G_Update(void)
{
    KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	if (keyboard_held & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}

    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        if (mainMemory._gameState == GS_DONE)
            break;
        C4Enum gameOver;
        if (memory->_playerNext == memory->_playerAI)
        {
            CalculateAIMove();
            memory->_playerNext = TogglePlayer(memory->_playerNext);
            gameOver = CheckWinner();
            if (gameOver != C4_EMPTY)
            {
                mainMemory._gameState = GS_DONE;
            }
        }
        else
        {
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
    if (memory->_playerAI == C4_PLAYER_1)
    {
        GPU_DrawFilledCircle(C_YELLOW, GPU_X/2+BLOCK_SIZE/2, BLOCK_SIZE/2, BLOCK_SIZE/2-1);
        GPU_DrawFilledCircle(C_RED, GPU_X/2+BLOCK_SIZE/2, BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2-1);
    }
    else if (memory->_playerAI == C4_PLAYER_2)
    {
        GPU_DrawFilledCircle(C_RED, GPU_X/2+BLOCK_SIZE/2, BLOCK_SIZE/2, BLOCK_SIZE/2-1);
        GPU_DrawFilledCircle(C_YELLOW, GPU_X/2+BLOCK_SIZE/2, BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2-1);
    }
    const char * txtAI = "AI";
    GPU_DrawText(C_GRAY_MEDIUM, font_small, GPU_X/2+BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2, txtAI, strlen(txtAI));
    const char * txtPlayer = "Player";
    GPU_DrawText(C_GRAY_MEDIUM, font_small, GPU_X/2+BLOCK_SIZE/2+BLOCK_SIZE, BLOCK_SIZE/2+BLOCK_SIZE, txtPlayer, strlen(txtPlayer));
    if (mainMemory._gameState != GS_DONE)
    {
        if (memory->_playerAI == memory->_playerNext)
        {
            const char * txtCalculating = "CALCULATING";
            uint8_t len = strlen(txtCalculating);
            GPU_DrawText(C_RED, font_big, S2_CENTER_X-font_big->width*len/2, S2_CENTER_Y-font_big->height/2, txtCalculating, len);
        }
    }
    else
    {
        C4Enum p1 = TogglePlayer(memory->_playerAI);
        if (p1 == memory->_playerNext)
        {
            const char * txtAIWins = "AI WINS";
            uint8_t l = strlen(txtAIWins);
            GPU_DrawText(C_RED, font_big, S2_CENTER_X-l*font_big->width/2, S2_CENTER_Y-font_big->height/2, txtAIWins, strlen(txtAIWins));
        }
        else
        {
            const char * txtPlayerWins = "YOU WIN";
            uint8_t l = strlen(txtPlayerWins);
            GPU_DrawText(C_BLUE, font_big, S2_CENTER_X-l*font_big->width/2, S2_CENTER_Y-font_big->height/2, txtPlayerWins, strlen(txtPlayerWins));
        }
        // Draw reset button
		{
			const char * textToDisplay = "RESET";
			int8_t len = strlen(textToDisplay);
			GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
			GPU_DrawText(C_BLACK, font_big, RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
		}
    }
    GPU_Render();
}

static void G_Deinit(void)
{

}

const Callbacks G7_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
