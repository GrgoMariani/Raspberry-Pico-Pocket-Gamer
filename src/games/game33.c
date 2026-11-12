// 15 PUZZLE - slide the tiles back into order

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define FP_N 4
#define FP_CELLS (FP_N*FP_N)
#define FP_CELL 50
#define FP_OX ((GPU_X - FP_N*FP_CELL)/2)
#define FP_OY 32

typedef struct {
    uint8_t tile[FP_CELLS];   /* 0 = the empty gap, else 1..15 */
    uint8_t gap;              /* index of the empty cell */
    uint16_t moves;
    uint8_t over;             /* 0 playing, 1 solved */
    uint32_t startTime;
    uint32_t endTime;
} Game33Memory;

static Game33Memory * const memory = (Game33Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestMoves = 0;

static uint8_t Solved(void)
{
    for (uint8_t i = 0; i < FP_CELLS-1; i++)
        if (memory->tile[i] != (uint8_t)(i+1))
            return 0;
    return (uint8_t)(memory->tile[FP_CELLS-1] == 0);
}

/* swap the gap with a neighbouring cell (index must be adjacent) */
static void SwapGap(uint8_t idx)
{
    memory->tile[memory->gap] = memory->tile[idx];
    memory->tile[idx] = 0;
    memory->gap = idx;
    memory->moves++;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game33Memory));
    for (uint8_t i = 0; i < FP_CELLS-1; i++)
        memory->tile[i] = (uint8_t)(i+1);
    memory->tile[FP_CELLS-1] = 0;
    memory->gap = FP_CELLS-1;
    /* scramble by making random legal moves from the solved board: that way
       the result is always solvable (a random permutation is not) */
    uint8_t lastGap = 255;
    for (uint16_t n = 0; n < 250; n++)
    {
        uint8_t gr = (uint8_t)(memory->gap / FP_N), gc = (uint8_t)(memory->gap % FP_N);
        uint8_t options[4];
        uint8_t no = 0;
        if (gr > 0)      options[no++] = (uint8_t)(memory->gap - FP_N);
        if (gr < FP_N-1) options[no++] = (uint8_t)(memory->gap + FP_N);
        if (gc > 0)      options[no++] = (uint8_t)(memory->gap - 1);
        if (gc < FP_N-1) options[no++] = (uint8_t)(memory->gap + 1);
        /* avoid immediately undoing the previous move so the shuffle spreads */
        uint8_t pick;
        uint8_t tries = 0;
        do {
            pick = options[Random_Range(no)];
            tries++;
        } while (pick == lastGap && no > 1 && tries < 8);
        lastGap = memory->gap;
        /* inline swap (does not count as a player move) */
        memory->tile[memory->gap] = memory->tile[pick];
        memory->tile[pick] = 0;
        memory->gap = pick;
    }
    memory->moves = 0;
    memory->over = 0;
    memory->startTime = IF_GetCurrentTime();
    mainMemory._gameState = GS_RUNNING;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

/* arrow keys slide a tile INTO the gap: LEFT pulls the tile right of the gap
   leftwards, etc. (the gap itself moves the opposite way) */
static void KeySlide(void)
{
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    if (memory->over)
        return;
    uint8_t gr = (uint8_t)(memory->gap / FP_N), gc = (uint8_t)(memory->gap % FP_N);
    if ((keys & KEY_LEFT) && gc < FP_N-1)  SwapGap((uint8_t)(memory->gap + 1));
    else if ((keys & KEY_RIGHT) && gc > 0) SwapGap((uint8_t)(memory->gap - 1));
    else if ((keys & KEY_UP) && gr < FP_N-1) SwapGap((uint8_t)(memory->gap + FP_N));
    else if ((keys & KEY_DOWN) && gr > 0)    SwapGap((uint8_t)(memory->gap - FP_N));
}

/* tapping a tile in the gap's row or column slides the whole segment */
static void TapSlide(uint16_t tx, uint16_t ty)
{
    if (memory->over)
        return;
    int x = ((int)tx - FP_OX)/FP_CELL;
    int y = ((int)ty - FP_OY)/FP_CELL;
    if (x < 0 || x >= FP_N || y < 0 || y >= FP_N)
        return;
    uint8_t idx = (uint8_t)(y*FP_N + x);
    if (idx == memory->gap)
        return;
    uint8_t gr = (uint8_t)(memory->gap / FP_N), gc = (uint8_t)(memory->gap % FP_N);
    if ((uint8_t)y == gr)
    {
        /* slide every tile between the tapped cell and the gap */
        if ((uint8_t)x < gc)
            for (uint8_t c = gc; c > (uint8_t)x; c--) SwapGap((uint8_t)(gr*FP_N + c-1));
        else
            for (uint8_t c = gc; c < (uint8_t)x; c++) SwapGap((uint8_t)(gr*FP_N + c+1));
    }
    else if ((uint8_t)x == gc)
    {
        if ((uint8_t)y < gr)
            for (uint8_t r = gr; r > (uint8_t)y; r--) SwapGap((uint8_t)((r-1)*FP_N + gc));
        else
            for (uint8_t r = gr; r < (uint8_t)y; r++) SwapGap((uint8_t)((r+1)*FP_N + gc));
    }
}

static void CheckWin(void)
{
    if (!memory->over && Solved())
    {
        memory->over = 1;
        memory->endTime = IF_GetCurrentTime();
        if (bestMoves == 0 || memory->moves < bestMoves)
            bestMoves = memory->moves;
        mainMemory._gameState = GS_DONE;
    }
}

static void G_Update(void)
{
    if (memory->over)
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    KeySlide();
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
        TapSlide(tx, ty);
    CheckWin();
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(14,12,34), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* header */
    sprintf(text, "MOVES %u", (unsigned)memory->moves);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)strlen(text));
    uint32_t now = memory->over ? memory->endTime : IF_GetCurrentTime();
    sprintf(text, "TIME %u", (unsigned)((now - memory->startTime)/ONE_SECOND));
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 4, text);
    if (bestMoves)
    {
        sprintf(text, "BEST %u", (unsigned)bestMoves);
        GPU_DrawText(C_GRAY_LIGHT, fm, (int16_t)(GPU_X - 6 - strlen(text)*fm->width), 4, text, (uint16_t)strlen(text));
    }

    /* board */
    GPU_DrawPanel(RGB(20,20,44), C_GRAY_DARK, (int16_t)(FP_OX-4), (int16_t)(FP_OY-4),
                  (int16_t)(FP_N*FP_CELL+8), (int16_t)(FP_N*FP_CELL+8));
    for (uint8_t i = 0; i < FP_CELLS; i++)
    {
        int16_t px = (int16_t)(FP_OX + (i % FP_N)*FP_CELL);
        int16_t py = (int16_t)(FP_OY + (i / FP_N)*FP_CELL);
        uint8_t t = memory->tile[i];
        if (t == 0)
        {
            GPU_DrawFilledSquare(RGB(12,12,24), px+1, py+1, FP_CELL-2, FP_CELL-2);
            continue;
        }
        /* a tile already in its goal position gets a green tint */
        uint8_t home = (uint8_t)(t == (uint8_t)(i+1));
        GPU_Color bg = home ? RGB(30,80,44) : RGB(52,56,104);
        GPU_DrawBevelSquare(bg, px+2, py+2, FP_CELL-4, FP_CELL-4);
        sprintf(text, "%u", (unsigned)t);
        GPU_DrawTextCentered(C_WHITE, fb, (int16_t)(px+FP_CELL/2), (int16_t)(py+FP_CELL/2 - fb->height/2), text);
    }

    GPU_DrawTextCentered(C_GRAY_DARK, fs, GPU_X/2, (int16_t)(GPU_Y-10), "arrows slide a tile, or tap a row/column");

    if (memory->over)
    {
        GPU_DrawPanel(RGB(16,24,16), C_C3, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28), "SOLVED!");
        sprintf(text, "%u moves", (unsigned)memory->moves);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G33_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };