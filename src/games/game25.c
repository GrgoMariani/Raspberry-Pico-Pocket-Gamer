// SIMON - watch the light pattern, then repeat it

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define SIM_MAX 64
#define NUM_TICKS 20
#define SHOW_ON 9
#define SHOW_GAP 4

typedef struct {
    uint8_t seq[SIM_MAX];
    uint8_t len;
    uint8_t pos;
    uint8_t state;        /* 0 ready, 1 showing, 2 input, 3 over */
    uint8_t lit;          /* pad currently lit during playback, 255 = none */
    uint8_t phase;        /* 0 = lit, 1 = gap */
    uint8_t timer;
    uint8_t flash;        /* pad flashed on input, 255 = none */
    uint8_t flashTick;
    uint8_t wrong;        /* pad that was wrong */
    uint32_t score;
} Game25Memory;

static Game25Memory * const memory = (Game25Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static const GPU_Color _padColor[4] = { C_GREEN, C_RED, C_YELLOW, C_BLUE };

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

#define PAD_SIZE 90
#define PAD_GAP 12
#define PAD_LEFT (GPU_X/2 - PAD_SIZE - PAD_GAP/2)
#define PAD_TOP (GPU_Y/2 - PAD_SIZE - PAD_GAP/2)

static void PadRect(uint8_t i, int16_t * x, int16_t * y)
{
    *x = (int16_t)(PAD_LEFT + (i & 1)*(PAD_SIZE + PAD_GAP));
    *y = (int16_t)(PAD_TOP + (i >> 1)*(PAD_SIZE + PAD_GAP));
}

/* playback speeds up as the sequence grows, like the arcade original */
static uint8_t ShowOn(void)
{
    int v = SHOW_ON - (int)memory->len/5;
    return (uint8_t)(v < 3 ? 3 : v);
}

static uint8_t ShowGap(void)
{
    int v = SHOW_GAP - (int)memory->len/10;
    return (uint8_t)(v < 2 ? 2 : v);
}

static void StartShow(void)
{
    memory->state = 1;
    memory->pos = 0;
    memory->phase = 1;
    memory->timer = ShowGap();
    memory->lit = 255;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game25Memory));
    memory->lit = 255;
    memory->flash = 255;
    memory->len = 1;
    memory->seq[0] = (uint8_t)Random_Range(4);
    memory->state = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void NextRound(void)
{
    if (memory->len < SIM_MAX)
    {
        memory->seq[memory->len] = (uint8_t)Random_Range(4);
        memory->len++;
    }
    StartShow();
}

static void GameOver(void)
{
    memory->state = 3;
    if (memory->score > bestScore)
        bestScore = memory->score;
    mainMemory._gameState = GS_DONE;
}

/* the player pressed a pad */
static void PressPad(uint8_t pad)
{
    if (memory->state != 2)
        return;
    memory->flash = pad;
    memory->flashTick = 5;
    if (pad == memory->seq[memory->pos])
    {
        memory->pos++;
        if (memory->pos >= memory->len)
        {
            memory->score = memory->len;      /* completed this many steps */
            NextRound();
        }
    }
    else
    {
        memory->wrong = pad;
        GameOver();
    }
}

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        /* the input flash decays on game ticks, not main loop iterations, so
           it stays visible for ~250ms instead of a few microseconds */
        if (memory->flashTick)
        {
            memory->flashTick--;
            if (!memory->flashTick)
                memory->flash = 255;
        }
        if (memory->state == 0)
        {
            /* a short pause, then play the first pattern */
            memory->timer++;
            if (memory->timer > 15)
                StartShow();
        }
        else if (memory->state == 1)
        {
            if (memory->timer)
                memory->timer--;
            else
            {
                if (memory->phase == 0)
                {
                    memory->lit = 255;
                    memory->phase = 1;
                    memory->timer = ShowGap();
                }
                else
                {
                    if (memory->pos >= memory->len)
                    {
                        memory->state = 2;
                        memory->pos = 0;
                        memory->lit = 255;
                    }
                    else
                    {
                        memory->lit = memory->seq[memory->pos];
                        memory->pos++;
                        memory->phase = 0;
                        memory->timer = ShowOn();
                    }
                }
            }
        }
    }
    /* input: keys map to the four pads, touch taps a pad */
    if (memory->state == 2)
    {
        KeyPressedEnum keys = Keyboard_GetPressedKeys();
        if (keys & KEY_UP)    PressPad(0);
        if (keys & KEY_RIGHT) PressPad(1);
        if (keys & KEY_DOWN)  PressPad(2);
        if (keys & KEY_LEFT)  PressPad(3);
        uint16_t tx, ty;
        if (Touch_TakeTap(&tx, &ty))
        {
            for (uint8_t i = 0; i < 4; i++)
            {
                int16_t px, py;
                PadRect(i, &px, &py);
                if (tx >= px && tx < px+PAD_SIZE && ty >= py && ty < py+PAD_SIZE)
                    PressPad(i);
            }
        }
    }
    else if (memory->state == 3)
    {
        if (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
    }
    else
    {
        Keyboard_GetPressedKeys();
        Touch_ClearTap();
    }
}

static void DrawPad(uint8_t i)
{
    int16_t x, y;
    PadRect(i, &x, &y);
    uint8_t active = (memory->lit == i) || (memory->flash == i);
    GPU_Color c = _padColor[i];
    if (memory->state == 3 && memory->wrong == i)
        c = C_WHITE;
    if (active)
    {
        GPU_DrawGlow(c, (int16_t)(x+PAD_SIZE/2), (int16_t)(y+PAD_SIZE/2), (int16_t)(PAD_SIZE/2));
        GPU_DrawBevelSquare(c, x, y, PAD_SIZE, PAD_SIZE);
    }
    else
    {
        GPU_DrawBevelSquare(GPU_ShadeColor(c, 90), x, y, PAD_SIZE, PAD_SIZE);
    }
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(8,8,24), C_BLACK, 0, 0, GPU_X, GPU_Y);

    for (uint8_t i = 0; i < 4; i++)
        DrawPad(i);

    /* centre hub with the score */
    GPU_DrawFilledCircle(RGB(16,18,40), GPU_X/2, GPU_Y/2, 34);
    GPU_DrawEmptyCircle(C_GRAY_DARK, GPU_X/2, GPU_Y/2, 34);
    sprintf(text, "%u", (unsigned)memory->score);
    GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, (int16_t)(GPU_Y/2-14), text);
    GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+6), "SIMON");

    /* header */
    sprintf(text, "BEST %u", (unsigned)bestScore);
    GPU_DrawText(C_GRAY_LIGHT, fm, 6, 4, text, (uint16_t)strlen(text));
    const char * status = "";
    if (memory->state == 1) status = "WATCH";
    else if (memory->state == 2) status = "YOUR TURN";
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 4, status);

    if (memory->state == 3)
    {
        GPU_DrawPanel(RGB(16,8,8), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28), "WRONG!");
        sprintf(text, "SCORE %u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G25_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };