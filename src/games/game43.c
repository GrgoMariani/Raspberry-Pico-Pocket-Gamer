// SHAPE HUNT - shapes and colours for young kids (ages 5+): a big target is
// shown at the top (a shape silhouette with its name, or a colour swatch with
// its name) and the matching card has to be tapped. The pictures make it
// playable even before kids can read. No timer, no losing: a wrong tap just
// says "try again".

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define SH_CHOICES 4
#define SH_SHAPES 6
#define SH_COLORS 6

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t mode;                /* 0 find the shape, 1 find the colour */
    uint8_t target;              /* shape index (mode 0) / colour index (mode 1) */
    uint8_t cardShape[SH_CHOICES];
    uint8_t cardColor[SH_CHOICES];
    uint8_t correct;
    uint16_t stars;
    uint16_t streak;
    uint16_t answered;
    uint8_t animTick;
} Game43Memory;

static Game43Memory * const memory = (Game43Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t bestStreak = 0;

static const char * const _shapeName[SH_SHAPES] = {
    "CIRCLE", "SQUARE", "TRIANGLE", "STAR", "HEART", "DIAMOND"
};
static const char * const _colorName[SH_COLORS] = {
    "RED", "BLUE", "GREEN", "YELLOW", "ORANGE", "PURPLE"
};
static const GPU_Color _colorVal[SH_COLORS] = {
    C_RED, C_BLUE, C_GREEN, C_YELLOW, C_ORANGE, C_PURPLE
};

/* card layout: four big picture cards along the bottom */
#define CARD_Y 140
#define CARD_H 84
#define CARD_W 68
static int16_t CardX(uint8_t i) { return (int16_t)(8 + i*77); }

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* pick 4 distinct values out of 6, one of them is the target */
static void PickFour(uint8_t * vals, uint8_t target, uint8_t * correctIdx)
{
    vals[0] = target;
    uint8_t cnt = 1;
    while (cnt < SH_CHOICES)
    {
        uint8_t v = (uint8_t)Random_Range(SH_SHAPES);
        uint8_t dup = 0;
        for (uint8_t j = 0; j < cnt; j++)
            if (vals[j] == v) dup = 1;
        if (!dup)
            vals[cnt++] = v;
    }
    for (uint8_t i = 0; i < SH_CHOICES; i++)
    {
        uint8_t j = (uint8_t)Random_Range(SH_CHOICES);
        uint8_t t = vals[i]; vals[i] = vals[j]; vals[j] = t;
    }
    for (uint8_t i = 0; i < SH_CHOICES; i++)
        if (vals[i] == target)
            *correctIdx = i;
}

static void NewQuestion(void)
{
    /* colour rounds join in once a few shapes have been found */
    memory->mode = (memory->answered >= 4 && Random_Range(2)) ? 1 : 0;
    if (memory->mode == 0)
    {
        memory->target = (uint8_t)Random_Range(SH_SHAPES);
        PickFour(memory->cardShape, memory->target, &memory->correct);
        for (uint8_t i = 0; i < SH_CHOICES; i++)
            memory->cardColor[i] = (uint8_t)Random_Range(SH_COLORS);
    }
    else
    {
        memory->target = (uint8_t)Random_Range(SH_COLORS);
        PickFour(memory->cardColor, memory->target, &memory->correct);
        uint8_t s = (uint8_t)Random_Range(SH_SHAPES);
        for (uint8_t i = 0; i < SH_CHOICES; i++)
            memory->cardShape[i] = s;
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
    memset(memory, 0, sizeof(Game43Memory));
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
            memory->cursor = (uint8_t)((memory->cursor + SH_CHOICES - 1) % SH_CHOICES);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % SH_CHOICES);
        if (keys & KEY_UP)
            Answer(memory->cursor);
        if (tap && ty >= CARD_Y && ty < CARD_Y + CARD_H)
        {
            for (uint8_t i = 0; i < SH_CHOICES; i++)
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

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(20,50,70), RGB(50,100,120), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 64, "SHAPE HUNT");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 100, "look at the shape or colour on top");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 114, "then tap the same one below");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 132, "LEFT/RIGHT + UP works too");
        DrawShape(0, C_RED, 60, 180, 16);
        DrawShape(2, C_GREEN, 120, 180, 16);
        DrawShape(3, C_YELLOW, 180, 180, 16);
        DrawShape(4, C_C2, 240, 180, 16);
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 214, "tap or press a key to play");
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

    /* the big target: a white silhouette, or a colour swatch */
    GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 16, memory->mode ? "FIND THIS COLOUR" : "FIND THIS SHAPE");
    if (memory->mode == 0)
        DrawShape(memory->target, C_WHITE, GPU_X/2, 66, 24);
    else
        DrawShape(0, _colorVal[memory->target], GPU_X/2, 66, 24);

    /* name / feedback line */
    if (memory->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 104, praise[memory->stars % 4]);
    }
    else if (memory->phase == 2)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 104, "TRY AGAIN!");
    }
    else if (memory->mode == 0)
    {
        GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 104, _shapeName[memory->target]);
    }
    else
    {
        GPU_DrawTextOutlined(_colorVal[memory->target], C_BLACK, fb, GPU_X/2, 104, _colorName[memory->target]);
    }

    /* the picture cards */
    for (uint8_t i = 0; i < SH_CHOICES; i++)
    {
        int16_t x = CardX(i);
        GPU_Color bg = RGB(30,60,80);
        if (memory->phase == 1 && i == memory->correct)
            bg = RGB(30,140,60);
        else if (memory->phase == 2 && i == memory->wrongIdx && (memory->phaseTicks & 2))
            bg = RGB(160,50,40);
        GPU_DrawBevelSquare(bg, x, CARD_Y, CARD_W, CARD_H);
        if (i == memory->cursor)
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), CARD_Y-2, CARD_W+4, CARD_H+4);
        DrawShape(memory->cardShape[i], _colorVal[memory->cardColor[i]],
                  (int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, 22);
        if (memory->phase == 1 && i == memory->correct)
            GPU_DrawBurst((int16_t)(x + CARD_W/2), CARD_Y + CARD_H/2, (uint8_t)(25 - memory->phaseTicks), 25, C_YELLOW);
    }
}

static void G_Deinit(void)
{
}

const Callbacks G43_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };