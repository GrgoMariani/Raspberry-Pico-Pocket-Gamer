// REVERSI - outflank the computer on an 8x8 board

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define RV_N 8
#define RV_CELL 26
#define RV_OX 6
#define RV_OY 22

#define RV_PLAYER 1     /* black, moves first */
#define RV_AI 2         /* white */

typedef struct {
    uint8_t board[RV_N][RV_N];   /* 0 empty, 1 black, 2 white */
    uint8_t current;             /* whose turn: RV_PLAYER or RV_AI */
    uint8_t cursor;              /* keyboard cursor, cell index */
    uint8_t state;               /* 0 player input, 1 AI thinking, 2 over */
    uint32_t aiAt;               /* when the AI may play */
    uint32_t passUntil;          /* "pass" message visibility */
    uint8_t passedPlayer;        /* who had to pass */
    uint8_t winner;              /* 0 draw, else the winner */
    uint8_t blackCount, whiteCount;
} Game35Memory;

static Game35Memory * const memory = (Game35Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t wins = 0, losses = 0, draws = 0;

static const int8_t _dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static const int8_t _dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

/* positional value of a square: corners are gold, the squares next to an
   empty corner are traps */
static const int8_t _weight[RV_N][RV_N] = {
    { 100, -30, 10,  5,  5, 10, -30, 100 },
    { -30, -40, -2, -2, -2, -2, -40, -30 },
    {  10,  -2,  8,  1,  1,  8,  -2,  10 },
    {   5,  -2,  1,  0,  0,  1,  -2,   5 },
    {   5,  -2,  1,  0,  0,  1,  -2,   5 },
    {  10,  -2,  8,  1,  1,  8,  -2,  10 },
    { -30, -40, -2, -2, -2, -2, -40, -30 },
    { 100, -30, 10,  5,  5, 10, -30, 100 },
};

static uint8_t InBounds(int x, int y)
{
    return (uint8_t)(x >= 0 && x < RV_N && y >= 0 && y < RV_N);
}

/* how many discs would flip if `player` played at (x,y); 0 = illegal move */
static uint8_t CountFlips(uint8_t x, uint8_t y, uint8_t player)
{
    if (memory->board[y][x])
        return 0;
    uint8_t opp = (uint8_t)(3 - player);
    uint8_t total = 0;
    for (uint8_t d = 0; d < 8; d++)
    {
        int nx = x + _dx[d], ny = y + _dy[d];
        uint8_t line = 0;
        while (InBounds(nx, ny) && memory->board[ny][nx] == opp)
        {
            nx += _dx[d]; ny += _dy[d]; line++;
        }
        if (line && InBounds(nx, ny) && memory->board[ny][nx] == player)
            total = (uint8_t)(total + line);
    }
    return total;
}

static void ApplyMove(uint8_t x, uint8_t y, uint8_t player)
{
    uint8_t opp = (uint8_t)(3 - player);
    for (uint8_t d = 0; d < 8; d++)
    {
        int nx = x + _dx[d], ny = y + _dy[d];
        uint8_t line = 0;
        int tx = nx, ty = ny;
        while (InBounds(tx, ty) && memory->board[ty][tx] == opp)
        {
            tx += _dx[d]; ty += _dy[d]; line++;
        }
        if (line && InBounds(tx, ty) && memory->board[ty][tx] == player)
        {
            int fx = nx, fy = ny;
            for (uint8_t i = 0; i < line; i++)
            {
                memory->board[fy][fx] = player;
                fx += _dx[d]; fy += _dy[d];
            }
        }
    }
    memory->board[y][x] = player;
}

static uint8_t HasMoves(uint8_t player)
{
    for (uint8_t y = 0; y < RV_N; y++)
        for (uint8_t x = 0; x < RV_N; x++)
            if (CountFlips(x, y, player))
                return 1;
    return 0;
}

static void CountDiscs(void)
{
    uint8_t b = 0, w = 0;
    for (uint8_t y = 0; y < RV_N; y++)
        for (uint8_t x = 0; x < RV_N; x++)
        {
            if (memory->board[y][x] == RV_PLAYER) b++;
            else if (memory->board[y][x] == RV_AI) w++;
        }
    memory->blackCount = b;
    memory->whiteCount = w;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game35Memory));
    memory->board[3][3] = RV_PLAYER;
    memory->board[4][4] = RV_PLAYER;
    memory->board[3][4] = RV_AI;
    memory->board[4][3] = RV_AI;
    memory->current = RV_PLAYER;
    memory->cursor = 0;
    memory->state = 0;
    memory->passUntil = 0;
    CountDiscs();
    mainMemory._gameState = GS_RUNNING;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void FinishGame(void)
{
    CountDiscs();
    memory->state = 2;
    if (memory->blackCount > memory->whiteCount)      { memory->winner = RV_PLAYER; wins++; }
    else if (memory->whiteCount > memory->blackCount) { memory->winner = RV_AI; losses++; }
    else                                              { memory->winner = 0; draws++; }
    mainMemory._gameState = GS_DONE;
}

/* hand the turn over, handling passes and the end of the game */
static void EndTurn(void)
{
    uint8_t next = (uint8_t)(3 - memory->current);
    if (HasMoves(next))
    {
        memory->current = next;
    }
    else if (HasMoves(memory->current))
    {
        memory->passedPlayer = next;
        memory->passUntil = IF_GetCurrentTime() + 1200000;
        /* the same player goes again */
    }
    else
    {
        FinishGame();
        return;
    }
    if (memory->current == RV_AI)
    {
        memory->state = 1;
        memory->aiAt = IF_GetCurrentTime() + 700000;
    }
    else
    {
        memory->state = 0;
    }
}

static void AIMove(void)
{
    int bestScore = -10000;
    uint8_t bestX[RV_N*RV_N], bestY[RV_N*RV_N];
    uint8_t nBest = 0;
    for (uint8_t y = 0; y < RV_N; y++)
        for (uint8_t x = 0; x < RV_N; x++)
        {
            uint8_t f = CountFlips(x, y, RV_AI);
            if (!f)
                continue;
            int score = f*2 + _weight[y][x];
            if (score > bestScore)
            {
                bestScore = score;
                nBest = 0;
                bestX[nBest] = x; bestY[nBest] = y; nBest++;
            }
            else if (score == bestScore && nBest < RV_N*RV_N)
            {
                bestX[nBest] = x; bestY[nBest] = y; nBest++;
            }
        }
    if (nBest)
    {
        uint8_t pick = (uint8_t)Random_Range(nBest);
        ApplyMove(bestX[pick], bestY[pick], RV_AI);
    }
}

static void TryPlace(uint8_t x, uint8_t y)
{
    if (memory->state != 0 || memory->current != RV_PLAYER)
        return;
    if (!CountFlips(x, y, RV_PLAYER))
        return;
    ApplyMove(x, y, RV_PLAYER);
    EndTurn();
}

static void G_Update(void)
{
    if (memory->state == 2)
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    if (memory->state == 1)
    {
        /* let the player see the board a moment before the computer answers */
        Keyboard_GetPressedKeys();
        Touch_ClearTap();
        if ((int32_t)(IF_GetCurrentTime() - memory->aiAt) >= 0)
        {
            AIMove();
            EndTurn();
        }
        return;
    }
    /* player's turn: arrows move a cursor, UP+DOWN together places a disc */
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    uint8_t cx = (uint8_t)(memory->cursor % RV_N), cy = (uint8_t)(memory->cursor / RV_N);
    if ((keys & KEY_UP) && (keys & KEY_DOWN))
    {
        TryPlace(cx, cy);
    }
    else
    {
        if (keys & KEY_LEFT)  cx = (uint8_t)((cx + RV_N - 1) % RV_N);
        if (keys & KEY_RIGHT) cx = (uint8_t)((cx + 1) % RV_N);
        if (keys & KEY_UP)    cy = (uint8_t)((cy + RV_N - 1) % RV_N);
        if (keys & KEY_DOWN)  cy = (uint8_t)((cy + 1) % RV_N);
        memory->cursor = (uint8_t)(cy*RV_N + cx);
    }
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
    {
        int x = ((int)tx - RV_OX)/RV_CELL;
        int y = ((int)ty - RV_OY)/RV_CELL;
        if (InBounds(x, y))
        {
            memory->cursor = (uint8_t)(y*RV_N + x);
            TryPlace((uint8_t)x, (uint8_t)y);
        }
    }
}

static void DrawDisc(int16_t cx, int16_t cy, uint8_t player)
{
    if (player == RV_PLAYER)
    {
        GPU_DrawFilledCircle(C_BLACK, cx, cy, 10);
        GPU_DrawEmptyCircle(C_GRAY_MEDIUM, cx, cy, 10);
        GPU_DrawPixel(C_GRAY_LIGHT, (uint16_t)(cx-3), (uint16_t)(cy-3));
    }
    else
    {
        GPU_DrawBall(C_WHITE, cx, cy, 10);
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(8,20,12), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* board */
    GPU_DrawFilledSquare(RGB(20,90,40), RV_OX, RV_OY, RV_N*RV_CELL, RV_N*RV_CELL);
    for (uint8_t i = 0; i <= RV_N; i++)
    {
        GPU_DrawLine(RGB(10,50,24), (int16_t)(RV_OX + i*RV_CELL), RV_OY,
                     (int16_t)(RV_OX + i*RV_CELL), (int16_t)(RV_OY + RV_N*RV_CELL));
        GPU_DrawLine(RGB(10,50,24), RV_OX, (int16_t)(RV_OY + i*RV_CELL),
                     (int16_t)(RV_OX + RV_N*RV_CELL), (int16_t)(RV_OY + i*RV_CELL));
    }
    /* legal moves for the player */
    if (memory->state == 0 && memory->current == RV_PLAYER)
    {
        for (uint8_t y = 0; y < RV_N; y++)
            for (uint8_t x = 0; x < RV_N; x++)
                if (CountFlips(x, y, RV_PLAYER))
                    GPU_DrawFilledCircle(RGB(120,220,120),
                                         (int16_t)(RV_OX + x*RV_CELL + RV_CELL/2),
                                         (int16_t)(RV_OY + y*RV_CELL + RV_CELL/2), 3);
    }
    /* discs */
    for (uint8_t y = 0; y < RV_N; y++)
        for (uint8_t x = 0; x < RV_N; x++)
            if (memory->board[y][x])
                DrawDisc((int16_t)(RV_OX + x*RV_CELL + RV_CELL/2),
                         (int16_t)(RV_OY + y*RV_CELL + RV_CELL/2), memory->board[y][x]);
    /* cursor */
    if (memory->state == 0)
    {
        uint8_t cx = (uint8_t)(memory->cursor % RV_N), cy = (uint8_t)(memory->cursor / RV_N);
        GPU_DrawEmptySquare(C_YELLOW, (int16_t)(RV_OX + cx*RV_CELL + 1), (int16_t)(RV_OY + cy*RV_CELL + 1),
                            RV_CELL-2, RV_CELL-2);
    }

    /* side panel */
    GPU_DrawPanel(RGB(12,24,16), C_GRAY_DARK, 220, 22, 96, 120);
    GPU_DrawTextCentered(C_GRAY_LIGHT, fm, 268, 28, "REVERSI");
    DrawDisc(238, 56, RV_PLAYER);
    sprintf(text, "YOU %u", (unsigned)memory->blackCount);
    GPU_DrawText(C_WHITE, fm, 252, 51, text, (uint16_t)strlen(text));
    DrawDisc(238, 82, RV_AI);
    sprintf(text, "CPU %u", (unsigned)memory->whiteCount);
    GPU_DrawText(C_WHITE, fm, 252, 77, text, (uint16_t)strlen(text));
    const char * turn = (memory->state == 1) ? "CPU THINKS" :
                        (memory->state == 2) ? "GAME OVER" : "YOUR TURN";
    GPU_DrawTextCentered(memory->state == 1 ? C_RED : C_C3, fs, 268, 104, turn);
    sprintf(text, "W%u L%u D%u", (unsigned)wins, (unsigned)losses, (unsigned)draws);
    GPU_DrawTextCentered(C_GRAY_MEDIUM, fs, 268, 122, text);

    GPU_DrawText(C_GRAY_DARK, fs, 220, 150, "arrows move", 11);
    GPU_DrawText(C_GRAY_DARK, fs, 220, 160, "UP+DOWN place", 13);
    GPU_DrawText(C_GRAY_DARK, fs, 220, 170, "or tap a cell", 13);

    /* pass notice */
    if (memory->passUntil && (int32_t)(IF_GetCurrentTime() - memory->passUntil) < 0 && memory->state != 2)
    {
        sprintf(text, "%s PASS", memory->passedPlayer == RV_PLAYER ? "YOU" : "CPU");
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, (int16_t)(RV_OX + RV_N*RV_CELL/2), 40, text);
    }

    if (memory->state == 2)
    {
        const char * msg = memory->winner == RV_PLAYER ? "YOU WIN!" :
                           memory->winner == RV_AI ? "CPU WINS" : "DRAW";
        GPU_Color col = memory->winner == RV_PLAYER ? C_C3 :
                        memory->winner == RV_AI ? C_RED : C_YELLOW;
        GPU_DrawPanel(RGB(12,24,16), col, (int16_t)(RV_OX + RV_N*RV_CELL/2 - 90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(col, C_BLACK, fb, (int16_t)(RV_OX + RV_N*RV_CELL/2), (int16_t)(GPU_Y/2-28), msg);
        sprintf(text, "%u : %u", (unsigned)memory->blackCount, (unsigned)memory->whiteCount);
        GPU_DrawTextCentered(C_WHITE, fm, (int16_t)(RV_OX + RV_N*RV_CELL/2), (int16_t)(GPU_Y/2-2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, (int16_t)(RV_OX + RV_N*RV_CELL/2), (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G35_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };