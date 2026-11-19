// COUNT IT! - counting practice for young kids (ages 5+): count the apples,
// balloons or stars on the screen and tap the matching number. There is no
// timer and no losing - a wrong tap just says "try again" - so it works well
// in a kindergarten / classroom setting. The range grows gently: 1..3, then
// 1..5, then 1..10 as more questions are answered.

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define CI_CHOICES 3

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;              /* keyboard selection */
    uint8_t wrongIdx;            /* card that flashes on a wrong tap */
    uint8_t count;               /* the number to count */
    uint8_t objType;             /* 0 apple, 1 balloon, 2 star */
    uint32_t seed;               /* layout jitter per question */
    uint8_t choice[CI_CHOICES];
    uint8_t correct;             /* index of the right card */
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game41Memory;

static Game41Memory * const memory = (Game41Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

static const GPU_Color _balloonCol[6] = { C_RED, C_C1, C_GREEN, C_ORANGE, C_PURPLE, C_C2 };

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
    /* gentle difficulty ramp */
    uint32_t maxN = memory->answered < 4 ? 3 : (memory->answered < 10 ? 5 : 10);
    memory->count = (uint8_t)(1 + Random_Range(maxN));
    memory->objType = (uint8_t)Random_Range(3);
    memory->seed = Random_Next();

    /* the right number plus two nearby distractors */
    uint8_t vals[CI_CHOICES];
    vals[0] = memory->count;
    uint8_t cnt = 1;
    while (cnt < CI_CHOICES)
    {
        int8_t v = (int8_t)((int8_t)memory->count + (int8_t)Random_Range(5) - 2);
        if (v < 1 || v > 10 || v == (int8_t)memory->count)
            continue;
        uint8_t dup = 0;
        for (uint8_t j = 0; j < cnt; j++)
            if (vals[j] == (uint8_t)v) dup = 1;
        if (!dup)
            vals[cnt++] = (uint8_t)v;
    }
    for (uint8_t i = 0; i < CI_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(CI_CHOICES);
        uint8_t t = vals[i]; vals[i] = vals[j]; vals[j] = t;
    }
    for (uint8_t i = 0; i < CI_CHOICES; i++)
    {
        memory->choice[i] = vals[i];
        if (vals[i] == memory->count)
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
        /* no penalty: the same question stays, just try again */
        memory->streak = 0;
        memory->wrongIdx = idx;
        memory->phase = 2;
        memory->phaseTicks = 12;
    }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game41Memory));
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
            memory->cursor = (uint8_t)((memory->cursor + CI_CHOICES - 1) % CI_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % CI_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < CI_CHOICES; i++)
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

static void DrawStarShape(GPU_Color color, int16_t x, int16_t y, int16_t r)
{
    GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y+r*2/3), (int16_t)(x+r), (int16_t)(y+r*2/3), x, (int16_t)(y-r));
    GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y-r*2/3), (int16_t)(x+r), (int16_t)(y-r*2/3), x, (int16_t)(y+r));
}

static void DrawObject(uint8_t type, uint8_t i, int16_t x, int16_t y)
{
    if (type == 0)
    {
        /* apple */
        GPU_DrawFilledCircle(C_RED, x, y, 10);
        GPU_DrawFilledSquare(RGB(110,70,30), (int16_t)(x-1), (int16_t)(y-14), 2, 5);
        GPU_DrawFilledSquare(C_GREEN, (int16_t)(x+1), (int16_t)(y-13), 5, 3);
        GPU_DrawPixel(C_WHITE, (uint16_t)(x-4), (uint16_t)(y-4));
    }
    else if (type == 1)
    {
        /* balloon */
        GPU_Color c = _balloonCol[i % 6];
        GPU_DrawFilledCircle(c, x, (int16_t)(y-3), 9);
        GPU_DrawFilledTriangle(c, (int16_t)(x-3), (int16_t)(y+5), (int16_t)(x+3), (int16_t)(y+5), x, (int16_t)(y+8));
        GPU_DrawLine(C_GRAY_LIGHT, x, (int16_t)(y+8), x, (int16_t)(y+16));
        GPU_DrawPixel(C_WHITE, (uint16_t)(x-3), (uint16_t)(y-6));
    }
    else
    {
        /* star */
        DrawStarShape(C_YELLOW, x, y, 11);
        GPU_DrawFilledCircle(C_ORANGE, x, y, 3);
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
    GPU_DrawVerticalGradient(RGB(30,40,90), RGB(70,90,150), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 64, "COUNT IT!");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 100, "count the things on the screen");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 114, "then tap the right number");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 132, "LEFT/RIGHT + UP works too");
        /* a little decoration */
        DrawObject(0, 0, 60, 180);
        DrawObject(1, 1, 160, 180);
        DrawObject(2, 2, 260, 180);
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* prompt + stars */
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, "HOW MANY?");
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 4, text, (uint16_t)strlen(text));
    }

    /* the objects to count: a jittered grid, max 2 rows of 5 */
    for (uint8_t i = 0; i < memory->count; i++)
    {
        uint32_t hh = (uint32_t)i * 2654435761u + memory->seed;
        int16_t x = (int16_t)(44 + (i % 5)*58 + (int16_t)(hh % 7) - 3);
        int16_t y = (int16_t)(66 + (i / 5)*56 + (int16_t)((hh >> 3) % 7) - 3);
        DrawObject(memory->objType, i, x, y);
    }

    /* feedback line between objects and cards */
    if (memory->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 150, praise[memory->stars % 4]);
    }
    else if (memory->phase == 2)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 150, "TRY AGAIN!");
    }

    /* the big number cards */
    for (uint8_t i = 0; i < CI_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(50,60,120);
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

const Callbacks G41_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };