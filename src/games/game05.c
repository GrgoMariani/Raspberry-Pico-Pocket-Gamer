// Tetramino

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    TF_EMPTY,
    TF_FULL,
    TF_OBJECT
} Mino;

typedef enum {
    TTRMN_I,
    TTRMN_J,
    TTRMN_L,
    TTRMN_O,
    TTRMN_S,
    TTRMN_T,
    TTRMN_Z,
} Tetramino;

typedef struct {
    Mino _field[20][10];
    uint32_t _score;
    Mino _activeTetramino[4][4];
    uint8_t _activeWH;
    Mino _nextTetramino[4][4];
    uint8_t _nextWH;
    int8_t _activeX;
    int8_t _activeY;
} Game5Memory;

static Game5Memory * const memory = (Game5Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static Tetramino GetRandomTetramino()
{
    uint8_t r = IF_Random() % 7;
    if (r == 0) return TTRMN_I;
    if (r == 1) return TTRMN_O;
    if (r == 2) return TTRMN_L;
    if (r == 3) return TTRMN_J;
    if (r == 4) return TTRMN_S;
    if (r == 5) return TTRMN_Z;
    return TTRMN_T;
}

#define NUM_TICKS 15

static uint32_t CalculateCurrentTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void _GenerateNextTetramino()
{
    Tetramino randTetramino = GetRandomTetramino();
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            memory->_nextTetramino[i][j] = TF_EMPTY;
    switch (randTetramino)
    {
    case TTRMN_I:
        memory->_nextWH = 4;
        for (uint8_t i=0; i<4; i++)
            memory->_nextTetramino[2][i] = TF_OBJECT;    
        break;
    case TTRMN_J:
        memory->_nextWH = 3;
        for (uint8_t i=0; i<3; i++)
            memory->_nextTetramino[2][i] = TF_OBJECT;
        memory->_nextTetramino[1][2] = TF_OBJECT;
        break;
    case TTRMN_L:
        memory->_nextWH = 3;
        for (uint8_t i=0; i<3; i++)
            memory->_nextTetramino[0][i] = TF_OBJECT;
        memory->_nextTetramino[1][2] = TF_OBJECT;
        break;
    case TTRMN_O:
        memory->_nextWH = 2;
        for (uint8_t i=0; i<2; i++)
        {
            memory->_nextTetramino[0][i] = TF_OBJECT;
            memory->_nextTetramino[1][i] = TF_OBJECT;
        }
        break;
    case TTRMN_S:
        memory->_nextWH = 3;
        for (uint8_t i=0; i<2; i++)
        {
            memory->_nextTetramino[0][i] = TF_OBJECT;
            memory->_nextTetramino[1][i+1] = TF_OBJECT;
        }
        break;
    case TTRMN_T:
        memory->_nextWH = 3;
        for (uint8_t i=0; i<3; i++)
            memory->_nextTetramino[0][i] = TF_OBJECT;
        memory->_nextTetramino[1][1] = TF_OBJECT;
        break;
    case TTRMN_Z:
        memory->_nextWH = 3;
        for (uint8_t i=0; i<2; i++)
        {
            memory->_nextTetramino[1][i] = TF_OBJECT;
            memory->_nextTetramino[0][i+1] = TF_OBJECT;
        }
        break;
    default:
        break;
    };
}

static uint8_t _CheckCollision(int8_t x, int8_t y)
{
    if (x<0 || y<0 || x>=10 || y>=20)
        return 1;
    if (memory->_field[y][x] == TF_FULL)
        return 1;
    return 0;
}

static void _PushTetramino()
{
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            memory->_activeTetramino[i][j] = memory->_nextTetramino[i][j];
    memory->_activeWH = memory->_nextWH;
    memory->_activeX = 4;
    memory->_activeY = 0;
    _GenerateNextTetramino();
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            {
                if (_CheckCollision(memory->_activeX+i, memory->_activeY+j))
                    {
                        mainMemory._gameState = GS_DONE;
                    }
            }
}

static void _MoveTetraminoLeft()
{
    for (uint8_t i = 0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (memory->_activeTetramino[i][j] == TF_OBJECT && _CheckCollision(i + memory->_activeX-1, j + memory->_activeY))
                return;
        }
    memory->_activeX--;
}

static void _MoveTetraminoRight()
{
    for (uint8_t i = 0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (memory->_activeTetramino[i][j] == TF_OBJECT && _CheckCollision(i + memory->_activeX+1, j + memory->_activeY))
                return;
        }
    memory->_activeX++;
}

static uint8_t _MoveTetraminoDown()
{
    for (uint8_t i = 0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (memory->_activeTetramino[i][j] == TF_OBJECT && _CheckCollision(i + memory->_activeX, j + memory->_activeY+1))
                return 0;
        }
    memory->_activeY++;
    return 1;
}

static void _NaturalFixActiveTetramino()
{
    for (uint8_t i=0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (memory->_activeTetramino[i][j] == TF_OBJECT)
                memory->_field[memory->_activeY+j][memory->_activeX+i] = TF_FULL;
        }
}

static uint8_t _CheckRow(uint8_t y)
{
    for (int8_t i=0; i<10; i++)
        if (memory->_field[y][i] == TF_EMPTY)
            return 0;
    return 1;
}

static void _CheckField()
{
    uint8_t final_score = 0;
    for (int8_t i=19; i>0; i--)
    {
        if (_CheckRow(i))
        {
            final_score++;
            for (int8_t j=i; j>0; j--)
            {
                for (int8_t k=0; k<10; k++)
                {
                    memory->_field[j][k] = memory->_field[j-1][k];
                    memory->_field[j-1][k] = TF_EMPTY;
                }
            }
            i++;
        }
    }
    memory->_score += final_score*(final_score+1)/2;
}

static void _DropTetramino()
{
    if (!_MoveTetraminoDown())
    {
        _NaturalFixActiveTetramino();
        _CheckField();
        _PushTetramino();
    }
}

static void _RotateTetramino()
{
    Mino rotated[4][4];
    // copy
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
        {
            rotated[i][j] = memory->_activeTetramino[i][j];
        }
    // invert by x
    for (uint8_t i=0; i<memory->_activeWH/2; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            Mino temp = rotated[i][j];
            rotated[i][j] = rotated[memory->_activeWH-1-i][j];
            rotated[memory->_activeWH-1-i][j] = temp;
        }
    // invert by diagonal
    for (uint8_t i=0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<i; j++)
        {
            Mino temp = rotated[i][j];
            rotated[i][j] = rotated[j][i];
            rotated[j][i] = temp;
        }
    // Check for collisions
    for (uint8_t i = 0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (rotated[i][j] == TF_OBJECT && _CheckCollision(i + memory->_activeX, j + memory->_activeY))
                return;
        }
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
        {
            memory->_activeTetramino[i][j] = rotated[i][j];
        }
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    for (uint8_t i=0; i<20; i++)
        for (uint8_t j=0; j<10; j++)
            memory->_field[i][j] = TF_EMPTY;
    memory->_score = 0;
    mainMemory._lastRenderedTick = CalculateCurrentTick();
    memory->_activeX = 0;
    memory->_activeY = 0;
    _GenerateNextTetramino();
    _PushTetramino();
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

    if (mainMemory._gameState == GS_RUNNING)
    {
        uint32_t tick = CalculateCurrentTick();
        while (mainMemory._lastRenderedTick < tick)
        {
            KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
            if (keyboard != KEY_NONE)
            {
                if (keyboard & KEY_LEFT)
                {
                    _MoveTetraminoLeft();
                }
                if (keyboard & KEY_RIGHT)
                {
                    _MoveTetraminoRight();
                }
                if (keyboard & KEY_UP)
                {
                    _RotateTetramino();
                }
				keyboard = KEY_NONE;
            }
            if (keyboard_held != KEY_NONE)
            {
            	if (keyboard_held & KEY_DOWN)
				{
					_DropTetramino();
				}
            	keyboard_held = KEY_NONE;
            }
            
            // on several ticks simply move it down
            if (mainMemory._lastRenderedTick%(NUM_TICKS/2) == 0)
            {
                if (!_MoveTetraminoDown())
                {
                    _NaturalFixActiveTetramino();
                    _CheckField();
                    _PushTetramino();
                }
            }
            mainMemory._lastRenderedTick++;
        }
    }
    else
	{
		if (mainMemory.touchPressed)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
	}
}

#define BLOCK_SIZE  ( (GPU_Y > GPU_X/2) ? (GPU_Y/20) : (GPU_X/20))
#define START_X ( (GPU_X/2 - BLOCK_SIZE*10)/2 )
#define START_Y ( (GPU_Y - BLOCK_SIZE*20)/2 )

static void G_Draw(void)
{
    const FontDescription * f = &fontDescription[1];
    GPU_ClearFramebuffers();
    // Draw map
    for (uint8_t i=0; i<20; i++)
        for (uint8_t j=0; j<10; j++)
        {
            if (memory->_field[i][j] == TF_EMPTY)
            {
                GPU_DrawEmptySquare(C_GRAY_LIGHT, START_X + BLOCK_SIZE*j, START_Y + BLOCK_SIZE*i, BLOCK_SIZE-1, BLOCK_SIZE-1);
            }
            else if (memory->_field[i][j] == TF_FULL)
            {
                GPU_DrawFilledSquare(C_GRAY_MEDIUM, START_X + BLOCK_SIZE*j, START_Y + BLOCK_SIZE*i, BLOCK_SIZE-1, BLOCK_SIZE-1);
            }
            else if (memory->_field[i][j] == TF_OBJECT)
            {

            }
        }
    // Draw current
    for (uint8_t i=0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (memory->_activeTetramino[i][j] == TF_OBJECT)
                GPU_DrawFilledSquare(C_YELLOW, START_X + BLOCK_SIZE*(i + memory->_activeX), START_Y + BLOCK_SIZE*(j + memory->_activeY), BLOCK_SIZE-1, BLOCK_SIZE-1);
        }
    // Draw next
    const char * txtNextOne = "Next:";
    GPU_DrawText(C_WHITE, f, GPU_X/2+16, GPU_Y/8, txtNextOne, strlen(txtNextOne));
    for (uint8_t i=0; i<memory->_nextWH; i++)
        for (uint8_t j=0; j<memory->_nextWH; j++)
        {
            if (memory->_nextTetramino[j][i] == TF_OBJECT)
                GPU_DrawFilledSquare(C_GRAY_MEDIUM, GPU_X/2+START_X+BLOCK_SIZE*j, GPU_Y/4+BLOCK_SIZE*i, BLOCK_SIZE-1, BLOCK_SIZE-1);
        }
    // Draw score
    const char * txtScore = "Score:";
    GPU_DrawText(C_BLUE, f, GPU_X/2+GPU_X/8, GPU_Y/2, txtScore, strlen(txtScore));
    char txtScoreInt[9];
    sprintf(txtScoreInt, "%d", memory->_score);
    uint8_t lenScore = strlen(txtScoreInt);
    GPU_DrawText(C_BLUE, f, S2_CENTER_X - (lenScore-1)*f->width, GPU_Y/2+2*f->height, txtScoreInt, lenScore);
    if (mainMemory._gameState == GS_DONE)
    {
        const FontDescription * f_big = &fontDescription[3];
        const char * txtGameOver1 = "GAME";
        const char * txtGameOver2 = "OVER";
        uint8_t goLen1 = strlen(txtGameOver1);
        uint8_t goLen2 = strlen(txtGameOver2);
        GPU_DrawFilledSquare(C_WHITE, 0, GPU_Y/4, GPU_X/2, GPU_Y/2);
        GPU_DrawText(C_RED, f_big, S1_CENTER_X - (goLen1)*f_big->width/2, S1_CENTER_Y - f_big->height, txtGameOver1, goLen1);
        GPU_DrawText(C_RED, f_big, S1_CENTER_X - (goLen2)*f_big->width/2, S1_CENTER_Y, txtGameOver2, goLen2);
        // Draw reset button
		{
			const char * textToDisplay = "RESET";
			int8_t len = strlen(textToDisplay);
			GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
			GPU_DrawText(C_BLACK, f_big, RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
		}
    }
    GPU_Render();
}

static void G_Deinit(void)
{

}

const Callbacks G5_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
