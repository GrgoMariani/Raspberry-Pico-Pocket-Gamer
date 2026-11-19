// SPOT THE DIFF - finding differences for ages 4-7: two pictures look almost
// the same, but a few details differ (a colour, a shape, a size, or something
// is missing). Tap a difference in EITHER picture to circle it in both. Find
// them all for a star and the next picture. One difference at first, then two
// and three. After three wrong taps a gentle hint pulses around one of the
// remaining differences. No timer, no losing.

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define SP_COLS 3
#define SP_ROWS 3
#define SP_SLOTS (SP_COLS*SP_ROWS)
#define SP_MAX_DIFF 3
#define SP_TYPES 8
#define SP_COLORS 6

typedef struct {
    uint8_t type;                /* sun, cloud, tree, flower, star, house... */
    uint8_t color;
    uint8_t big;
    uint8_t present;
} SpotObj;

typedef struct {
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t phase;               /* 0 hunting, 1 all-found celebration */
    uint8_t phaseTicks;
    SpotObj left[SP_SLOTS];      /* the original picture */
    SpotObj right[SP_SLOTS];     /* the slightly different copy */
    uint8_t diffSlot[SP_MAX_DIFF];
    uint8_t found[SP_MAX_DIFF];
    uint8_t numDiffs;
    uint8_t foundCount;
    uint8_t cursor;              /* keyboard slot cursor (left picture) */
    uint8_t wrongSlot;
    uint8_t wrongTicks;
    uint8_t wrongCount;
    uint8_t hintSlot;
    uint8_t hintTicks;
    uint8_t praiseTicks;
    uint16_t stars;
    uint16_t rounds;
    uint8_t animTick;
} Game49Memory;

