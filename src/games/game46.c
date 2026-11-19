// SUBTRACT IT - first subtraction for ages 5-7: a group of apples is shown,
// some of them are crossed out, and the number left over has to be tapped.
// The pictures let kids count their way to the answer, everything stays
// within 10. No timer, no losing: a wrong tap just says "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define SI_CHOICES 3

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t a, b;                /* a apples, b of them crossed out */
    uint8_t choice[SI_CHOICES];
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game46Memory;

static Game46Memory * const memory = (Game46Memory*)(&mainMemory.sharedMemory.memblocks[0]);
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
    /* start with groups up to 5, then up to 10; at least one apple stays */
    uint32_t maxA = memory->answered < 4 ? 5 : 10;
    memory->a = (uint8_t)(2 + Random_Range(maxA - 1));
    memory->b = (uint8_t)(1 + Random_Range(memory->a - 1));
    uint8_t rest = (uint8_t)(memory->a - memory->b);

    uint8_t vals[SI_CHOICES];
    vals[0] = rest;
    uint8_t cnt = 1;
    while (cnt < SI_CHOICES)
    {
        int8_t v = (int8_t)((int8_t)rest + (int8_t)Random_Range(5) - 2);
        if (v < 1 || v > 10 || v == (int8_t)rest)
            continue;
        uint8_t dup = 0;
        for (uint8_t j = 0; j < cnt; j++)
            if (vals[j] == (uint8_t)v) dup = 1;
        if (!dup)
            vals[cnt++] = (uint8_t)v;
    }
    for (uint8_t i = 0; i < SI_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(SI_CHOICES);
        uint8_t t = vals[i]; vals[i] = vals[j]; vals[j] = t;
    }
    for (uint8_t i = 0; i < SI_CHOICES; i++)
    {
        memory->choice[i] = vals[i];
        if (vals[i] == rest)
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
    memset(memory, 0, sizeof(Game46Memory));
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
            memory->cursor = (uint8_t)((memory->cursor + SI_CHOICES - 1) % SI_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % SI_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < SI_CHOICES; i++)
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

static void DrawApple(int16_t x, int16_t y, uint8_t crossed)
{
    GPU_Color body = crossed ? GPU_ShadeColor(C_RED, 110) : C_RED;
    GPU_DrawFilledCircle(body, x, y, 8);
    GPU_DrawFilledSquare(RGB(110,70,30), (int16_t)(x-1), (int16_t)(y-11), 2, 4);
    GPU_DrawFilledSquare(crossed ? GPU_ShadeColor(C_GREEN, 110) : C_GREEN, (int16_t)(x+1), (int16_t)(y-10), 4, 3);
    if (crossed)
    {
        /* a big red X: this apple is taken away */
        GPU_DrawLine(C_RED, (int16_t)(x-8), (int16_t)(y-8), (int16_t)(x+8), (int16_t)(y+8));
        GPU_DrawLine(C_RED, (int16_t)(x-8), (int16_t)(y+8), (int16_t)(x+8), (int16_t)(y-8));
        GPU_DrawLine(C_RED, (int16_t)(x-7), (int16_t)(y-8), (int16_t)(x+9), (int16_t)(y+8));
        GPU_DrawLine(C_RED, (int16_t)(x-7), (int16_t)(y+8), (int16_t)(x+9), (int16_t)(y-8));
    }
    else
    {
        GPU_DrawPixel(C_WHITE, (uint16_t)(x-3), (uint16_t)(y-3));
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
    GPU_DrawVerticalGradient(RGB(60,30,50), RGB(110,60,90), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 56, "SUBTRACT IT!");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 90, "some apples are crossed out");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 104, "tap how many are left over");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 122, "LEFT/RIGHT + UP works too");
        DrawApple(100, 170, 0);
        DrawApple(140, 170, 0);
        DrawApple(180, 170, 1);
        DrawApple(220, 170, 1);
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* prompt + stars */
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, "HOW MANY ARE LEFT?");
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 231, text, (uint16_t)strlen(text));
    }

    /* the apples: rows of five, the last b are crossed out */
    for (uint8_t i = 0; i < memory->a; i++)
    {
        int16_t x = (int16_t)(44 + (i % 5)*58);
        int16_t y = (i < 5) ? 64 : 108;
        DrawApple(x, y, (uint8_t)(i >= memory->a - memory->b));
    }

    /* the sum in symbols under the picture */
    sprintf(text, "%u - %u = ?", (unsigned)memory->a, (unsigned)memory->b);
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
    for (uint8_t i = 0; i < SI_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(90,45,75);
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

const Callbacks G46_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };