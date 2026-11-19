// ADD IT UP - first addition for young kids (ages 5+): two groups of apples
// are shown side by side with a big plus sign, and the total has to be tapped.
// The pictures let kids count their way to the answer, sums stay within 10.
// No timer, no losing: a wrong tap just says "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define AI_CHOICES 3

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t a, b;                /* the two apple groups */
    uint8_t choice[AI_CHOICES];
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game44Memory;

static Game44Memory * const memory = (Game44Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

/* card layout: three big number buttons along the bottom */
#define CARD_Y 178
#define CARD_H 52
#define CARD_W 88
static int16_t CardX(uint8_t i) { return (int16_t)(12 + i*102); }

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void NewQuestion(void)
{
    /* start with sums up to 5, then up to 10 */
    if (memory->answered < 4)
    {
        memory->a = (uint8_t)(1 + Random_Range(4));
        memory->b = (uint8_t)(1 + Random_Range(5 - memory->a));
    }
    else
    {
        memory->a = (uint8_t)(1 + Random_Range(5));
        memory->b = (uint8_t)(1 + Random_Range(5));
    }
    uint8_t sum = (uint8_t)(memory->a + memory->b);

    uint8_t vals[AI_CHOICES];
    vals[0] = sum;
    uint8_t cnt = 1;
    while (cnt < AI_CHOICES)
    {
        int8_t v = (int8_t)((int8_t)sum + (int8_t)Random_Range(5) - 2);
        if (v < 2 || v > 10 || v == (int8_t)sum)
            continue;
        uint8_t dup = 0;
        for (uint8_t j = 0; j < cnt; j++)
            if (vals[j] == (uint8_t)v) dup = 1;
        if (!dup)
            vals[cnt++] = (uint8_t)v;
    }
    for (uint8_t i = 0; i < AI_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(AI_CHOICES);
        uint8_t t = vals[i]; vals[i] = vals[j]; vals[j] = t;
    }
    for (uint8_t i = 0; i < AI_CHOICES; i++)
    {
        memory->choice[i] = vals[i];
        if (vals[i] == sum)
            memory->correct = i;
    }
    memory->phase = 0;
    memory->cursor = 1;
}

static void Answer(uint8_t idx)
{
    if (memory->state != 1 || memory->phase != 0)
        return;
    if (idx == memory->correct)
    {
        memory->stars++;
        memory->streak++;
        memory->answered++;
        if (memory->streak > bestStreak)
            bestStreak = memory->streak;
        memory->phase = 1;
        memory->phaseTicks = 25;
    }
    else
    {
        memory->streak = 0;
        memory->wrongIdx = idx;
        memory->phase = 2;
        memory->phaseTicks = 12;
    }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game44Memory));
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void Tick(void)
{
    memory->animTick++;
    if (memory->state != 1 || memory->phase == 0)
        return;
    if (memory->phaseTicks)
        memory->phaseTicks--;
    if (memory->phaseTicks == 0)
    {
        if (memory->phase == 1)
            NewQuestion();
        else
            memory->phase = 0;
    }
}

static void G_Update(void)
{
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    uint16_t tx, ty;
    uint8_t tap = Touch_TakeTap(&tx, &ty);

    if (memory->state == 0)
    {
        if (keys != KEY_NONE || tap)
        {
            memory->state = 1;
            NewQuestion();
        }
    }
    else if (memory->phase == 0)
    {
        if (keys & KEY_LEFT)
            memory->cursor = (uint8_t)((memory->cursor + AI_CHOICES - 1) % AI_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % AI_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < AI_CHOICES; i++)
                if (tx >= CardX(i) && tx < CardX(i) + CARD_W)
                {
                    memory->cursor = i;
                    Answer(i);
                    break;
                }
        }
    }

    uint32_t tick = CalculateTick();
    if (tick > mainMemory._lastRenderedTick + 4*NUM_TICKS)
        mainMemory._lastRenderedTick = tick - 4*NUM_TICKS;
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

/* ------------------------------------------------------------------ drawing */

static void DrawApple(int16_t x, int16_t y)
{
    GPU_DrawFilledCircle(C_RED, x, y, 8);
    GPU_DrawFilledSquare(RGB(110,70,30), (int16_t)(x-1), (int16_t)(y-11), 2, 4);
    GPU_DrawFilledSquare(C_GREEN, (int16_t)(x+1), (int16_t)(y-10), 4, 3);
    GPU_DrawPixel(C_WHITE, (uint16_t)(x-3), (uint16_t)(y-3));
}

/* a group of up to 5 apples: a row of 3 over a row of 2 */
static void DrawGroup(uint8_t n, int16_t cx)
{
    for (uint8_t i = 0; i < n && i < 5; i++)
    {
        int16_t x = (i < 3) ? (int16_t)(cx - 24 + i*24) : (int16_t)(cx - 12 + (i-3)*24);
        int16_t y = (i < 3) ? 70 : 100;
        DrawApple(x, y);
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    char text[24];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(70,40,20), RGB(130,90,50), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 56, "ADD IT UP!");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 90, "count the apples on both sides");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 104, "then tap how many all together");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 122, "LEFT/RIGHT + UP works too");
        DrawGroup(2, 100);
        GPU_DrawFilledSquare(C_WHITE, 150, 79, 20, 6);
        GPU_DrawFilledSquare(C_WHITE, 157, 72, 6, 20);
        DrawGroup(1, 220);
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* prompt + stars */
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, "ADD THE APPLES!");
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 4, text, (uint16_t)strlen(text));
    }

    /* the two groups with a big plus between them */
    DrawGroup(memory->a, 78);
    GPU_DrawFilledSquare(C_WHITE, 150, 79, 20, 6);
    GPU_DrawFilledSquare(C_WHITE, 157, 72, 6, 20);
    DrawGroup(memory->b, 242);

    /* the sum in symbols under the picture */
    sprintf(text, "%u + %u = ?", (unsigned)memory->a, (unsigned)memory->b);
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 128, text);

    /* feedback line */
    if (memory->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 152, praise[memory->stars % 4]);
    }
    else if (memory->phase == 2)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 152, "TRY AGAIN!");
    }

    /* the big number cards */
    for (uint8_t i = 0; i < AI_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(110,70,30);
        if (memory->phase == 1 && i == memory->correct)
            bg = RGB(30,140,60);
        else if (memory->phase == 2 && i == memory->wrongIdx && (memory->phaseTicks & 2))
            bg = RGB(160,50,40);
        GPU_DrawBevelSquare(bg, x, CARD_Y, CARD_W, CARD_H);
        if (i == memory->cursor)
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), CARD_Y-2, CARD_W+4, CARD_H+4);
        sprintf(text, "%u", (unsigned)memory->choice[i]);
        GPU_DrawTextCentered(C_WHITE, fh, (int16_t)(x + CARD_W/2), CARD_Y + (CARD_H - fh->height)/2, text);
        if (memory->phase == 1 && i == memory->correct)
            GPU_DrawBurst((int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, (uint8_t)(25 - memory->phaseTicks), 25, C_YELLOW);
    }
}

static void G_Deinit(void)
{
}

const Callbacks G44_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };