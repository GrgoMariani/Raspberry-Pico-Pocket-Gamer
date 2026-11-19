// BIG OR SMALL - number comparison for ages 5-7: two big number cards are
// shown (with counting dots underneath) and the bigger - or smaller - number
// has to be tapped, as the question on top says. Numbers start within 10 and
// grow to 20. No timer, no losing: a wrong tap just says "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define BS_CHOICES 2

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t wantBigger;          /* 1 = tap the bigger, 0 = tap the smaller */
    uint8_t choice[BS_CHOICES];
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game45Memory;

static Game45Memory * const memory = (Game45Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

/* two big cards side by side */
#define CARD_Y 118
#define CARD_H 96
#define CARD_W 120
static int16_t CardX(uint8_t i) { return (int16_t)(24 + i*152); }

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void NewQuestion(void)
{
    /* first steps: always the bigger number, within 10; later both questions
       and numbers up to 20 */
    uint32_t maxN = memory->answered < 6 ? 10 : 20;
    memory->wantBigger = (memory->answered < 4 || Random_Range(2)) ? 1 : 0;
    uint8_t a = (uint8_t)(1 + Random_Range(maxN));
    uint8_t b;
    do { b = (uint8_t)(1 + Random_Range(maxN)); } while (b == a);
    memory->choice[0] = a;
    memory->choice[1] = b;
    uint8_t bigger = (a > b) ? 0 : 1;
    memory->correct = memory->wantBigger ? bigger : (uint8_t)(1 - bigger);
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
    memset(memory, 0, sizeof(Game45Memory));
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
        if (keys & (KEY_LEFT | KEY_RIGHT))
            memory->cursor = (uint8_t)(1 - memory->cursor);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < BS_CHOICES; i++)
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

/* the number with counting dots underneath (rows of five) */
static void DrawNumberCard(uint8_t i)
{
    int16_t x = CardX(i);
    int16_t cx = (int16_t)(x + CARD_W/2);
    char text[4];
    GPU_Color bg = RGB(40,70,110);
    if (memory->phase == 1 && i == memory->correct)
        bg = RGB(30,140,60);
    else if (memory->phase == 2 && i == memory->wrongIdx && (memory->phaseTicks & 2))
        bg = RGB(160,50,40);
    GPU_DrawBevelSquare(bg, x, CARD_Y, CARD_W, CARD_H);
    if (i == memory->cursor)
        GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), CARD_Y-2, CARD_W+4, CARD_H+4);
    sprintf(text, "%u", (unsigned)memory->choice[i]);
    GPU_DrawTextCentered(C_WHITE, &fontDescription[3], cx, CARD_Y + 8, text);
    for (uint8_t d = 0; d < memory->choice[i]; d++)
    {
        int16_t dx = (int16_t)(cx - 20 + (d % 5)*10);
        int16_t dy = (int16_t)(CARD_Y + 46 + (d / 5)*9);
        GPU_DrawFilledCircle(C_YELLOW, dx, dy, 2);
    }
    if (memory->phase == 1 && i == memory->correct)
        GPU_DrawBurst(cx, CARD_Y + CARD_H/2, (uint8_t)(25 - memory->phaseTicks), 25, C_YELLOW);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(20,60,60), RGB(50,110,100), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 64, "BIG OR SMALL");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 100, "two numbers stand side by side");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 114, "tap the bigger one - or the smaller,");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 128, "just read the question on top!");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 146, "the dots help you count - LEFT/RIGHT + UP works too");
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* question + stars */
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6,
                         memory->wantBigger ? "TAP THE BIGGER NUMBER" : "TAP THE SMALLER NUMBER");
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 224, text, (uint16_t)strlen(text));
    }

    /* feedback line */
    if (memory->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 92, praise[memory->stars % 4]);
    }
    else if (memory->phase == 2)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 92, "TRY AGAIN!");
    }

    DrawNumberCard(0);
    DrawNumberCard(1);
}

static void G_Deinit(void)
{
}

const Callbacks G45_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };