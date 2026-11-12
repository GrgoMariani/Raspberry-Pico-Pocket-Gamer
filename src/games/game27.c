// MEMORY - find all the matching pairs of cards

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define MM_COLS 4
#define MM_ROWS 4
#define MM_CELLS (MM_COLS*MM_ROWS)
#define MM_PAIRS (MM_CELLS/2)
#define MM_CELL 52
#define MM_OX ((GPU_X - MM_COLS*MM_CELL)/2)
#define MM_OY 26

/* The logic used to run once per MAIN LOOP iteration instead of once per game
   tick. On the console the main loop spins orders of magnitude faster than
   the frame rate, so the 22 "ticks" a mismatched pair was shown lasted only a
   few milliseconds - the cards snapped back before you could see them, and
   the cursor flew across the board on a single key press. Like every other
   game, Memory now advances on a fixed 20 Hz tick. */
#define NUM_TICKS 20

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

typedef struct {
    uint8_t card[MM_CELLS];     /* symbol on each card */
    uint8_t state[MM_CELLS];    /* 0 down, 1 up, 2 matched */
    uint8_t first;              /* index of the first flipped card, 255 = none */
    uint8_t lockTick;           /* countdown while a mismatched pair is shown */
    uint8_t cursor;
    uint16_t moves;
    uint8_t matched;
    uint8_t over;
    uint32_t startTime;
    uint32_t endTime;
} Game27Memory;

static Game27Memory * const memory = (Game27Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestMoves = 0;

static const GPU_Color _symColor[MM_PAIRS] = {
    C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_C1, C_BLUE, C_MAGENTA, C_CYAN
};

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game27Memory));
    /* build the pairs and shuffle them */
    uint8_t deck[MM_CELLS];
    for (uint8_t i = 0; i < MM_CELLS; i++)
        deck[i] = (uint8_t)(i % MM_PAIRS);
    for (uint8_t i = MM_CELLS-1; i > 0; i--)
    {
        uint8_t j = (uint8_t)Random_Range(i+1);
        uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    for (uint8_t i = 0; i < MM_CELLS; i++)
        memory->card[i] = deck[i];
    memory->first = 255;
    memory->startTime = IF_GetCurrentTime();
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void FlipCard(uint8_t idx)
{
    if (memory->over || memory->lockTick)
        return;
    if (idx >= MM_CELLS || memory->state[idx] != 0)
        return;
    memory->state[idx] = 1;
    if (memory->first == 255)
    {
        memory->first = idx;
        return;
    }
    /* second card of the pair */
    memory->moves++;
    uint8_t a = memory->first;
    if (memory->card[a] == memory->card[idx])
    {
        memory->state[a] = 2;
        memory->state[idx] = 2;
        memory->matched += 2;
        memory->first = 255;
        if (memory->matched >= MM_CELLS)
        {
            memory->over = 1;
            memory->endTime = IF_GetCurrentTime();
            if (bestMoves == 0 || memory->moves < bestMoves)
                bestMoves = memory->moves;
            mainMemory._gameState = GS_DONE;
        }
    }
    else
    {
        memory->lockTick = 22;      /* show the mismatch for a moment */
    }
}

static void Tick(void)
{
    if (memory->lockTick)
    {
        memory->lockTick--;
        if (!memory->lockTick)
        {
            /* flip the mismatched pair back down */
            for (uint8_t i = 0; i < MM_CELLS; i++)
                if (memory->state[i] == 1)
                    memory->state[i] = 0;
            memory->first = 255;
        }
    }
    if (memory->over)
    {
        uint16_t tx, ty;
        if (mainMemory.touchPressedNew || Touch_TakeTap(&tx, &ty) ||
            Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    /* keyboard: LEFT/RIGHT walk the cursor, UP/DOWN flip */
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    if (keys & KEY_LEFT)  memory->cursor = (uint8_t)((memory->cursor + MM_CELLS - 1) % MM_CELLS);
    if (keys & KEY_RIGHT) memory->cursor = (uint8_t)((memory->cursor + 1) % MM_CELLS);
    if (keys & (KEY_UP | KEY_DOWN))
        FlipCard(memory->cursor);
    /* touch taps a card */
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
    {
        int x = ((int)tx - MM_OX)/MM_CELL;
        int y = ((int)ty - MM_OY)/MM_CELL;
        if (x >= 0 && x < MM_COLS && y >= 0 && y < MM_ROWS)
        {
            uint8_t idx = (uint8_t)(y*MM_COLS + x);
            memory->cursor = idx;
            FlipCard(idx);
        }
    }
}

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    /* after a long stall (debugger, menu) do not replay seconds of ticks */
    if (tick > mainMemory._lastRenderedTick + 2*NUM_TICKS)
        mainMemory._lastRenderedTick = tick - 2*NUM_TICKS;
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

/* each symbol is a distinct coloured shape */
static void DrawSymbol(uint8_t sym, int16_t cx, int16_t cy)
{
    GPU_Color c = _symColor[sym];
    switch (sym)
    {
    case 0: GPU_DrawFilledCircle(c, cx, cy, 12); break;
    case 1: GPU_DrawFilledSquare(c, (int16_t)(cx-11), (int16_t)(cy-11), 22, 22); break;
    case 2: GPU_DrawFilledTriangle(c, cx, (int16_t)(cy-12), (int16_t)(cx-12), (int16_t)(cy+10), (int16_t)(cx+12), (int16_t)(cy+10)); break;
    case 3: /* diamond */
        GPU_DrawFilledTriangle(c, cx, (int16_t)(cy-13), (int16_t)(cx-12), cy, (int16_t)(cx+12), cy);
        GPU_DrawFilledTriangle(c, cx, (int16_t)(cy+13), (int16_t)(cx-12), cy, (int16_t)(cx+12), cy);
        break;
    case 4: GPU_DrawEmptyCircle(c, cx, cy, 12); GPU_DrawEmptyCircle(c, cx, cy, 8); break;
    case 5: /* plus */
        GPU_DrawFilledSquare(c, (int16_t)(cx-4), (int16_t)(cy-12), 8, 24);
        GPU_DrawFilledSquare(c, (int16_t)(cx-12), (int16_t)(cy-4), 24, 8);
        break;
    case 6: /* star-ish: two triangles */
        GPU_DrawFilledTriangle(c, cx, (int16_t)(cy-13), (int16_t)(cx-11), (int16_t)(cy+7), (int16_t)(cx+11), (int16_t)(cy+7));
        GPU_DrawFilledTriangle(c, cx, (int16_t)(cy+13), (int16_t)(cx-11), (int16_t)(cy-7), (int16_t)(cx+11), (int16_t)(cy-7));
        break;
    default: /* heart-ish: two circles + triangle */
        GPU_DrawFilledCircle(c, (int16_t)(cx-6), (int16_t)(cy-4), 7);
        GPU_DrawFilledCircle(c, (int16_t)(cx+6), (int16_t)(cy-4), 7);
        GPU_DrawFilledTriangle(c, (int16_t)(cx-12), (int16_t)(cy-2), (int16_t)(cx+12), (int16_t)(cy-2), cx, (int16_t)(cy+12));
        break;
    }
}

static void DrawCard(uint8_t idx)
{
    int x = idx % MM_COLS, y = idx / MM_COLS;
    int16_t px = (int16_t)(MM_OX + x*MM_CELL);
    int16_t py = (int16_t)(MM_OY + y*MM_CELL);
    int16_t cx = (int16_t)(px + MM_CELL/2);
    int16_t cy = (int16_t)(py + MM_CELL/2);
    if (memory->state[idx] == 0)
    {
        GPU_DrawBevelSquare(RGB(60,64,110), px+2, py+2, MM_CELL-5, MM_CELL-5);
        /* a little pattern on the back */
        GPU_DrawEmptySquare(RGB(90,94,150), px+8, py+8, MM_CELL-17, MM_CELL-17);
        GPU_DrawPixel(C_GRAY_LIGHT, (uint16_t)cx, (uint16_t)cy);
    }
    else
    {
        GPU_Color bg = (memory->state[idx] == 2) ? RGB(20,40,24) : RGB(30,32,52);
        GPU_DrawBevelSquare(bg, px+2, py+2, MM_CELL-5, MM_CELL-5);
        if (memory->state[idx] == 2)
            GPU_DrawGlow(_symColor[memory->card[idx]], cx, cy, 16);
        DrawSymbol(memory->card[idx], cx, cy);
    }
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(12,12,32), C_BLACK, 0, 0, GPU_X, GPU_Y);

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

    for (uint8_t i = 0; i < MM_CELLS; i++)
        DrawCard(i);

    /* cursor */
    if (!memory->over)
    {
        int x = memory->cursor % MM_COLS, y = memory->cursor / MM_COLS;
        GPU_DrawEmptySquare(C_YELLOW, (int16_t)(MM_OX + x*MM_CELL), (int16_t)(MM_OY + y*MM_CELL), MM_CELL-1, MM_CELL-1);
    }

    if (memory->over)
    {
        GPU_DrawPanel(RGB(16,24,16), C_C3, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28), "ALL MATCHED!");
        sprintf(text, "%u moves", (unsigned)memory->moves);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G27_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };