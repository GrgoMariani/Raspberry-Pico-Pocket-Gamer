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
    /* field cells: 0 = empty, 1..7 = locked piece of that colour index + 1 */
    uint8_t _field[20][10];
    uint32_t _score;
    uint32_t _lines;
    Mino _activeTetramino[4][4];
    uint8_t _activeWH;
    Mino _nextTetramino[4][4];
    uint8_t _nextWH;
    int8_t _activeX;
    int8_t _activeY;
    GPU_Color _activeColor;
    GPU_Color _nextColor;
    uint8_t _activeColorIdx;
    uint8_t _nextColorIdx;
    uint8_t _clearFlash;      /* ticks remaining of the line-clear flash */
    uint8_t _lastClear;       /* rows cleared by the last lock */
} Game5Memory;

/* one colour per shape, like the modern games do it */
static const GPU_Color _pieceColor[7] = {
    C_CYAN, C_BLUE, C_ORANGE, C_YELLOW, C_GREEN, C_MAGENTA, C_RED
};

static Game5Memory * const memory = (Game5Memory*)(&mainMemory.sharedMemory.memblocks[0]);

/* 7-bag randomizer: the seven pieces are shuffled into a bag and dealt one
   by one, so every shape appears exactly once per seven pieces. Pure random
   draws used to produce long droughts of the I piece (and floods of S/Z). */
static uint8_t _bag[7];
static uint8_t _bagPos = 7;

static Tetramino GetRandomTetramino()
{
    if (_bagPos >= 7)
    {
        for (uint8_t i = 0; i < 7; i++)
            _bag[i] = i;
        for (uint8_t i = 6; i > 0; i--)
        {
            uint8_t j = (uint8_t)Random_Range(i + 1);
            uint8_t t = _bag[i];
            _bag[i] = _bag[j];
            _bag[j] = t;
        }
        _bagPos = 0;
    }
    return (Tetramino)_bag[_bagPos++];
}

#define NUM_TICKS 15

static uint32_t CalculateCurrentTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void _GenerateNextTetramino()
{
    Tetramino randTetramino = GetRandomTetramino();
    memory->_nextColor = _pieceColor[randTetramino];
    memory->_nextColorIdx = (uint8_t)randTetramino;
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
    if (memory->_field[y][x] != 0)
        return 1;
    return 0;
}

static void _PushTetramino()
{
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            memory->_activeTetramino[i][j] = memory->_nextTetramino[i][j];
    memory->_activeWH = memory->_nextWH;
    memory->_activeColor = memory->_nextColor;
    memory->_activeColorIdx = memory->_nextColorIdx;
    memory->_activeX = 4;
    memory->_activeY = 0;
    _GenerateNextTetramino();
    /* game over only when a cell of the new piece really collides, not when the
       empty part of its 4x4 box touches the stack */
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            {
                if (memory->_activeTetramino[i][j] == TF_OBJECT &&
                    _CheckCollision(memory->_activeX+i, memory->_activeY+j))
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
                memory->_field[memory->_activeY+j][memory->_activeX+i] = (uint8_t)(memory->_activeColorIdx + 1);
        }
}

static uint8_t _CheckRow(uint8_t y)
{
    for (int8_t i=0; i<10; i++)
        if (memory->_field[y][i] == 0)
            return 0;
    return 1;
}

static void _CheckField()
{
    uint8_t final_score = 0;
    for (int8_t i=19; i>=0; i--)
    {
        if (_CheckRow(i))
        {
            final_score++;
            for (int8_t j=i; j>0; j--)
            {
                for (int8_t k=0; k<10; k++)
                {
                    memory->_field[j][k] = memory->_field[j-1][k];
                    memory->_field[j-1][k] = 0;
                }
            }
            if (i == 0)
            {
                /* the top row has nothing above it to shift down */
                for (int8_t k=0; k<10; k++)
                    memory->_field[0][k] = 0;
            }
            else
            {
                /* the row that slid down into this place has to be checked too */
                i++;
            }
        }
    }
    memory->_lines += final_score;
    /* 1/3/6/10 points for 1..4 rows, times the current level */
    memory->_score += (uint32_t)(final_score*(final_score+1)/2) * (memory->_lines/10 + 1);
    if (final_score)
    {
        memory->_clearFlash = 8;
        memory->_lastClear = final_score;
    }
}

/* the row the active piece would land on if dropped straight down */
static int8_t _GhostY(void)
{
    int8_t gy = memory->_activeY;
    while (gy < 20)
    {
        uint8_t hit = 0;
        for (uint8_t i = 0; i < memory->_activeWH && !hit; i++)
            for (uint8_t j = 0; j < memory->_activeWH && !hit; j++)
                if (memory->_activeTetramino[i][j] == TF_OBJECT &&
                    _CheckCollision((int8_t)(i + memory->_activeX), (int8_t)(j + gy + 1)))
                    hit = 1;
        if (hit)
            break;
        gy++;
    }
    return gy;
}

/* gravity speeds up with every ten cleared rows */
static uint32_t _GravityInterval(void)
{
    uint32_t level = memory->_lines/10;
    uint32_t interval = NUM_TICKS/2 - level;
    if (interval < 2)
        interval = 2;
    return interval;
}

/* soft drop: one point per cell, like the modern games */
static void _SoftDrop()
{
    if (_MoveTetraminoDown())
        memory->_score += 1;
    else
    {
        _NaturalFixActiveTetramino();
        _CheckField();
        _PushTetramino();
    }
}

/* instant drop to the ghost position, rewarding the player for the distance */
static void _HardDrop(void)
{
    uint32_t cells = 0;
    while (_MoveTetraminoDown())
        cells++;
    memory->_score += cells;
    _NaturalFixActiveTetramino();
    _CheckField();
    _PushTetramino();
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
    /* check for collisions, with wall kicks: a piece that does not fit in
       place is tried one and two cells to the left and to the right, so that
       rotating next to a wall or the stack still works */
    static const int8_t kicks[5] = { 0, -1, 1, -2, 2 };
    uint8_t k;
    for (k = 0; k < 5; k++)
    {
        uint8_t fits = 1;
        for (uint8_t i = 0; i<memory->_activeWH && fits; i++)
            for (uint8_t j=0; j<memory->_activeWH && fits; j++)
            {
                if (rotated[i][j] == TF_OBJECT &&
                    _CheckCollision((int8_t)(i + memory->_activeX + kicks[k]), (int8_t)(j + memory->_activeY)))
                    fits = 0;
            }
        if (fits)
            break;
    }
    if (k == 5)
        return;
    memory->_activeX = (int8_t)(memory->_activeX + kicks[k]);
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
            memory->_field[i][j] = 0;
    memory->_score = 0;
    memory->_lines = 0;
    memory->_clearFlash = 0;
    memory->_lastClear = 0;
    _bagPos = 7;   /* a fresh bag for every game */
    mainMemory._lastRenderedTick = CalculateCurrentTick();
    memory->_activeX = 0;
    memory->_activeY = 0;
    /* a key that was pressed before the reset must not move the new piece */
    Keyboard_GetPressedKeys();
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

    if (mainMemory._gameState == GS_RUNNING)
    {
        /* a tap on the playfield hard-drops the piece */
        uint16_t tx, ty;
        if (Touch_TakeTap(&tx, &ty) && tx < GPU_X/2)
            _HardDrop();
        uint32_t tick = CalculateCurrentTick();
        while (mainMemory._lastRenderedTick < tick)
        {
            /* the flash decays on game ticks, not main loop iterations (the
               loop spins far faster, so it used to vanish within milliseconds) */
            if (memory->_clearFlash)
                memory->_clearFlash--;
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
					_SoftDrop();
				}
            	keyboard_held = KEY_NONE;
            }
            
            // on several ticks simply move it down, faster in higher levels
            if (mainMemory._lastRenderedTick % _GravityInterval() == 0)
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
		if (mainMemory.touchPressedNew)
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
    GPU_DrawPanel(RGB(10,12,30), C_GRAY_DARK, (int16_t)(START_X-3), (int16_t)(START_Y-3),
                  (int16_t)(10*BLOCK_SIZE+6), (int16_t)(20*BLOCK_SIZE+6));
    for (uint8_t i=0; i<20; i++)
        for (uint8_t j=0; j<10; j++)
        {
            if (memory->_field[i][j] == 0)
            {
                GPU_DrawPixel(RGB(40,44,64), (uint16_t)(START_X + BLOCK_SIZE*j + BLOCK_SIZE/2),
                              (uint16_t)(START_Y + BLOCK_SIZE*i + BLOCK_SIZE/2));
            }
            else
            {
                GPU_DrawBevelSquare(_pieceColor[memory->_field[i][j]-1], START_X + BLOCK_SIZE*j, START_Y + BLOCK_SIZE*i, BLOCK_SIZE-1, BLOCK_SIZE-1);
            }
        }
    // Draw ghost (landing preview) then the current piece
    if (mainMemory._gameState == GS_RUNNING)
    {
        int8_t gy = _GhostY();
        if (gy > memory->_activeY)
        {
            GPU_Color ghost = GPU_ShadeColor(memory->_activeColor, 70);
            for (uint8_t i=0; i<memory->_activeWH; i++)
                for (uint8_t j=0; j<memory->_activeWH; j++)
                    if (memory->_activeTetramino[i][j] == TF_OBJECT)
                        GPU_DrawEmptySquare(ghost, START_X + BLOCK_SIZE*(i + memory->_activeX), START_Y + BLOCK_SIZE*(j + gy), BLOCK_SIZE-1, BLOCK_SIZE-1);
        }
    }
    for (uint8_t i=0; i<memory->_activeWH; i++)
        for (uint8_t j=0; j<memory->_activeWH; j++)
        {
            if (memory->_activeTetramino[i][j] == TF_OBJECT)
                GPU_DrawBevelSquare(memory->_activeColor, START_X + BLOCK_SIZE*(i + memory->_activeX), START_Y + BLOCK_SIZE*(j + memory->_activeY), BLOCK_SIZE-1, BLOCK_SIZE-1);
        }
    /* line-clear flash across the field */
    if (memory->_clearFlash)
    {
        GPU_Color flash = GPU_ShadeColor(C_WHITE, (int16_t)(memory->_clearFlash*28));
        GPU_DrawEmptySquare(flash, (int16_t)(START_X-1), (int16_t)(START_Y-1), 10*BLOCK_SIZE+2, 20*BLOCK_SIZE+2);
        char clr[12];
        sprintf(clr, "CLEAR x%u", (unsigned)memory->_lastClear);
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, f, S1_CENTER_X, (int16_t)(START_Y+4), clr);
    }
    // Draw next
    GPU_DrawPanel(RGB(16,18,48), C_GRAY_DARK, (int16_t)(GPU_X/2+8), 8, (int16_t)(GPU_X/2-16), 70);
    const char * txtNextOne = "NEXT";
    GPU_DrawTextCentered(C_GRAY_LIGHT, f, (int16_t)(GPU_X*3/4), 14, txtNextOne);
    for (uint8_t i=0; i<memory->_nextWH; i++)
        for (uint8_t j=0; j<memory->_nextWH; j++)
        {
            if (memory->_nextTetramino[j][i] == TF_OBJECT)
                GPU_DrawBevelSquare(memory->_nextColor, (int16_t)(GPU_X/2+START_X+BLOCK_SIZE*j), (int16_t)(28+BLOCK_SIZE*i), BLOCK_SIZE-1, BLOCK_SIZE-1);
        }
    // Draw score
    GPU_DrawPanel(RGB(16,18,48), C_GRAY_DARK, (int16_t)(GPU_X/2+8), 86, (int16_t)(GPU_X/2-16), 92);
    const char * txtScore = "SCORE";
    GPU_DrawTextCentered(C_GRAY_LIGHT, f, (int16_t)(GPU_X*3/4), 92, txtScore);
    char txtScoreInt[24];
    sprintf(txtScoreInt, "%u", (unsigned)memory->_score);
    GPU_DrawTextCentered(C_C1, f, (int16_t)(GPU_X*3/4), 110, txtScoreInt);
    sprintf(txtScoreInt, "LEVEL %u", (unsigned)(memory->_lines/10 + 1));
    GPU_DrawTextCentered(C_GRAY_LIGHT, f, (int16_t)(GPU_X*3/4), 132, txtScoreInt);
    sprintf(txtScoreInt, "LINES %u", (unsigned)memory->_lines);
    GPU_DrawTextCentered(C_GRAY_MEDIUM, f, (int16_t)(GPU_X*3/4), 150, txtScoreInt);
    GPU_DrawTextCentered(C_GRAY_DARK, &fontDescription[0], (int16_t)(GPU_X*3/4), 168, "tap field = drop");
    if (mainMemory._gameState == GS_DONE)
    {
        const FontDescription * f_big = &fontDescription[3];
        GPU_DrawPanel(RGB(16,18,48), C_RED, 4, (int16_t)(GPU_Y/4), (int16_t)(GPU_X/2-8), 110);
        GPU_DrawTextCentered(C_RED, f_big, S1_CENTER_X, (int16_t)(GPU_Y/2-30), "GAME");
        GPU_DrawTextCentered(C_RED, f_big, S1_CENTER_X, (int16_t)(GPU_Y/2-6), "OVER");
        GPU_DrawButton(C_YELLOW, C_BLACK, f_big, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G5_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
