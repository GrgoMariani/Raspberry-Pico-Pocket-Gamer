// PATTERN PLAY - logic and patterns for ages 5-7: a row of coloured shapes
// repeats a pattern (A B A B ..., A A B B ..., A B C ... or A B B ...) and the
// shape that comes next has to be tapped. Patterns start with two elements
// and grow to three. No timer, no losing: a wrong tap just says "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define PP_CHOICES 3
#define PP_SLOTS 6               /* five shown + the question mark */
#define PP_SHAPES 6
#define PP_COLORS 6

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t elemShape[PP_CHOICES];   /* the three distinct pattern elements */
    uint8_t elemColor[PP_CHOICES];
    uint8_t period;              /* 2, 3 or 4 slots before the repeat */
    uint8_t map[4];              /* element index per slot of the period */
    uint8_t choice[PP_CHOICES];  /* element indices on the cards */
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game47Memory;

static Game47Memory * const memory = (Game47Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

static const GPU_Color _colorVal[PP_COLORS] = {
    C_RED, C_BLUE, C_GREEN, C_YELLOW, C_ORANGE, C_PURPLE
};

/* the row of pattern slots */
#define SLOT_Y 56
#define SLOT_H 56
#define SLOT_W 46
static int16_t SlotX(uint8_t i) { return (int16_t)(8 + i*52); }

/* card layout: three big picture cards along the bottom */
#define CARD_Y 150
#define CARD_H 74
#define CARD_W 88
static int16_t CardX(uint8_t i) { return (int16_t)(12 + i*102); }

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void NewQuestion(void)
{
    /* three distinct shape+colour elements */
    for (uint8_t i = 0; i < PP_CHOICES; i++)
    {
        uint8_t dup;
        do
        {
            dup = 0;
            memory->elemShape[i] = (uint8_t)Random_Range(PP_SHAPES);
            memory->elemColor[i] = (uint8_t)Random_Range(PP_COLORS);
            for (uint8_t j = 0; j < i; j++)
                if (memory->elemShape[j] == memory->elemShape[i] &&
                    memory->elemColor[j] == memory->elemColor[i])
                    dup = 1;
        } while (dup);
    }

    /* pattern kinds unlock gently: AB, then AABB/ABB, then ABC */
    uint32_t kinds = memory->answered < 4 ? 1 : (memory->answered < 10 ? 3 : 4);
    uint32_t kind = Random_Range(kinds);
    if (kind == 0)      { memory->period = 2; memory->map[0] = 0; memory->map[1] = 1; }
    else if (kind == 1) { memory->period = 4; memory->map[0] = 0; memory->map[1] = 0; memory->map[2] = 1; memory->map[3] = 1; }
    else if (kind == 2) { memory->period = 3; memory->map[0] = 0; memory->map[1] = 1; memory->map[2] = 1; }
    else                { memory->period = 3; memory->map[0] = 0; memory->map[1] = 1; memory->map[2] = 2; }

    /* the cards offer all three elements; the right one continues the row */
    uint8_t answer = memory->map[5 % memory->period];
    uint8_t vals[PP_CHOICES] = { 0, 1, 2 };
    for (uint8_t i = 0; i < PP_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(PP_CHOICES);
        uint8_t t = vals[i]; vals[i] = vals[j]; vals[j] = t;
    }
    for (uint8_t i = 0; i < PP_CHOICES; i++)
    {
        memory->choice[i] = vals[i];
        if (vals[i] == answer)
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
    memset(memory, 0, sizeof(Game47Memory));
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
            memory->cursor = (uint8_t)((memory->cursor + PP_CHOICES - 1) % PP_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % PP_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < PP_CHOICES; i++)
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

static void DrawShape(uint8_t shape, GPU_Color color, int16_t x, int16_t y, int16_t r)
{
    switch (shape)
    {
        case 0:     /* circle */
            GPU_DrawFilledCircle(color, x, y, r);
            break;
        case 1:     /* square */
            GPU_DrawFilledSquare(color, (int16_t)(x-r*3/4), (int16_t)(y-r*3/4), (int16_t)(r*3/2), (int16_t)(r*3/2));
            break;
        case 2:     /* triangle */
            GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y+r*3/4), (int16_t)(x+r), (int16_t)(y+r*3/4), x, (int16_t)(y-r));
            break;
        case 3:     /* star */
            GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y+r*2/3), (int16_t)(x+r), (int16_t)(y+r*2/3), x, (int16_t)(y-r));
            GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y-r*2/3), (int16_t)(x+r), (int16_t)(y-r*2/3), x, (int16_t)(y+r));
            break;
        case 4:     /* heart */
            GPU_DrawFilledCircle(color, (int16_t)(x-r/2), (int16_t)(y-r/3), (int16_t)(r/2+1));
            GPU_DrawFilledCircle(color, (int16_t)(x+r/2), (int16_t)(y-r/3), (int16_t)(r/2+1));
            GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y-r/6), (int16_t)(x+r), (int16_t)(y-r/6), x, (int16_t)(y+r));
            break;
        default:    /* diamond */
            GPU_DrawFilledTriangle(color, (int16_t)(x-r*3/4), y, (int16_t)(x+r*3/4), y, x, (int16_t)(y-r));
            GPU_DrawFilledTriangle(color, (int16_t)(x-r*3/4), y, (int16_t)(x+r*3/4), y, x, (int16_t)(y+r));
            break;
    }
}

static void DrawElement(uint8_t elem, int16_t x, int16_t y, int16_t r)
{
    DrawShape(memory->elemShape[elem], _colorVal[memory->elemColor[elem]], x, y, r);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    char text[24];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(40,30,80), RGB(80,60,140), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 56, "PATTERN PLAY");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 90, "the shapes follow a pattern");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 104, "tap the one that comes next");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 122, "LEFT/RIGHT + UP works too");
        /* a little A B A ? demo */
        GPU_DrawFilledCircle(C_RED, 80, 170, 14);
        GPU_DrawFilledSquare(C_YELLOW, 126, 156, 28, 28);
        GPU_DrawFilledCircle(C_RED, 180, 170, 14);
        GPU_DrawTextCentered(C_WHITE, fh, 234, 157, "?");
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* prompt + stars */
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, "WHAT COMES NEXT?");
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    if (bestStreak)
    {
        sprintf(text, "BEST ROW %u", (unsigned)bestStreak);
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, 4, text, (uint16_t)strlen(text));
    }

    /* the pattern row: five shapes and a question mark */
    for (uint8_t i = 0; i < PP_SLOTS; i++)
    {
        int16_t x = SlotX(i);
        GPU_DrawBevelSquare(RGB(50,40,100), x, SLOT_Y, SLOT_W, SLOT_H);
        if (i < PP_SLOTS - 1)
        {
            DrawElement(memory->map[i % memory->period],
                        (int16_t)(x + SLOT_W/2), SLOT_Y + SLOT_H/2, 14);
        }
        else
        {
            /* the missing piece pulses gently */
            GPU_Color qc = (memory->animTick & 8) ? C_YELLOW : C_WHITE;
            GPU_DrawTextCentered(qc, fh, (int16_t)(x + SLOT_W/2), SLOT_Y + (SLOT_H - fh->height)/2, "?");
        }
    }

    /* feedback line */
    if (memory->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 122, praise[memory->stars % 4]);
    }
    else if (memory->phase == 2)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 122, "TRY AGAIN!");
    }

    /* the picture cards */
    for (uint8_t i = 0; i < PP_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(60,45,120);
        if (memory->phase == 1 && i == memory->correct)
            bg = RGB(30,140,60);
        else if (memory->phase == 2 && i == memory->wrongIdx && (memory->phaseTicks & 2))
            bg = RGB(160,50,40);
        GPU_DrawBevelSquare(bg, x, CARD_Y, CARD_W, CARD_H);
        if (i == memory->cursor)
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), CARD_Y-2, CARD_W+4, CARD_H+4);
        DrawElement(memory->choice[i], (int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, 20);
        if (memory->phase == 1 && i == memory->correct)
            GPU_DrawBurst((int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, (uint8_t)(25 - memory->phaseTicks), 25, C_YELLOW);
    }
}

static void G_Deinit(void)
{
}

const Callbacks G47_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };