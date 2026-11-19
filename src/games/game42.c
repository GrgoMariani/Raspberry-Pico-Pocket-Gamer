// ABC MATCH - letter recognition for young kids (ages 5+): a big letter is
// shown and the matching letter card has to be tapped. The first questions
// match capital to capital, later ones match a capital to its small letter
// ("A" -> "a"), which is exactly what kids learn before school. No timer, no
// losing: a wrong tap just says "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define AM_CHOICES 4

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t target;              /* letter index 0..25, shown as a capital */
    uint8_t smallCards;          /* 1 = the cards show small letters */
    uint8_t choice[AM_CHOICES];  /* letter indices */
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game42Memory;

static Game42Memory * const memory = (Game42Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

/* card layout: four big letter cards along the bottom */
#define CARD_Y 140
#define CARD_H 84
#define CARD_W 68
static int16_t CardX(uint8_t i) { return (int16_t)(8 + i*77); }

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void NewQuestion(void)
{
    memory->target = (uint8_t)Random_Range(26);
    /* first steps: capital = capital; then find the small letter */
    memory->smallCards = (memory->answered >= 4) ? 1 : 0;

    uint8_t vals[AM_CHOICES];
    vals[0] = memory->target;
    uint8_t cnt = 1;
    while (cnt < AM_CHOICES)
    {
        uint8_t v = (uint8_t)Random_Range(26);
        uint8_t dup = 0;
        for (uint8_t j = 0; j < cnt; j++)
            if (vals[j] == v) dup = 1;
        if (!dup)
            vals[cnt++] = v;
    }
    for (uint8_t i = 0; i < AM_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(AM_CHOICES);
        uint8_t t = vals[i]; vals[i] = vals[j]; vals[j] = t;
    }
    for (uint8_t i = 0; i < AM_CHOICES; i++)
    {
        memory->choice[i] = vals[i];
        if (vals[i] == memory->target)
            memory->correct = i;
    }
    memory->phase = 0;
    memory->cursor = 0;
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
    memset(memory, 0, sizeof(Game42Memory));
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
            memory->cursor = (uint8_t)((memory->cursor + AM_CHOICES - 1) % AM_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % AM_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < AM_CHOICES; i++)
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

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    char text[24];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(60,30,90), RGB(110,70,150), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 64, "ABC MATCH");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 100, "look at the big letter");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 114, "then tap the same letter below");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 132, "A and a are the same letter!");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 144, "LEFT/RIGHT + UP works too");
        GPU_DrawTextCentered(C_WHITE, fh, 100, 168, "A");
        GPU_DrawTextCentered(C_WHITE, fh, 220, 168, "a");
        GPU_DrawLine(C_YELLOW, 116, 181, 204, 181);
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* stars HUD */
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 4, text, (uint16_t)strlen(text));
    }

    /* the big target letter in its panel */
    GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 16, memory->smallCards ? "FIND THE SMALL LETTER" : "FIND THE SAME LETTER");
    GPU_DrawPanel(RGB(30,20,60), C_YELLOW, (int16_t)(GPU_X/2-32), 32, 64, 52);
    text[0] = (char)('A' + memory->target);
    text[1] = 0;
    GPU_DrawTextCentered(C_WHITE, fh, GPU_X/2, 45, text);

    /* feedback line */
    if (memory->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 102, praise[memory->stars % 4]);
    }
    else if (memory->phase == 2)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 102, "TRY AGAIN!");
    }

    /* the letter cards */
    for (uint8_t i = 0; i < AM_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(70,40,120);
        if (memory->phase == 1 && i == memory->correct)
            bg = RGB(30,140,60);
        else if (memory->phase == 2 && i == memory->wrongIdx && (memory->phaseTicks & 2))
            bg = RGB(160,50,40);
        GPU_DrawBevelSquare(bg, x, CARD_Y, CARD_W, CARD_H);
        if (i == memory->cursor)
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), CARD_Y-2, CARD_W+4, CARD_H+4);
        text[0] = (char)((memory->smallCards ? 'a' : 'A') + memory->choice[i]);
        text[1] = 0;
        GPU_DrawTextCentered(C_WHITE, fh, (int16_t)(x + CARD_W/2), CARD_Y + (CARD_H - fh->height)/2, text);
        if (memory->phase == 1 && i == memory->correct)
            GPU_DrawBurst((int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, (uint8_t)(25 - memory->phaseTicks), 25, C_YELLOW);
    }
}

static void G_Deinit(void)
{
}

const Callbacks G42_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };