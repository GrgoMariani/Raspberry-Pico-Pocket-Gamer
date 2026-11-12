// FLOOD-IT - flood the board with one colour from the top-left corner

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define FI_N 12
#define FI_COLORS 6
#define FI_CELL 16
#define FI_OX ((GPU_X - FI_N*FI_CELL)/2)
#define FI_OY 24

#define FI_PAL_W 44
#define FI_PAL_H 16
#define FI_PAL_Y 220
#define FI_PAL_OX ((GPU_X - FI_COLORS*FI_PAL_W)/2)

typedef struct {
    uint8_t board[FI_N][FI_N];   /* colour index 0..FI_COLORS-1 */
    uint8_t selColor;            /* palette selector (keyboard) */
    int16_t movesLeft;
    uint8_t level;
    uint8_t over;                /* 0 playing, 1 won, 2 lost */
    uint32_t score;
} Game34Memory;

static Game34Memory * const memory = (Game34Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static const GPU_Color _palColor[FI_COLORS] = {
    C_RED, C_GREEN, C_BLUE, C_YELLOW, C_MAGENTA, C_CYAN
};

static uint8_t MovesForLevel(uint8_t level)
{
    int16_t m = (int16_t)(23 - (level-1));
    if (m < 16) m = 16;
    return (uint8_t)m;
}

static void NewBoard(void)
{
    for (uint8_t y = 0; y < FI_N; y++)
        for (uint8_t x = 0; x < FI_N; x++)
            memory->board[y][x] = (uint8_t)Random_Range(FI_COLORS);
    memory->movesLeft = MovesForLevel(memory->level);
    memory->over = 0;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game34Memory));
    memory->level = 1;
    memory->selColor = 0;
    NewBoard();
    mainMemory._gameState = GS_RUNNING;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static uint8_t AllOneColor(void)
{
    uint8_t c = memory->board[0][0];
    for (uint8_t y = 0; y < FI_N; y++)
        for (uint8_t x = 0; x < FI_N; x++)
            if (memory->board[y][x] != c)
                return 0;
    return 1;
}

/* recolour the connected region of the top-left cell to `color`. Returns 0
   when the colour was already the current one (no move is spent then). */
static uint8_t Flood(uint8_t color)
{
    uint8_t old = memory->board[0][0];
    if (color == old)
        return 0;
    /* iterative flood fill; cells are recoloured when pushed so each cell is
       visited exactly once (no recursion, no duplicate stack entries) */
    uint8_t stackX[FI_N*FI_N];
    uint8_t stackY[FI_N*FI_N];
    int top = 0;
    memory->board[0][0] = color;
    stackX[top] = 0; stackY[top] = 0; top++;
    while (top)
    {
        top--;
        int x = stackX[top], y = stackY[top];
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };
        for (uint8_t d = 0; d < 4; d++)
        {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || nx >= FI_N || ny < 0 || ny >= FI_N)
                continue;
            if (memory->board[ny][nx] == old && top < FI_N*FI_N)
            {
                memory->board[ny][nx] = color;
                stackX[top] = (uint8_t)nx; stackY[top] = (uint8_t)ny; top++;
            }
        }
    }
    memory->movesLeft--;
    return 1;
}

static void AfterMove(void)
{
    if (AllOneColor())
    {
        memory->over = 1;
        memory->score += (uint32_t)(100 + memory->movesLeft*10 + memory->level*20);
        if (memory->score > bestScore)
            bestScore = memory->score;
    }
    else if (memory->movesLeft <= 0)
    {
        memory->over = 2;
        if (memory->score > bestScore)
            bestScore = memory->score;
        mainMemory._gameState = GS_DONE;
    }
}

static void ApplyColor(uint8_t color)
{
    if (memory->over)
        return;
    memory->selColor = color;
    if (Flood(color))
        AfterMove();
}

static void G_Update(void)
{
    if (memory->over == 1)
    {
        /* won: a tap or key starts the next, harder level */
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
        {
            if (memory->level < 99)
                memory->level++;
            NewBoard();
        }
        return;
    }
    if (memory->over == 2)
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    /* keyboard: LEFT/RIGHT pick a colour, UP/DOWN flood with it */
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    if (keys & KEY_LEFT)  memory->selColor = (uint8_t)((memory->selColor + FI_COLORS - 1) % FI_COLORS);
    if (keys & KEY_RIGHT) memory->selColor = (uint8_t)((memory->selColor + 1) % FI_COLORS);
    if (keys & (KEY_UP | KEY_DOWN))
        ApplyColor(memory->selColor);
    /* touch: tap a palette button to flood with that colour */
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty) && ty >= FI_PAL_Y && ty < FI_PAL_Y+FI_PAL_H)
    {
        int idx = ((int)tx - FI_PAL_OX)/FI_PAL_W;
        if (idx >= 0 && idx < FI_COLORS)
            ApplyColor((uint8_t)idx);
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,10,26), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* header */
    sprintf(text, "LEVEL %u", (unsigned)memory->level);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)strlen(text));
    sprintf(text, "MOVES %d", (int)memory->movesLeft);
    GPU_DrawTextCentered(memory->movesLeft <= 4 ? C_RED : C_YELLOW, fm, GPU_X/2, 4, text);
    sprintf(text, "SCORE %u", (unsigned)memory->score);
    GPU_DrawText(C_GRAY_LIGHT, fm, (int16_t)(GPU_X - 6 - strlen(text)*fm->width), 4, text, (uint16_t)strlen(text));

    /* board */
    for (uint8_t y = 0; y < FI_N; y++)
        for (uint8_t x = 0; x < FI_N; x++)
            GPU_DrawFilledSquare(_palColor[memory->board[y][x]],
                                 (int16_t)(FI_OX + x*FI_CELL), (int16_t)(FI_OY + y*FI_CELL),
                                 FI_CELL-1, FI_CELL-1);
    GPU_DrawEmptySquare(C_WHITE, (int16_t)(FI_OX-1), (int16_t)(FI_OY-1),
                        (int16_t)(FI_N*FI_CELL+1), (int16_t)(FI_N*FI_CELL+1));

    /* palette */
    for (uint8_t i = 0; i < FI_COLORS; i++)
    {
        int16_t px = (int16_t)(FI_PAL_OX + i*FI_PAL_W);
        GPU_DrawBevelSquare(_palColor[i], px+2, FI_PAL_Y, FI_PAL_W-4, FI_PAL_H);
        if (i == memory->selColor)
            GPU_DrawEmptySquare(C_WHITE, px+1, FI_PAL_Y-1, FI_PAL_W-2, FI_PAL_H+2);
    }

    if (memory->over == 1)
    {
        GPU_DrawPanel(RGB(16,24,16), C_C3, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28), "FLOODED!");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-2), "next level is harder");
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
    else if (memory->over == 2)
    {
        GPU_DrawPanel(RGB(24,10,10), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28), "OUT OF MOVES");
        sprintf(text, "SCORE %u  BEST %u", (unsigned)memory->score, (unsigned)bestScore);
        GPU_DrawTextCentered(C_WHITE, fs, GPU_X/2, (int16_t)(GPU_Y/2-2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G34_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };