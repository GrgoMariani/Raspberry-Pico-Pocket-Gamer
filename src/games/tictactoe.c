// Shared tic tac toe implementation (1 player vs AI and 2 players)

#include "tictactoe.h"

#include "../gpu.h"
#include "../interface.h"

#include <string.h>

typedef enum {
    TTT_EMPTY = 0,
    TTT_X = 1,
    TTT_O = 2
} Cell;

typedef enum {
    TTT_PLAYING = 0,
    TTT_X_WINS = 1,
    TTT_O_WINS = 2,
    TTT_DRAW = 3
} TTTResult;

typedef struct {
    Cell _board[3][3];
    uint8_t _moves;
    uint8_t _nextIsO;          /* 0 = X is to move, 1 = O is to move */
    uint8_t _aiIsO;            /* the side the computer plays */
    uint8_t _victoryMask;      /* one bit per winning line */
    TTTResult _final;
    uint32_t _aiTick;
} TTTMemory;

static TTTMemory * const memory = (TTTMemory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 4

#define BOARD_X GPU_BORDER_X
#define BOARD_Y GPU_BORDER_Y
#define BOARD_SIZE GPU_REST
#define CELL (BOARD_SIZE/3)

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 30
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 40)

static const uint8_t _lines[8][3] = {
    { 0,1,2 }, { 3,4,5 }, { 6,7,8 },      /* rows    */
    { 0,3,6 }, { 1,4,7 }, { 2,5,8 },      /* columns */
    { 0,4,8 }, { 2,4,6 }                  /* diagonals */
};

static Cell CellAt(uint8_t x, uint8_t y)
{
    return memory->_board[y][x];
}

static void SetCell(uint8_t x, uint8_t y, Cell c)
{
    memory->_board[y][x] = c;
}

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* returns the result of the current position and fills the victory mask */
static TTTResult Evaluate(uint8_t * victoryMask)
{
    uint8_t mask = 0;
    TTTResult win = TTT_PLAYING;
    for (uint8_t l = 0; l < 8; l++)
    {
        Cell a = memory->_board[_lines[l][0]/3][_lines[l][0]%3];
        Cell b = memory->_board[_lines[l][1]/3][_lines[l][1]%3];
        Cell c = memory->_board[_lines[l][2]/3][_lines[l][2]%3];
        if (a != TTT_EMPTY && a == b && a == c)
        {
            mask |= (uint8_t)(1 << l);
            win = (a == TTT_X) ? TTT_X_WINS : TTT_O_WINS;
        }
    }
    if (victoryMask)
        *victoryMask = mask;
    if (win != TTT_PLAYING)
        return win;
    if (memory->_moves >= 9)
        return TTT_DRAW;
    return TTT_PLAYING;
}

/* would playing (x,y) as `side` win immediately? */
static uint8_t WinsImmediately(uint8_t x, uint8_t y, Cell side)
{
    if (CellAt(x, y) != TTT_EMPTY)
        return 0;
    SetCell(x, y, side);
    memory->_moves++;
    uint8_t dummy;
    TTTResult r = Evaluate(&dummy);
    memory->_moves--;
    SetCell(x, y, TTT_EMPTY);
    return (uint8_t)(r == ((side == TTT_X) ? TTT_X_WINS : TTT_O_WINS));
}

static void Play(uint8_t x, uint8_t y, Cell side)
{
    if (CellAt(x, y) != TTT_EMPTY)
        return;
    SetCell(x, y, side);
    memory->_moves++;
    memory->_nextIsO = (side == TTT_X) ? 1 : 0;
    uint8_t mask = 0;
    TTTResult r = Evaluate(&mask);
    if (r != TTT_PLAYING)
    {
        memory->_final = r;
        memory->_victoryMask = mask;
        mainMemory._gameState = GS_DONE;
    }
}

/* the computer: win if it can, block if it must, otherwise prefer the centre,
   then a corner, then anything */
static void AIMove(void)
{
    Cell ai = memory->_aiIsO ? TTT_O : TTT_X;
    Cell human = memory->_aiIsO ? TTT_X : TTT_O;
    for (uint8_t i = 0; i < 9; i++)
        if (WinsImmediately(i%3, i/3, ai))
        {
            Play(i%3, i/3, ai);
            return;
        }
    for (uint8_t i = 0; i < 9; i++)
        if (WinsImmediately(i%3, i/3, human))
        {
            Play(i%3, i/3, ai);
            return;
        }
    static const uint8_t preference[9] = { 4, 0, 2, 6, 8, 1, 3, 5, 7 };
    uint8_t choices[9];
    uint8_t n = 0;
    for (uint8_t p = 0; p < 9; p++)
        if (CellAt(preference[p]%3, preference[p]/3) == TTT_EMPTY)
            choices[n++] = preference[p];
    if (n == 0)
        return;
    /* among the best free cells pick one at random so that the computer does
       not play the very same game every time */
    uint8_t pick = choices[Random_Range(n < 3 ? n : 3)];
    Play(pick%3, pick/3, ai);
}

void TTT_Init(uint8_t withAI)
{
    memset(memory, 0, sizeof(TTTMemory));
    mainMemory._gameState = GS_RUNNING;
    memory->_nextIsO = 0;
    memory->_aiIsO = (withAI && (Random_Next() & 1)) ? 1 : 0;
    memory->_aiTick = CalculateTick() + 2;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void TouchMove(uint8_t withAI)
{
    if (!mainMemory.touchPressedNew)
        return;
    if (mainMemory._gameState != GS_RUNNING)
    {
        if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W &&
            mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
            TTT_Init(withAI);
        return;
    }
    if (withAI && memory->_nextIsO == memory->_aiIsO)
        return;                       /* the computer is thinking */
    int32_t x = ((int32_t)mainMemory.touch_X - BOARD_X)/CELL;
    int32_t y = ((int32_t)mainMemory.touch_Y - BOARD_Y)/CELL;
    if (x < 0 || x > 2 || y < 0 || y > 2)
        return;
    Play((uint8_t)x, (uint8_t)y, memory->_nextIsO ? TTT_O : TTT_X);
    memory->_aiTick = CalculateTick() + 2;
}

void TTT_Update(uint8_t withAI)
{
    TouchMove(withAI);
    if (!withAI || mainMemory._gameState != GS_RUNNING)
        return;
    if (memory->_nextIsO != memory->_aiIsO)
        return;
    /* give the player a moment to see the board before the computer answers */
    uint32_t tick = CalculateTick();
    if ((int32_t)(tick - memory->_aiTick) < 0)
        return;
    memory->_aiTick = tick + 2;
    AIMove();
}

/* ------------------------------------------------------------------ drawing */

static void DrawMark(Cell c, int16_t cx, int16_t cy, uint8_t dim)
{
    const int16_t r = (int16_t)(CELL/2 - 10);
    GPU_Color color = (c == TTT_X) ? C_GREEN : C_RED;
    if (dim)
        color = GPU_ShadeColor(color, 140);
    if (c == TTT_X)
    {
        GPU_DrawLine(color, cx-r, cy-r, cx+r, cy+r);
        GPU_DrawLine(color, cx-r+1, cy-r, cx+r+1, cy+r);
        GPU_DrawLine(color, cx+r, cy-r, cx-r, cy+r);
        GPU_DrawLine(color, cx+r-1, cy-r, cx-r-1, cy+r);
    }
    else
    {
        GPU_DrawFilledCircle(GPU_ShadeColor(color, 160), cx, cy, r);
        GPU_DrawFilledCircle(C_BLACK, cx, cy, (int16_t)(r-5));
        GPU_DrawEmptyCircle(color, cx, cy, r);
        GPU_DrawPixel(RGB(255,255,255), (uint16_t)(cx-r/3), (uint16_t)(cy-r/3));
    }
}

void TTT_Draw(uint8_t withAI)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,12,40), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* the board */
    GPU_DrawPanel(RGB(20,24,60), C_C1, (int16_t)(BOARD_X-2), (int16_t)(BOARD_Y-2),
                  (int16_t)(BOARD_SIZE+4), (int16_t)(BOARD_SIZE+4));
    for (uint8_t y = 0; y < 3; y++)
        for (uint8_t x = 0; x < 3; x++)
        {
            int16_t cx = (int16_t)(BOARD_X + x*CELL + CELL/2);
            int16_t cy = (int16_t)(BOARD_Y + y*CELL + CELL/2);
            GPU_DrawBevelSquare(RGB(28,34,80), (int16_t)(BOARD_X + x*CELL + 2),
                                (int16_t)(BOARD_Y + y*CELL + 2), (int16_t)(CELL-4), (int16_t)(CELL-4));
            Cell c = CellAt(x, y);
            if (c != TTT_EMPTY)
                DrawMark(c, cx, cy, (uint8_t)(memory->_final != TTT_PLAYING));
        }
    /* the winning line */
    if (memory->_victoryMask)
    {
        for (uint8_t l = 0; l < 8; l++)
        {
            if (!(memory->_victoryMask & (1 << l)))
                continue;
            uint8_t a = _lines[l][0], b = _lines[l][2];
            int16_t x0 = (int16_t)(BOARD_X + (a%3)*CELL + CELL/2);
            int16_t y0 = (int16_t)(BOARD_Y + (a/3)*CELL + CELL/2);
            int16_t x1 = (int16_t)(BOARD_X + (b%3)*CELL + CELL/2);
            int16_t y1 = (int16_t)(BOARD_Y + (b/3)*CELL + CELL/2);
            for (int8_t d = -2; d <= 2; d++)
                GPU_DrawLine(C_YELLOW, (int16_t)(x0+d), y0, (int16_t)(x1+d), y1);
        }
    }

    /* the right hand panel */
    GPU_DrawPanel(RGB(16,18,48), C_GRAY_DARK, (int16_t)(GPU_X/2+8), 8, (int16_t)(GPU_X/2-16), 60);
    char text[24];
    if (mainMemory._gameState == GS_RUNNING)
    {
        if (withAI && memory->_nextIsO == memory->_aiIsO)
        {
            GPU_DrawTextCentered(C_RED, fb, S2_CENTER_X, 16, "COMPUTER");
            GPU_DrawTextCentered(C_RED, fb, S2_CENTER_X, 40, "THINKS");
        }
        else
        {
            const char * who = withAI ? "YOUR MOVE" : (memory->_nextIsO ? "O TO MOVE" : "X TO MOVE");
            GPU_DrawTextCentered(C_WHITE, fb, S2_CENTER_X, 16, who);
            if (!withAI)
                DrawMark(memory->_nextIsO ? TTT_O : TTT_X, S2_CENTER_X, 48, 0);
            else
                GPU_DrawTextCentered(C_GRAY_LIGHT, fm, S2_CENTER_X, 44, "tap a field");
        }
    }
    else
    {
        const char * msg;
        GPU_Color color;
        if (memory->_final == TTT_DRAW)
        {
            msg = "DRAW";
            color = C_YELLOW;
        }
        else if (!withAI)
        {
            msg = (memory->_final == TTT_X_WINS) ? "X WINS" : "O WINS";
            color = (memory->_final == TTT_X_WINS) ? C_GREEN : C_RED;
        }
        else if ((memory->_final == TTT_O_WINS) == (memory->_aiIsO == 1))
        {
            msg = "CPU WINS";
            color = C_RED;
        }
        else
        {
            msg = "YOU WIN";
            color = C_GREEN;
        }
        GPU_DrawTextCentered(color, fb, S2_CENTER_X, 26, msg);
    }

    /* who plays what */
    {
        int16_t y = 88;
        DrawMark(TTT_X, (int16_t)(GPU_X/2+26), (int16_t)(y+5), 0);
        const char * labelX = withAI ? (memory->_aiIsO ? "you" : "computer") : "player 1";
        GPU_DrawText(C_GRAY_LIGHT, fm, (int16_t)(GPU_X/2+44), y, labelX, (uint16_t)strlen(labelX));
        y += 24;
        DrawMark(TTT_O, (int16_t)(GPU_X/2+26), (int16_t)(y+5), 0);
        const char * labelO = withAI ? (memory->_aiIsO ? "computer" : "you") : "player 2";
        GPU_DrawText(C_GRAY_LIGHT, fm, (int16_t)(GPU_X/2+44), y, labelO, (uint16_t)strlen(labelO));
    }

    if (mainMemory._gameState != GS_RUNNING)
    {
        sprintf(text, "moves: %u", (unsigned)memory->_moves);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, S2_CENTER_X, (int16_t)(RESET_BUTTON_Y - 20), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

void TTT_Deinit(void)
{
}