static Game49Memory * const memory = (Game49Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static const GPU_Color _colorVal[SP_COLORS] = {
    C_RED, C_BLUE, C_GREEN, C_YELLOW, C_ORANGE, C_PURPLE
};

/* two 150x150 pictures side by side, 3x3 cells of 50px */
#define PIC_Y 30
#define PIC_H 150
#define CELL 50
static int16_t PicX(uint8_t side) { return (int16_t)(side ? 164 : 6); }

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void NewScene(void)
{
    /* gentle ramp: one difference, then two, then three */
    memory->numDiffs = (uint8_t)(memory->rounds < 3 ? 1 : (memory->rounds < 8 ? 2 : 3));

    for (uint8_t i = 0; i < SP_SLOTS; i++)
    {
        memory->left[i].type = (uint8_t)Random_Range(SP_TYPES);
        memory->left[i].color = (uint8_t)Random_Range(SP_COLORS);
        memory->left[i].big = (uint8_t)Random_Range(2);
        memory->left[i].present = 1;
        memory->right[i] = memory->left[i];
    }
    /* pick distinct slots and change one detail in the right picture */
    for (uint8_t d = 0; d < memory->numDiffs; d++)
    {
        uint8_t slot;
        uint8_t dup;
        do
        {
            dup = 0;
            slot = (uint8_t)Random_Range(SP_SLOTS);
            for (uint8_t j = 0; j < d; j++)
                if (memory->diffSlot[j] == slot) dup = 1;
        } while (dup);
        memory->diffSlot[d] = slot;
        memory->found[d] = 0;
        SpotObj * o = &memory->right[slot];
        uint8_t kind = (uint8_t)Random_Range(4);
        if (kind == 0)          /* another colour */
            o->color = (uint8_t)((o->color + 1 + Random_Range(SP_COLORS - 1)) % SP_COLORS);
        else if (kind == 1)     /* another shape */
            o->type = (uint8_t)((o->type + 1 + Random_Range(SP_TYPES - 1)) % SP_TYPES);
        else if (kind == 2)     /* gone! */
            o->present = 0;
        else                    /* another size */
            o->big = (uint8_t)(1 - o->big);
    }
    memory->foundCount = 0;
    memory->phase = 0;
    memory->cursor = 4;
    memory->wrongTicks = 0;
    memory->wrongCount = 0;
    memory->hintTicks = 0;
    memory->praiseTicks = 0;
}

static void CheckSlot(uint8_t slot)
{
    if (memory->state != 1 || memory->phase != 0 || slot >= SP_SLOTS)
        return;
    for (uint8_t d = 0; d < memory->numDiffs; d++)
    {
        if (memory->diffSlot[d] == slot && !memory->found[d])
        {
            memory->found[d] = 1;
            memory->foundCount++;
            memory->stars++;
            memory->praiseTicks = 20;
            memory->hintTicks = 0;
            if (memory->foundCount == memory->numDiffs)
            {
                memory->phase = 1;
                memory->phaseTicks = 45;
            }
            return;
        }
    }
    /* not a difference (or already found): gentle flash, no penalty */
    memory->wrongSlot = slot;
    memory->wrongTicks = 12;
    memory->wrongCount++;
    if ((memory->wrongCount % 3) == 0)
    {
        /* a little help: pulse around one remaining difference */
        for (uint8_t d = 0; d < memory->numDiffs; d++)
            if (!memory->found[d])
            {
                memory->hintSlot = memory->diffSlot[d];
                memory->hintTicks = 40;
                break;
            }
    }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game49Memory));
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void Tick(void)
{
    memory->animTick++;
    if (memory->state != 1)
        return;
    if (memory->wrongTicks)
        memory->wrongTicks--;
    if (memory->hintTicks)
        memory->hintTicks--;
    if (memory->praiseTicks)
        memory->praiseTicks--;
    if (memory->phase == 1)
    {
        if (memory->phaseTicks)
            memory->phaseTicks--;
        if (memory->phaseTicks == 0)
        {
            memory->rounds++;
            NewScene();
        }
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
            NewScene();
        }
    }
    else if (memory->phase == 0)
    {
        if (keys & KEY_LEFT)
            memory->cursor = (uint8_t)((memory->cursor + SP_SLOTS - 1) % SP_SLOTS);
        if (keys & KEY_RIGHT)
            memory->cursor = (uint8_t)((memory->cursor + 1) % SP_SLOTS);
        if (keys & KEY_UP)
            CheckSlot(memory->cursor);
        if (tap && ty >= PIC_Y && ty < PIC_Y + PIC_H)
        {
            /* a difference counts in either picture: map the tap to a slot */
            int16_t rel = (int16_t)tx - PicX(0);
            if (rel < 0 || rel >= PIC_H)
                rel = (int16_t)tx - PicX(1);
            if (rel >= 0 && rel < PIC_H)
            {
                uint8_t slot = (uint8_t)(((ty - PIC_Y)/CELL)*SP_COLS + rel/CELL);
                memory->cursor = slot;
                CheckSlot(slot);
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

static void DrawObj(uint8_t type, GPU_Color col, uint8_t big, int16_t cx, int16_t cy)
{
    int16_t r = big ? 12 : 8;
    switch (type)
    {
        case 0:     /* sun with rays */
            GPU_DrawFilledCircle(col, cx, cy, r);
            GPU_DrawLine(col, (int16_t)(cx-r-4), cy, (int16_t)(cx-r-1), cy);
            GPU_DrawLine(col, (int16_t)(cx+r+1), cy, (int16_t)(cx+r+4), cy);
            GPU_DrawLine(col, cx, (int16_t)(cy-r-4), cx, (int16_t)(cy-r-1));
            GPU_DrawLine(col, cx, (int16_t)(cy+r+1), cx, (int16_t)(cy+r+4));
            break;
        case 1:     /* cloud */
            GPU_DrawFilledCircle(col, (int16_t)(cx-r/2), cy, (int16_t)(r*3/4));
            GPU_DrawFilledCircle(col, (int16_t)(cx+r/2), cy, (int16_t)(r*3/4));
            GPU_DrawFilledCircle(col, cx, (int16_t)(cy-r/3), (int16_t)(r*3/4));
            break;
        case 2:     /* tree */
            GPU_DrawFilledSquare(RGB(110,70,30), (int16_t)(cx-2), cy, 4, (int16_t)(r+2));
            GPU_DrawFilledCircle(col, cx, (int16_t)(cy-r/2), r);
            break;
        case 3:     /* flower */
            GPU_DrawLine(C_GREEN, cx, (int16_t)(cy+r), cx, cy);
            GPU_DrawFilledCircle(col, (int16_t)(cx-r/2), (int16_t)(cy-r/3), (int16_t)(r/3+1));
            GPU_DrawFilledCircle(col, (int16_t)(cx+r/2), (int16_t)(cy-r/3), (int16_t)(r/3+1));
            GPU_DrawFilledCircle(col, cx, (int16_t)(cy-r), (int16_t)(r/3+1));
            GPU_DrawFilledCircle(col, cx, (int16_t)(cy+r/3-1), (int16_t)(r/3+1));
            GPU_DrawFilledCircle(C_YELLOW, cx, (int16_t)(cy-r/3), (int16_t)(r/4+1));
            break;
        case 4:     /* star */
            GPU_DrawFilledTriangle(col, (int16_t)(cx-r), (int16_t)(cy+r*2/3), (int16_t)(cx+r), (int16_t)(cy+r*2/3), cx, (int16_t)(cy-r));
            GPU_DrawFilledTriangle(col, (int16_t)(cx-r), (int16_t)(cy-r*2/3), (int16_t)(cx+r), (int16_t)(cy-r*2/3), cx, (int16_t)(cy+r));
            break;
        case 5:     /* house */
            GPU_DrawFilledSquare(col, (int16_t)(cx-r*3/4), (int16_t)(cy-r/4), (int16_t)(r*3/2), (int16_t)(r+2));
            GPU_DrawFilledTriangle(GPU_ShadeColor(col, 150), (int16_t)(cx-r), (int16_t)(cy-r/4), (int16_t)(cx+r), (int16_t)(cy-r/4), cx, (int16_t)(cy-r));
            GPU_DrawFilledSquare(RGB(60,40,30), (int16_t)(cx-2), (int16_t)(cy+r/4), 4, (int16_t)(r*3/4+2));
            break;
        case 6:     /* balloon */
            GPU_DrawFilledCircle(col, cx, (int16_t)(cy-r/3), (int16_t)(r*3/4));
            GPU_DrawLine(C_GRAY_DARK, cx, (int16_t)(cy+r/2), cx, (int16_t)(cy+r));
            GPU_DrawPixel(C_WHITE, (uint16_t)(cx-2), (uint16_t)(cy-r/2));
            break;
        default:    /* bird */
            GPU_DrawFilledCircle(col, cx, cy, (int16_t)(r*2/3));
            GPU_DrawFilledTriangle(col, (int16_t)(cx-r/2), cy, (int16_t)(cx+r/3), cy, (int16_t)(cx-r/4), (int16_t)(cy-r));
            GPU_DrawFilledTriangle(C_ORANGE, (int16_t)(cx+r*2/3), (int16_t)(cy-2), (int16_t)(cx+r*2/3), (int16_t)(cy+2), (int16_t)(cx+r), cy);
            GPU_DrawPixel(C_WHITE, (uint16_t)(cx+2), (uint16_t)(cy-2));
            break;
    }
}

static void DrawPicture(uint8_t side)
{
    int16_t x0 = PicX(side);
    const SpotObj * objs = side ? memory->right : memory->left;
    /* sky + grass */
    GPU_DrawVerticalGradient(RGB(120,180,230), RGB(180,220,240), x0, PIC_Y, PIC_H, 100);
    GPU_DrawFilledSquare(RGB(110,180,90), x0, PIC_Y + 100, PIC_H, 50);
    GPU_DrawFilledSquare(RGB(90,160,75), x0, PIC_Y + 100, PIC_H, 3);
    for (uint8_t i = 0; i < SP_SLOTS; i++)
    {
        if (!objs[i].present)
            continue;
        int16_t cx = (int16_t)(x0 + (i % SP_COLS)*CELL + CELL/2);
        int16_t cy = (int16_t)(PIC_Y + (i / SP_COLS)*CELL + CELL/2);
        DrawObj(objs[i].type, _colorVal[objs[i].color], objs[i].big, cx, cy);
    }
    GPU_DrawEmptySquare(C_WHITE, (int16_t)(x0-1), PIC_Y-1, PIC_H+2, PIC_H+2);
}

static void RingBoth(uint8_t slot, GPU_Color col)
{
    int16_t cx = (int16_t)((slot % SP_COLS)*CELL + CELL/2);
    int16_t cy = (int16_t)(PIC_Y + (slot / SP_COLS)*CELL + CELL/2);
    GPU_DrawEmptyCircle(col, (int16_t)(PicX(0) + cx), cy, 22);
    GPU_DrawEmptyCircle(col, (int16_t)(PicX(1) + cx), cy, 22);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[28];

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(25,30,60), RGB(50,60,100), 0, 0, GPU_X, GPU_Y);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 20, "SPOT THE DIFF");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 48, "two pictures look almost the same");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 62, "tap what is different in either one");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 80, "LEFT/RIGHT + UP works too");
        /* a little demo: same house, different colour, already circled */
        GPU_DrawVerticalGradient(RGB(120,180,230), RGB(180,220,240), 30, 100, 110, 70);
        GPU_DrawFilledSquare(RGB(110,180,90), 30, 170, 110, 20);
        GPU_DrawVerticalGradient(RGB(120,180,230), RGB(180,220,240), 180, 100, 110, 70);
        GPU_DrawFilledSquare(RGB(110,180,90), 180, 170, 110, 20);
        DrawObj(0, C_YELLOW, 0, 55, 120);
        DrawObj(0, C_YELLOW, 0, 205, 120);
        DrawObj(5, C_RED, 1, 105, 150);
        DrawObj(5, C_BLUE, 1, 255, 150);
        GPU_DrawEmptyCircle(C_GREEN, 105, 150, 22);
        GPU_DrawEmptyCircle(C_GREEN, 255, 150, 22);
        GPU_DrawEmptySquare(C_WHITE, 29, 99, 112, 92);
        GPU_DrawEmptySquare(C_WHITE, 179, 99, 112, 92);
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 210, "tap or press a key to play");
        return;
    }

    /* prompt + stars */
    if (memory->numDiffs == 1)
        GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, "FIND THE DIFFERENCE");
    else
    {
        sprintf(text, "FIND %u DIFFERENCES", (unsigned)memory->numDiffs);
        GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 6, text);
    }
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));

    DrawPicture(0);
    DrawPicture(1);

    /* found differences circled in green in BOTH pictures */
    for (uint8_t d = 0; d < memory->numDiffs; d++)
        if (memory->found[d])
            RingBoth(memory->diffSlot[d], C_GREEN);
    /* the gentle hint pulses around one remaining difference */
    if (memory->hintTicks && (memory->hintTicks & 4))
        RingBoth(memory->hintSlot, C_YELLOW);
    /* a wrong tap flashes red briefly */
    if (memory->wrongTicks && (memory->wrongTicks & 2))
        RingBoth(memory->wrongSlot, C_RED);
    /* keyboard cursor on the left picture */
    if (memory->phase == 0)
    {
        int16_t cx = (int16_t)(PicX(0) + (memory->cursor % SP_COLS)*CELL);
        int16_t cy = (int16_t)(PIC_Y + (memory->cursor / SP_COLS)*CELL);
        GPU_DrawEmptySquare(C_YELLOW, cx, cy, CELL, CELL);
    }

    /* feedback + progress */
    if (memory->phase == 1)
    {
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 190, "WELL DONE!");
        GPU_DrawBurst(GPU_X/2, 199, (uint8_t)(45 - memory->phaseTicks), 45, C_YELLOW);
    }
    else if (memory->praiseTicks)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 190, praise[memory->stars % 4]);
    }
    else if (memory->wrongTicks)
    {
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 190, "TRY AGAIN!");
    }
    sprintf(text, "FOUND %u / %u", (unsigned)memory->foundCount, (unsigned)memory->numDiffs);
    GPU_DrawText(C_WHITE, fm, 6, 222, text, (uint16_t)strlen(text));
    sprintf(text, "PICTURE %u", (unsigned)(memory->rounds + 1));
    GPU_DrawText(C_GRAY_LIGHT, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 224, text, (uint16_t)strlen(text));
}

static void G_Deinit(void)
{
}

const Callbacks G49_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };