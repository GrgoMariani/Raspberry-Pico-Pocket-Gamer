// MINESWEEPER - clear the field without detonating a mine

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define MS_COLS 12
#define MS_ROWS 9
#define MS_CELL 24
#define MS_OX ((GPU_X - MS_COLS*MS_CELL)/2)
#define MS_OY 22
#define MS_MINES 18

typedef struct {
    uint8_t mine[MS_ROWS][MS_COLS];
    uint8_t adj[MS_ROWS][MS_COLS];
    uint8_t state[MS_ROWS][MS_COLS];   /* 0 hidden, 1 revealed, 2 flagged */
    uint8_t placed;
    uint8_t cursorX, cursorY;
    uint8_t flagMode;
    uint8_t over;          /* 0 playing, 1 lost, 2 won */
    uint8_t started;
    uint16_t flags;
    uint16_t revealed;
    uint32_t startTime;
    uint32_t endTime;
} Game24Memory;

static Game24Memory * const memory = (Game24Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define FLAG_BTN_X 4
#define FLAG_BTN_W 64
#define RESET_BTN_X (GPU_X-70)
#define RESET_BTN_W 66
#define BTN_Y 2
#define BTN_H 16

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game24Memory));
    memory->cursorX = MS_COLS/2;
    memory->cursorY = MS_ROWS/2;
    memory->startTime = IF_GetCurrentTime();
    mainMemory._gameState = GS_RUNNING;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static uint8_t InBounds(int x, int y)
{
    return (uint8_t)(x >= 0 && x < MS_COLS && y >= 0 && y < MS_ROWS);
}

/* place the mines after the first tap so the first cell (and its neighbours)
   are always safe */
static void PlaceMines(int safeX, int safeY)
{
    uint8_t placed = 0;
    while (placed < MS_MINES)
    {
        int x = (int)Random_Range(MS_COLS);
        int y = (int)Random_Range(MS_ROWS);
        int dx = x - safeX, dy = y - safeY;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= 1 && dy <= 1)
            continue;
        if (memory->mine[y][x])
            continue;
        memory->mine[y][x] = 1;
        placed++;
    }
    for (int y = 0; y < MS_ROWS; y++)
        for (int x = 0; x < MS_COLS; x++)
        {
            uint8_t n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if ((dx || dy) && InBounds(x+dx, y+dy) && memory->mine[y+dy][x+dx])
                        n++;
            memory->adj[y][x] = n;
        }
    memory->placed = 1;
}

/* reveal a cell, flood-filling through empty regions */
static void Reveal(int x, int y)
{
    int stackX[MS_ROWS*MS_COLS];
    int stackY[MS_ROWS*MS_COLS];
    int top = 0;
    stackX[top] = x; stackY[top] = y; top++;
    while (top)
    {
        top--;
        int cx = stackX[top], cy = stackY[top];
        if (!InBounds(cx, cy))
            continue;
        if (memory->state[cy][cx] != 0)
            continue;
        memory->state[cy][cx] = 1;
        memory->revealed++;
        if (memory->mine[cy][cx])
        {
            memory->over = 1;
            memory->endTime = IF_GetCurrentTime();
            mainMemory._gameState = GS_DONE;
            return;
        }
        if (memory->adj[cy][cx] == 0)
        {
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if ((dx || dy) && InBounds(cx+dx, cy+dy) && memory->state[cy+dy][cx+dx] == 0 && top < MS_ROWS*MS_COLS)
                    {
                        stackX[top] = cx+dx; stackY[top] = cy+dy; top++;
                    }
        }
    }
    if (memory->revealed >= MS_ROWS*MS_COLS - MS_MINES)
    {
        memory->over = 2;
        memory->endTime = IF_GetCurrentTime();
        mainMemory._gameState = GS_DONE;
    }
}

static void ToggleFlag(int x, int y)
{
    if (!InBounds(x, y) || memory->state[y][x] == 1)
        return;
    if (memory->state[y][x] == 2)
    {
        memory->state[y][x] = 0;
        memory->flags--;
    }
    else
    {
        memory->state[y][x] = 2;
        memory->flags++;
    }
}

static void ActOnCell(int x, int y)
{
    if (memory->over)
        return;
    if (!memory->placed)
        PlaceMines(x, y);
    if (memory->flagMode)
        ToggleFlag(x, y);
    else
        Reveal(x, y);
}

static void G_Update(void)
{
    /* header buttons */
    if (mainMemory.touchPressedNew)
    {
        uint16_t tx = mainMemory.touch_X, ty = mainMemory.touch_Y;
        if (ty < MS_OY)
        {
            if (tx >= FLAG_BTN_X && tx < FLAG_BTN_X+FLAG_BTN_W)
                memory->flagMode = (uint8_t)!memory->flagMode;
            else if (tx >= RESET_BTN_X && tx < RESET_BTN_X+RESET_BTN_W)
                G_Init();
            return;
        }
    }
    if (memory->over)
    {
        if (mainMemory.touchPressedNew)
        {
            int x = ((int)mainMemory.touch_X - MS_OX)/MS_CELL;
            int y = ((int)mainMemory.touch_Y - MS_OY)/MS_CELL;
            if (InBounds(x, y))
                G_Init();
        }
        return;
    }
    /* keyboard: LEFT/RIGHT walk the cursor through the grid, UP reveals,
       DOWN flags. Touch taps a cell directly (FLAG button switches to flagging). */
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    int idx = memory->cursorY*MS_COLS + memory->cursorX;
    int total = MS_ROWS*MS_COLS;
    if (keys & KEY_LEFT)  idx = (idx + total - 1) % total;
    if (keys & KEY_RIGHT) idx = (idx + 1) % total;
    memory->cursorX = (uint8_t)(idx % MS_COLS);
    memory->cursorY = (uint8_t)(idx / MS_COLS);
    if (keys & KEY_UP)
    {
        if (!memory->placed) PlaceMines(memory->cursorX, memory->cursorY);
        Reveal(memory->cursorX, memory->cursorY);
    }
    if (keys & KEY_DOWN)
        ToggleFlag(memory->cursorX, memory->cursorY);
    /* a tap on the board acts on that cell */
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty) && ty >= MS_OY)
    {
        int x = ((int)tx - MS_OX)/MS_CELL;
        int y = ((int)ty - MS_OY)/MS_CELL;
        if (InBounds(x, y))
        {
            memory->cursorX = (uint8_t)x;
            memory->cursorY = (uint8_t)y;
            ActOnCell(x, y);
        }
    }
}

/* the number colours follow the classic palette */
static GPU_Color NumberColor(uint8_t n)
{
    switch (n)
    {
    case 1: return C_C1;
    case 2: return C_GREEN;
    case 3: return C_RED;
    case 4: return C_PURPLE;
    case 5: return C_ORANGE;
    case 6: return C_CYAN;
    case 7: return C_WHITE;
    default: return C_GRAY_LIGHT;
    }
}

static void DrawCell(int x, int y)
{
    int16_t px = (int16_t)(MS_OX + x*MS_CELL);
    int16_t py = (int16_t)(MS_OY + y*MS_CELL);
    const FontDescription * f = &fontDescription[2];
    uint8_t st = memory->state[y][x];
    if (st == 1)
    {
        GPU_DrawFilledSquare(RGB(26,28,44), px, py, MS_CELL-1, MS_CELL-1);
        if (memory->mine[y][x])
        {
            GPU_DrawGlow(C_RED, (int16_t)(px+MS_CELL/2), (int16_t)(py+MS_CELL/2), 9);
            GPU_DrawFilledCircle(C_GRAY_LIGHT, (int16_t)(px+MS_CELL/2), (int16_t)(py+MS_CELL/2), 6);
            GPU_DrawPixel(C_RED, (uint16_t)(px+MS_CELL/2), (uint16_t)(py+MS_CELL/2));
        }
        else if (memory->adj[y][x])
        {
            char n[2] = { (char)('0' + memory->adj[y][x]), 0 };
            GPU_DrawTextCentered(NumberColor(memory->adj[y][x]), f, (int16_t)(px+MS_CELL/2), (int16_t)(py+MS_CELL/2-f->height/2), n);
        }
    }
    else
    {
        GPU_DrawBevelSquare(RGB(70,74,100), px, py, MS_CELL-1, MS_CELL-1);
        if (st == 2)
        {
            /* a little flag */
            int16_t fx = (int16_t)(px+MS_CELL/2);
            int16_t fy = (int16_t)(py+MS_CELL/2);
            GPU_DrawLine(C_GRAY_LIGHT, fx, (int16_t)(fy-7), fx, (int16_t)(fy+6));
            GPU_DrawFilledTriangle(C_RED, fx, (int16_t)(fy-7), fx, (int16_t)(fy-1), (int16_t)(fx+7), (int16_t)(fy-4));
            GPU_DrawLine(C_GRAY_LIGHT, (int16_t)(fx-4), (int16_t)(fy+6), (int16_t)(fx+4), (int16_t)(fy+6));
        }
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,12,30), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* header */
    GPU_DrawButton(memory->flagMode ? C_ORANGE : C_GRAY_DARK, C_WHITE, fs, "FLAG", FLAG_BTN_X, BTN_Y, FLAG_BTN_W, BTN_H);
    int minesLeft = MS_MINES - (int)memory->flags;
    sprintf(text, "MINES %d", minesLeft);
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 5, text);
    GPU_DrawButton(C_GRAY_DARK, C_WHITE, fs, "RESET", RESET_BTN_X, BTN_Y, RESET_BTN_W, BTN_H);

    for (int y = 0; y < MS_ROWS; y++)
        for (int x = 0; x < MS_COLS; x++)
            DrawCell(x, y);

    /* keyboard cursor */
    if (!memory->over)
    {
        int16_t px = (int16_t)(MS_OX + memory->cursorX*MS_CELL);
        int16_t py = (int16_t)(MS_OY + memory->cursorY*MS_CELL);
        GPU_DrawEmptySquare(C_YELLOW, px, py, MS_CELL-1, MS_CELL-1);
    }

    if (memory->over)
    {
        /* reveal all mines on loss */
        if (memory->over == 1)
            for (int y = 0; y < MS_ROWS; y++)
                for (int x = 0; x < MS_COLS; x++)
                    if (memory->mine[y][x] && memory->state[y][x] != 2)
                    {
                        int16_t px = (int16_t)(MS_OX + x*MS_CELL + MS_CELL/2);
                        int16_t py = (int16_t)(MS_OY + y*MS_CELL + MS_CELL/2);
                        GPU_DrawFilledCircle(C_GRAY_MEDIUM, px, py, 5);
                    }
        GPU_DrawPanel(RGB(16,18,48), memory->over == 2 ? C_C3 : C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(memory->over == 2 ? C_C3 : C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28),
                             memory->over == 2 ? "CLEARED!" : "BOOM!");
        uint32_t secs = (memory->endTime - memory->startTime)/ONE_SECOND;
        sprintf(text, "TIME %u s", (unsigned)secs);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fs, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap the board to retry");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G24_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };