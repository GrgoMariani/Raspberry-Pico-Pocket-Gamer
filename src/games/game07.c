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
    uint8_t _lastCol;       /* most recently placed stone, for the drop animation */
    uint8_t _lastRow;
    uint32_t _dropStart;    /* when the last stone was dropped (animation) */
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
    memory->_dropStart = IF_GetCurrentTime() - ONE_SECOND;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

/* returns the row the stone landed in, or -1 when the column is full.
   The return type has to be signed: with uint8_t the "-1" becomes 255 and
   "Drop(...) != -1" is true for a full column, which made the AI write
   outside of the field. */
static int16_t Drop(C4Enum player, uint8_t row)
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
    memory->_lastCol = row;
    memory->_lastRow = max;
    memory->_dropStart = IF_GetCurrentTime();
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


/* the four-in-a-row lines as cell indices (col*6+row) */
static const uint8_t _c4lines[69][4] = {
    /* horizontal */
    {0,6,12,18},{1,7,13,19},{2,8,14,20},{3,9,15,21},{4,10,16,22},{5,11,17,23},
    {6,12,18,24},{7,13,19,25},{8,14,20,26},{9,15,21,27},{10,16,22,28},{11,17,23,29},
    {12,18,24,30},{13,19,25,31},{14,20,26,32},{15,21,27,33},{16,22,28,34},{17,23,29,35},
    {18,24,30,36},{19,25,31,37},{20,26,32,38},{21,27,33,39},{22,28,34,40},{23,29,35,41},
    /* vertical */
    {0,1,2,3},{1,2,3,4},{2,3,4,5},{6,7,8,9},{7,8,9,10},{8,9,10,11},
    {12,13,14,15},{13,14,15,16},{14,15,16,17},{18,19,20,21},{19,20,21,22},{20,21,22,23},
    {24,25,26,27},{25,26,27,28},{26,27,28,29},{30,31,32,33},{31,32,33,34},{32,33,34,35},
    {36,37,38,39},{37,38,39,40},{38,39,40,41},
    /* diagonal down-right */
    {0,7,14,21},{1,8,15,22},{2,9,16,23},{6,13,20,27},{7,14,21,28},{8,15,22,29},
    {12,19,26,33},{13,20,27,34},{14,21,28,35},{18,25,32,39},{19,26,33,40},{20,27,34,41},
    /* diagonal up-right */
    {3,8,13,18},{4,9,14,19},{5,10,15,20},{9,14,19,24},{10,15,20,25},{11,16,21,26},
    {15,20,25,30},{16,21,26,31},{17,22,27,32},{21,26,31,36},{22,27,32,37},{23,28,33,38},
};
#define C4_LINE_COUNT 69

/* returns 1 and fills cells with the four winning positions */
static uint8_t FindWinLine(uint8_t * cells)
{
    for (uint8_t l = 0; l < C4_LINE_COUNT; l++)
    {
        C4Enum a = memory->_field[_c4lines[l][0]/6][_c4lines[l][0]%6];
        if (a == C4_EMPTY)
            continue;
        if (a == memory->_field[_c4lines[l][1]/6][_c4lines[l][1]%6] &&
            a == memory->_field[_c4lines[l][2]/6][_c4lines[l][2]%6] &&
            a == memory->_field[_c4lines[l][3]/6][_c4lines[l][3]%6])
        {
            for (uint8_t k = 0; k < 4; k++)
                cells[k] = _c4lines[l][k];
            return 1;
        }
    }
    return 0;
}

static void CalculateAIMove()
{
    C4Enum p1 = TogglePlayer(memory->_playerAI);
    // check if win -> play
    for (uint8_t col=0; col<7; col++)
    {
        int16_t row = Drop(memory->_playerAI, col);
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
        int16_t row = Drop(p1, col);
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
    /* otherwise play towards the middle, but never into a column where the
       stone would hand the opponent an immediate win right on top of it */
    static const uint8_t order[7] = { 3, 2, 4, 1, 5, 0, 6 };
    uint8_t fallback = 7;
    for (uint8_t k = 0; k < 7; k++)
    {
        uint8_t col = order[k];
        int16_t row = Drop(memory->_playerAI, col);
        if (row == -1)
            continue;
        if (fallback == 7)
            fallback = col;
        uint8_t givesWin = 0;
        int16_t row2 = Drop(p1, col);
        if (row2 != -1)
        {
            givesWin = (uint8_t)(CheckWinner() == p1);
            memory->_field[col][row2] = C4_EMPTY;
        }
        memory->_field[col][row] = C4_EMPTY;
        if (!givesWin)
        {
            Drop(memory->_playerAI, col);
            return;
        }
    }
    if (fallback != 7)
        Drop(memory->_playerAI, fallback);
}


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)

#define BLOCK_SIZE ( (GPU_X/2 < GPU_Y) ? GPU_X/14 : GPU_Y/7 )
#define START_X (GPU_X/2 - 7*BLOCK_SIZE)/2
#define START_Y (GPU_Y - 7*BLOCK_SIZE)/2

/* keys and touch are polled every main loop iteration, not only on the slow
   AI tick, so that moving the selector feels instant */
static void HandleHumanInput(void)
{
    KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
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
    uint8_t drop = (keyboard & KEY_DOWN) ? 1 : 0;
    /* tapping a column drops the stone there */
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
    {
        if (tx < GPU_X/2)
        {
            int32_t col = ((int32_t)tx - START_X)/BLOCK_SIZE;
            if (col < 0) col = 0;
            if (col > 6) col = 6;
            memory->_choicePosition = (uint8_t)col;
            drop = 1;
        }
    }
    if (drop)
    {
        if (Drop(memory->_playerNext, memory->_choicePosition) != -1)
        {
            memory->_playerNext = TogglePlayer(memory->_playerNext);
            if (CheckWinner() != C4_EMPTY)
                mainMemory._gameState = GS_DONE;
        }
    }
}

static void G_Update(void)
{
    if (mainMemory._gameState == GS_RUNNING && memory->_playerNext != memory->_playerAI)
        HandleHumanInput();
    else if (mainMemory._gameState == GS_RUNNING)
    {
        /* while the computer thinks, drop any input so it cannot leak into
           the player's next turn as a surprise move */
        Keyboard_GetPressedKeys();
        Touch_ClearTap();
    }
    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        if (mainMemory._gameState == GS_DONE)
            break;
        if (memory->_playerNext == memory->_playerAI)
        {
            CalculateAIMove();
            memory->_playerNext = TogglePlayer(memory->_playerNext);
            if (CheckWinner() != C4_EMPTY)
                mainMemory._gameState = GS_DONE;
        }
    }
    if (mainMemory._gameState != GS_RUNNING)
	{
		if (mainMemory.touchPressedNew)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
	}
}

static void G_Draw(void)
{
    const FontDescription * font_small = &fontDescription[0];
    const FontDescription * font_big = &fontDescription[2];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(8,10,32), C_BLACK, 0, 0, GPU_X, GPU_Y);
    // Draw Board: a blue frame with a hole per cell
    GPU_DrawPanel(RGB(20,40,140), C_C1, (int16_t)(START_X-4), (int16_t)(START_Y+BLOCK_SIZE-5),
                  (int16_t)(7*BLOCK_SIZE+8), (int16_t)(6*BLOCK_SIZE+8));
    uint32_t dropEl = IF_GetCurrentTime() - memory->_dropStart;
    for (uint8_t i=0; i<7; i++)
        for (uint8_t j=0; j<6; j++)
        {
            int16_t cx = (int16_t)(START_X + i*BLOCK_SIZE + BLOCK_SIZE/2);
            int16_t cy = (int16_t)(START_Y + j*BLOCK_SIZE + BLOCK_SIZE/2 + BLOCK_SIZE);
            GPU_DrawFilledCircle(RGB(6,8,24), cx, cy, (int16_t)(BLOCK_SIZE/2-2));
            C4Enum cell = memory->_field[i][j];
            if (cell == C4_EMPTY)
                continue;
            /* the freshly dropped stone falls into place (time based, so the
               animation lasts ~300ms no matter how fast the main loop spins) */
            uint8_t anim = (uint8_t)(dropEl < 300000 && i == memory->_lastCol && j == memory->_lastRow &&
                                     mainMemory._gameState == GS_RUNNING);
            if (anim)
            {
                int16_t top = (int16_t)(START_Y - BLOCK_SIZE);
                int32_t p = (int32_t)(dropEl * 256 / 300000);
                int32_t eased = p*p/256;
                cy = (int16_t)(top + (cy - top)*eased/256);
            }
            GPU_Color pc = (cell == C4_PLAYER_1) ? C_YELLOW : C_RED;
            if (!anim && i == memory->_lastCol && j == memory->_lastRow)
                GPU_DrawGlow(pc, cx, cy, (int16_t)(BLOCK_SIZE/2));
            GPU_DrawBall(pc, cx, cy, (int16_t)(BLOCK_SIZE/2-3));
        }
    // Draw Choice position
    if (mainMemory._gameState == GS_RUNNING)
    {
        GPU_Color turn = (memory->_playerNext == C4_PLAYER_1) ? C_YELLOW : C_RED;
        int16_t ax = (int16_t)(START_X + memory->_choicePosition*BLOCK_SIZE + BLOCK_SIZE/2);
        GPU_DrawBall(turn, ax, (int16_t)(START_Y + BLOCK_SIZE/2), (int16_t)(BLOCK_SIZE/2-3));
        GPU_DrawFilledTriangle(turn, (int16_t)(ax-5), (int16_t)(START_Y+BLOCK_SIZE-4),
                               (int16_t)(ax+5), (int16_t)(START_Y+BLOCK_SIZE-4),
                               ax, (int16_t)(START_Y+BLOCK_SIZE+2));
    }
    // Draw on second screen
    GPU_DrawPanel(RGB(16,18,48), C_GRAY_DARK, (int16_t)(GPU_X/2+8), 8, (int16_t)(GPU_X/2-16), 52);
    {
        int16_t cx = (int16_t)(GPU_X/2+24);
        GPU_DrawBall(memory->_playerAI == C4_PLAYER_1 ? C_YELLOW : C_RED, cx, 20, 7);
        GPU_DrawText(C_GRAY_LIGHT, font_small, (int16_t)(cx+14), 16, "AI", 2);
        GPU_DrawBall(memory->_playerAI == C4_PLAYER_1 ? C_RED : C_YELLOW, cx, 42, 7);
        GPU_DrawText(C_GRAY_LIGHT, font_small, (int16_t)(cx+14), 38, "YOU", 3);
    }
    if (mainMemory._gameState != GS_DONE)
    {
        if (memory->_playerAI == memory->_playerNext)
            GPU_DrawTextCentered(C_RED, font_big, S2_CENTER_X, S2_CENTER_Y, "CALCULATING");
        else
        {
            GPU_DrawTextCentered(C_GRAY_MEDIUM, font_big, S2_CENTER_X, S2_CENTER_Y, "YOUR TURN");
            GPU_DrawTextCentered(C_GRAY_DARK, font_small, S2_CENTER_X, (int16_t)(S2_CENTER_Y+22), "LEFT/RIGHT + DOWN or tap");
        }
    }
    else
    {
        uint8_t winCells[4];
        uint8_t hasWin = FindWinLine(winCells);
        const char * msg;
        GPU_Color msgColor;
        if (!hasWin)
        {
            msg = "DRAW";
            msgColor = C_YELLOW;
        }
        else
        {
            C4Enum winner = memory->_field[winCells[0]/6][winCells[0]%6];
            /* highlight the four in a row */
            for (uint8_t k = 0; k < 4; k++)
            {
                int16_t cx = (int16_t)(START_X + (winCells[k]/6)*BLOCK_SIZE + BLOCK_SIZE/2);
                int16_t cy = (int16_t)(START_Y + (winCells[k]%6)*BLOCK_SIZE + BLOCK_SIZE/2 + BLOCK_SIZE);
                GPU_DrawEmptyCircle(C_WHITE, cx, cy, (int16_t)(BLOCK_SIZE/2-2));
            }
            if (winner == memory->_playerAI)
            {
                msg = "AI WINS";
                msgColor = C_RED;
            }
            else
            {
                msg = "YOU WIN";
                msgColor = C_C3;
            }
        }
        GPU_DrawTextCentered(msgColor, font_big, S2_CENTER_X, (int16_t)(S2_CENTER_Y-30), msg);
        GPU_DrawButton(C_YELLOW, C_BLACK, font_big, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}
static void G_Deinit(void)
{

}

const Callbacks G7_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
