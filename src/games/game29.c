// WHACK-A-MOLE - tap the moles before they duck back down

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 20
#define WM_HOLES 9
#define WM_TIME (60*NUM_TICKS)

typedef struct {
    uint8_t up;          /* mole is out of the hole */
    uint8_t type;        /* 0 normal, 1 golden, 2 bomb */
    uint8_t whacked;     /* just hit, playing the bonk animation */
    uint16_t timer;      /* ticks left before it ducks / animation ends */
} Mole;

typedef struct {
    Mole mole[WM_HOLES];
    uint32_t score;
    uint16_t timeLeft;
    uint16_t spawnIn;
    uint8_t combo;
    uint8_t cursor;
    uint8_t state;       /* 0 ready, 1 playing, 2 over */
    uint8_t flash;       /* screen flash on a bomb */
    int8_t popup;        /* floating score text */
    uint16_t popupTick;
    uint8_t popupHole;
} Game29Memory;

static Game29Memory * const memory = (Game29Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static const int16_t _holeX[3] = { 70, 160, 250 };
static const int16_t _holeY[3] = { 78, 132, 186 };

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game29Memory));
    memory->timeLeft = WM_TIME;
    memory->spawnIn = 10;
    memory->state = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void Whack(uint8_t h)
{
    if (memory->state != 1 || h >= WM_HOLES)
        return;
    Mole * m = &memory->mole[h];
    if (!m->up || m->whacked)
        return;
    m->whacked = 1;
    m->timer = 8;
    memory->popupHole = h;
    memory->popupTick = 16;
    if (m->type == 2)
    {
        /* bomb: penalty and the combo breaks */
        memory->score = (memory->score > 30) ? memory->score - 30 : 0;
        memory->combo = 0;
        memory->flash = 8;
        memory->popup = -30;
    }
    else
    {
        memory->combo++;
        int16_t pts = (m->type == 1) ? 50 : 10;
        pts = (int16_t)(pts + (memory->combo > 1 ? memory->combo*2 : 0));
        memory->score += (uint32_t)pts;
        memory->popup = pts;
    }
}

static void Tick(void)
{
    if (memory->state != 1)
        return;
    if (memory->flash)
        memory->flash--;
    if (memory->popupTick)
        memory->popupTick--;
    if (memory->timeLeft)
        memory->timeLeft--;
    if (memory->timeLeft == 0)
    {
        memory->state = 2;
        if (memory->score > bestScore)
            bestScore = memory->score;
        mainMemory._gameState = GS_DONE;
        return;
    }
    /* spawn moles, faster as the score climbs */
    if (memory->spawnIn)
        memory->spawnIn--;
    else
    {
        uint8_t hidden[WM_HOLES];
        uint8_t n = 0;
        for (uint8_t i = 0; i < WM_HOLES; i++)
            if (!memory->mole[i].up)
                hidden[n++] = i;
        if (n)
        {
            uint8_t h = hidden[Random_Range(n)];
            Mole * m = &memory->mole[h];
            m->up = 1;
            m->whacked = 0;
            uint32_t r = Random_Range(10);
            m->type = (r < 6) ? 0 : (r < 8 ? 1 : 2);
            m->timer = (uint16_t)(34 + Random_Range(20));
        }
        /* spawn faster as the score climbs, but never let the subtraction
           underflow: at score >= 2400 the old unsigned gap wrapped to ~65535
           ticks and no mole ever appeared again */
        uint32_t faster = memory->score/150;
        if (faster > 10) faster = 10;
        uint16_t gap = (uint16_t)(16 - faster);
        if (gap < 6) gap = 6;
        memory->spawnIn = gap;
    }
    /* mole lifetimes */
    for (uint8_t i = 0; i < WM_HOLES; i++)
    {
        Mole * m = &memory->mole[i];
        if (!m->up)
            continue;
        if (m->timer)
            m->timer--;
        if (m->timer == 0)
        {
            if (!m->whacked && m->type != 2)
                memory->combo = 0;      /* a missed mole breaks the combo */
            m->up = 0;
            m->whacked = 0;
        }
    }
}

static void G_Update(void)
{
    if (memory->state == 0)
    {
        if (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE)
            memory->state = 1;
        else
            Touch_ClearTap();
    }
    else if (memory->state == 2)
    {
        if (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
    }
    else
    {
        /* keyboard: arrows move the cursor, UP+DOWN together whacks */
        KeyPressedEnum keys = Keyboard_GetPressedKeys();
        uint8_t cx = memory->cursor % 3, cy = memory->cursor / 3;
        if ((keys & KEY_UP) && (keys & KEY_DOWN))
            Whack(memory->cursor);
        else
        {
            if (keys & KEY_LEFT)  cx = (uint8_t)((cx + 2) % 3);
            if (keys & KEY_RIGHT) cx = (uint8_t)((cx + 1) % 3);
            if (keys & KEY_UP)    cy = (uint8_t)((cy + 2) % 3);
            if (keys & KEY_DOWN)  cy = (uint8_t)((cy + 1) % 3);
            memory->cursor = (uint8_t)(cy*3 + cx);
        }
        /* touch: tap a hole to whack it */
        uint16_t tx, ty;
        if (Touch_TakeTap(&tx, &ty))
        {
            for (uint8_t i = 0; i < WM_HOLES; i++)
            {
                int32_t dx = (int32_t)tx - _holeX[i%3];
                int32_t dy = (int32_t)ty - _holeY[i/3];
                if (dx*dx + dy*dy < 34*34)
                {
                    memory->cursor = i;
                    Whack(i);
                    break;
                }
            }
        }
    }
    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

static void DrawHole(uint8_t i)
{
    int16_t cx = _holeX[i%3], cy = _holeY[i/3];
    Mole * m = &memory->mole[i];
    /* the hole */
    GPU_DrawFilledCircle(RGB(30,22,16), cx, (int16_t)(cy+8), 26);
    GPU_DrawEmptyCircle(RGB(60,44,30), cx, (int16_t)(cy+8), 26);
    if (m->up)
    {
        int16_t my = m->whacked ? (int16_t)(cy+6) : (int16_t)(cy-6);
        GPU_Color body = (m->type == 1) ? C_YELLOW : (m->type == 2) ? C_GRAY_DARK : RGB(150,100,60);
        if (!m->whacked)
            GPU_DrawGlow(body, cx, my, 20);
        GPU_DrawFilledCircle(body, cx, my, 16);
        /* ears */
        GPU_DrawFilledCircle(body, (int16_t)(cx-10), (int16_t)(my-12), 5);
        GPU_DrawFilledCircle(body, (int16_t)(cx+10), (int16_t)(my-12), 5);
        if (m->type == 2)
        {
            /* bomb: a fuse and a spark */
            GPU_DrawLine(C_GRAY_LIGHT, cx, (int16_t)(my-16), (int16_t)(cx+6), (int16_t)(my-22));
            GPU_DrawGlow(C_ORANGE, (int16_t)(cx+6), (int16_t)(my-22), 5);
            GPU_DrawFilledCircle(C_RED, cx, my, 6);
        }
        else
        {
            /* face */
            GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx-5), (int16_t)(my-3), 3);
            GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx+5), (int16_t)(my-3), 3);
            GPU_DrawPixel(C_BLACK, (uint16_t)(cx-5), (uint16_t)(my-3));
            GPU_DrawPixel(C_BLACK, (uint16_t)(cx+5), (uint16_t)(my-3));
            GPU_DrawFilledCircle(RGB(220,150,120), cx, (int16_t)(my+4), 4);
            if (m->whacked)
            {
                GPU_DrawTextOutlined(C_WHITE, C_BLACK, &fontDescription[0], cx, (int16_t)(my-24), "BONK");
            }
        }
    }
    /* keyboard cursor */
    if (memory->state == 1 && memory->cursor == i)
        GPU_DrawEmptySquare(C_YELLOW, (int16_t)(cx-30), (int16_t)(cy-30), 60, 60);
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(30,80,40), RGB(16,44,24), 0, 0, GPU_X, GPU_Y);
    if (memory->flash)
        GPU_DrawFilledSquare(GPU_ShadeColor(C_RED, 90), 0, 0, GPU_X, GPU_Y);

    for (uint8_t i = 0; i < WM_HOLES; i++)
        DrawHole(i);

    /* floating score popup */
    if (memory->popupTick && memory->state == 1)
    {
        int16_t hx = _holeX[memory->popupHole%3];
        int16_t hy = (int16_t)(_holeY[memory->popupHole/3] - 34 - (16 - memory->popupTick));
        sprintf(text, "%+d", (int)memory->popup);
        GPU_DrawTextOutlined(memory->popup > 0 ? C_C3 : C_RED, C_BLACK, fm, hx, hy, text);
    }

    /* HUD */
    GPU_DrawPanel(RGB(8,20,10), C_GREEN, 0, 0, GPU_X, 22);
    sprintf(text, "SCORE %u", (unsigned)memory->score);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)strlen(text));
    sprintf(text, "TIME %u", (unsigned)(memory->timeLeft/NUM_TICKS));
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 4, text);
    if (memory->combo >= 2)
    {
        sprintf(text, "x%u", (unsigned)memory->combo);
        GPU_DrawText(C_C3, fm, (int16_t)(GPU_X-40), 4, text, (uint16_t)strlen(text));
    }

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 100, "WHACK-A-MOLE");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 130, "tap moles, avoid bombs");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fm, GPU_X/2, 146, "tap or press a key to start");
    }
    else if (memory->state == 2)
    {
        GPU_DrawPanel(RGB(16,24,16), C_GREEN, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-40), 180, 80);
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "TIME UP");
        sprintf(text, "SCORE %u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-6), text);
        sprintf(text, "BEST %u", (unsigned)bestScore);
        GPU_DrawTextCentered(C_GRAY_LIGHT, fm, GPU_X/2, (int16_t)(GPU_Y/2+10), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+26), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G29_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };