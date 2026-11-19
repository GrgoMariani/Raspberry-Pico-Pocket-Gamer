// WHAT TIME? - reading the clock for ages 5-7: an analog clock face shows an
// o'clock time (later also half past) and the matching digital time has to be
// tapped. The short hand is the hour, the long hand the minutes - the classic
// first clock-reading exercise. No timer, no losing: a wrong tap just says
// "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define WT_CHOICES 3

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t hour;                /* 1..12 */
    uint8_t minutes;             /* 0 or 30 */
    uint8_t choiceHour[WT_CHOICES];
    uint8_t choiceMin[WT_CHOICES];
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game48Memory;

static Game48Memory * const memory = (Game48Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

#define CLOCK_X 160
#define CLOCK_Y 96
#define CLOCK_R 50

/* 24 hand directions (every half hour), radius 26, index 0 = 12 o'clock */
static const int8_t _half[24][2] = {
    {0,-26},{7,-25},{13,-23},{18,-18},{23,-13},{25,-7},
    {26,0},{25,7},{23,13},{18,18},{13,23},{7,25},
    {0,26},{-7,25},{-13,23},{-18,18},{-23,13},{-25,7},
    {-26,0},{-25,-7},{-23,-13},{-18,-18},{-13,-23},{-7,-25}
};

/* card layout: three big time buttons along the bottom */
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
    memory->hour = (uint8_t)(1 + Random_Range(12));
    /* first steps: full hours only, then half past joins in */
    memory->minutes = (memory->answered >= 6 && Random_Range(2)) ? 30 : 0;

    memory->choiceHour[0] = memory->hour;
    memory->choiceMin[0] = memory->minutes;
    uint8_t cnt = 1;
    while (cnt < WT_CHOICES)
    {
        uint8_t h = (uint8_t)(1 + Random_Range(12));
        uint8_t m = (memory->answered >= 6 && Random_Range(2)) ? 30 : 0;
        uint8_t dup = 0;
        for (uint8_t j = 0; j < cnt; j++)
            if (memory->choiceHour[j] == h && memory->choiceMin[j] == m)
                dup = 1;
        if (!dup)
        {
            memory->choiceHour[cnt] = h;
            memory->choiceMin[cnt] = m;
            cnt++;
        }
    }
    for (uint8_t i = 0; i < WT_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(WT_CHOICES);
        uint8_t th = memory->choiceHour[i]; memory->choiceHour[i] = memory->choiceHour[j]; memory->choiceHour[j] = th;
        uint8_t tm = memory->choiceMin[i];  memory->choiceMin[i] = memory->choiceMin[j];  memory->choiceMin[j] = tm;
    }
    for (uint8_t i = 0; i < WT_CHOICES; i++)
        if (memory->choiceHour[i] == memory->hour && memory->choiceMin[i] == memory->minutes)
            memory->correct = i;
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
    memset(memory, 0, sizeof(Game48Memory));
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
            memory->cursor = (uint8_t)((memory->cursor + WT_CHOICES - 1) % WT_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % WT_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < WT_CHOICES; i++)
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

static void DrawClock(uint8_t hour, uint8_t minutes)
{
    /* face + rim */
    GPU_DrawFilledCircle(C_WHITE, CLOCK_X, CLOCK_Y, CLOCK_R);
    GPU_DrawEmptyCircle(C_GRAY_DARK, CLOCK_X, CLOCK_Y, CLOCK_R);
    GPU_DrawEmptyCircle(C_GRAY_DARK, CLOCK_X, CLOCK_Y, CLOCK_R-1);
    /* the twelve hour ticks */
    for (uint8_t k = 0; k < 12; k++)
    {
        const int8_t * d = _half[k*2];
        GPU_DrawLine(C_GRAY_DARK,
                     (int16_t)(CLOCK_X + d[0]*44/26), (int16_t)(CLOCK_Y + d[1]*44/26),
                     (int16_t)(CLOCK_X + d[0]*48/26), (int16_t)(CLOCK_Y + d[1]*48/26));
    }
    /* the four cardinal numbers */
    const FontDescription * fs = &fontDescription[0];
    GPU_DrawTextCentered(C_BLACK, fs, CLOCK_X, CLOCK_Y - 42, "12");
    GPU_DrawTextCentered(C_BLACK, fs, CLOCK_X + 40, CLOCK_Y - 4, "3");
    GPU_DrawTextCentered(C_BLACK, fs, CLOCK_X, CLOCK_Y + 34, "6");
    GPU_DrawTextCentered(C_BLACK, fs, CLOCK_X - 40, CLOCK_Y - 4, "9");
    /* minute hand: long, blue (12 or 6) */
    int16_t my = minutes ? 40 : -40;
    GPU_DrawLine(C_C1, CLOCK_X, CLOCK_Y, CLOCK_X, (int16_t)(CLOCK_Y + my));
    GPU_DrawLine(C_C1, (int16_t)(CLOCK_X+1), CLOCK_Y, (int16_t)(CLOCK_X+1), (int16_t)(CLOCK_Y + my));
    /* hour hand: short, dark, moves half a step at half past */
    uint8_t k = (uint8_t)(((hour % 12)*2 + (minutes ? 1 : 0)) % 24);
    GPU_DrawLine(C_BLACK, CLOCK_X, CLOCK_Y, (int16_t)(CLOCK_X + _half[k][0]), (int16_t)(CLOCK_Y + _half[k][1]));
    GPU_DrawLine(C_BLACK, (int16_t)(CLOCK_X+1), CLOCK_Y, (int16_t)(CLOCK_X + _half[k][0] + 1), (int16_t)(CLOCK_Y + _half[k][1]));
    /* pin */
    GPU_DrawFilledCircle(C_RED, CLOCK_X, CLOCK_Y, 3);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(30,40,80), RGB(60,80,130), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 12, "WHAT TIME?");
        DrawClock(3, 0);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 152, "look at the clock, tap the matching time");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 168, "short hand = hour, long hand = minutes");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 182, "LEFT/RIGHT + UP works too");
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* prompt + stars */
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, "WHAT TIME IS IT?");
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 4, text, (uint16_t)strlen(text));
    }

    DrawClock(memory->hour, memory->minutes);

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

    /* the big time cards */
    for (uint8_t i = 0; i < WT_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(45,60,110);
        if (memory->phase == 1 && i == memory->correct)
            bg = RGB(30,140,60);
        else if (memory->phase == 2 && i == memory->wrongIdx && (memory->phaseTicks & 2))
            bg = RGB(160,50,40);
        GPU_DrawBevelSquare(bg, x, CARD_Y, CARD_W, CARD_H);
        if (i == memory->cursor)
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), CARD_Y-2, CARD_W+4, CARD_H+4);
        sprintf(text, "%u:%02u", (unsigned)memory->choiceHour[i], (unsigned)memory->choiceMin[i]);
        GPU_DrawTextCentered(C_WHITE, fb, (int16_t)(x + CARD_W/2), CARD_Y + (CARD_H - fb->height)/2, text);
        if (memory->phase == 1 && i == memory->correct)
            GPU_DrawBurst((int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, (uint8_t)(25 - memory->phaseTicks), 25, C_YELLOW);
    }
}

static void G_Deinit(void)
{
}

const Callbacks G48_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };