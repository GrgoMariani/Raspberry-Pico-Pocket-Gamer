// FLAPPY BIRD - flap through the gaps in the pipes

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#define NUM_TICKS 30
#define FB_BIRD_X 80
#define FB_GRAVITY 0.34f
#define FB_FLAP -5.4f
#define FB_PIPE_W 46
#define FB_PIPE_SPEED 2.6f
#define FB_PIPE_SPACING 142
#define FB_NUM_PIPES 4
#define FB_GROUND 212
#define FB_GAP 66

typedef struct {
    float x;
    float gapY;       /* centre of the gap */
    uint8_t passed;
} Pipe;

typedef struct {
    float birdY;
    float birdVY;
    Pipe pipes[FB_NUM_PIPES];
    uint32_t score;
    uint8_t state;      /* 0 ready, 1 playing, 2 dead */
    uint8_t wing;
    uint16_t deadTick;
    float scroll;       /* background parallax */
} Game26Memory;

static Game26Memory * const memory = (Game26Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void ResetPipes(void)
{
    for (uint8_t i = 0; i < FB_NUM_PIPES; i++)
    {
        memory->pipes[i].x = GPU_X + 60 + i*FB_PIPE_SPACING;
        memory->pipes[i].gapY = 60.0f + (float)Random_Range(110);
        memory->pipes[i].passed = 0;
    }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game26Memory));
    memory->birdY = 110.0f;
    memory->state = 0;
    ResetPipes();
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void Flap(void)
{
    if (memory->state == 0)
    {
        memory->state = 1;
        memory->birdVY = FB_FLAP;
    }
    else if (memory->state == 1)
    {
        memory->birdVY = FB_FLAP;
    }
}

static void Die(void)
{
    memory->state = 2;
    memory->deadTick = 0;
    if (memory->score > bestScore)
        bestScore = memory->score;
}

static void G_Update(void)
{
    /* flap input */
    if (Keyboard_TakeKey(KEY_UP))
        Flap();
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
        Flap();
    if (memory->state == 2)
    {
        if (memory->deadTick > 30 && (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE))
            G_Init();
    }
    else
    {
        Keyboard_GetPressedKeys();
    }

    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        memory->wing = (uint8_t)((memory->wing + 1) & 7);
        if (memory->state == 0)
        {
            /* gentle hover on the ready screen */
            memory->birdY = 110.0f + 6.0f*sinf((float)mainMemory._lastRenderedTick*0.15f);
            memory->scroll += FB_PIPE_SPEED*0.4f;
        }
        else if (memory->state == 1)
        {
            memory->scroll += FB_PIPE_SPEED;
            memory->birdVY += FB_GRAVITY;
            if (memory->birdVY > 7.0f) memory->birdVY = 7.0f;
            memory->birdY += memory->birdVY;
            if (memory->birdY < 6) { memory->birdY = 6; memory->birdVY = 0; }
            if (memory->birdY > FB_GROUND-8) { memory->birdY = FB_GROUND-8; Die(); }
            /* once dead, stop processing pipes: a pipe passed later in this
               same tick must not add to the score of the finished run */
            for (uint8_t i = 0; i < FB_NUM_PIPES && memory->state == 1; i++)
            {
                Pipe * p = &memory->pipes[i];
                p->x -= FB_PIPE_SPEED;
                if (p->x < -FB_PIPE_W)
                {
                    p->x += FB_NUM_PIPES*FB_PIPE_SPACING;
                    p->gapY = 60.0f + (float)Random_Range(110);
                    p->passed = 0;
                }
                /* scoring */
                if (!p->passed && p->x + FB_PIPE_W < FB_BIRD_X)
                {
                    p->passed = 1;
                    memory->score++;
                }
                /* collision */
                if (FB_BIRD_X + 8 > p->x && FB_BIRD_X - 8 < p->x + FB_PIPE_W)
                {
                    float gap = FB_GAP;
                    if (memory->birdY - 7 < p->gapY - gap/2 || memory->birdY + 7 > p->gapY + gap/2)
                    {
                        Die();
                        break;
                    }
                }
            }
        }
        else if (memory->state == 2)
        {
            memory->deadTick++;
            if (memory->birdY < FB_GROUND-8)
            {
                memory->birdVY += FB_GRAVITY;
                memory->birdY += memory->birdVY;
                if (memory->birdY > FB_GROUND-8) memory->birdY = FB_GROUND-8;
            }
        }
    }
}

static void DrawPipe(const Pipe * p)
{
    float gap = FB_GAP;
    int16_t x = (int16_t)p->x;
    int16_t topEnd = (int16_t)(p->gapY - gap/2);
    int16_t botStart = (int16_t)(p->gapY + gap/2);
    /* top pipe */
    GPU_DrawFilledSquare(RGB(40,160,60), x, 0, FB_PIPE_W, topEnd);
    GPU_DrawFilledSquare(RGB(70,200,90), x, 0, 6, topEnd);
    GPU_DrawFilledSquare(RGB(30,120,45), (int16_t)(x+FB_PIPE_W-6), 0, 6, topEnd);
    GPU_DrawBevelSquare(RGB(50,180,70), (int16_t)(x-3), (int16_t)(topEnd-14), FB_PIPE_W+6, 14);
    /* bottom pipe */
    GPU_DrawFilledSquare(RGB(40,160,60), x, botStart, FB_PIPE_W, (int16_t)(FB_GROUND-botStart));
    GPU_DrawFilledSquare(RGB(70,200,90), x, botStart, 6, (int16_t)(FB_GROUND-botStart));
    GPU_DrawFilledSquare(RGB(30,120,45), (int16_t)(x+FB_PIPE_W-6), botStart, 6, (int16_t)(FB_GROUND-botStart));
    GPU_DrawBevelSquare(RGB(50,180,70), (int16_t)(x-3), botStart, FB_PIPE_W+6, 14);
}

static void DrawBird(void)
{
    int16_t bx = FB_BIRD_X;
    int16_t by = (int16_t)memory->birdY;
    GPU_DrawGlow(C_YELLOW, bx, by, 12);
    GPU_DrawFilledCircle(C_YELLOW, bx, by, 8);
    /* wing */
    int16_t wy = (int16_t)(by + ((memory->wing < 4) ? -2 : 3));
    GPU_DrawFilledCircle(C_ORANGE, (int16_t)(bx-3), wy, 4);
    /* eye */
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(bx+3), (int16_t)(by-3), 3);
    GPU_DrawPixel(C_BLACK, (uint16_t)(bx+4), (uint16_t)(by-3));
    /* beak */
    GPU_DrawFilledTriangle(C_ORANGE, (int16_t)(bx+7), (int16_t)(by-1), (int16_t)(bx+7), (int16_t)(by+3), (int16_t)(bx+13), (int16_t)(by+1));
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    char text[24];
    GPU_ClearFramebuffers();
    /* sky */
    GPU_DrawVerticalGradient(RGB(70,140,220), RGB(150,210,240), 0, 0, GPU_X, FB_GROUND);
    /* parallax clouds */
    for (uint8_t i = 0; i < 4; i++)
    {
        int16_t cx = (int16_t)((GPU_X + 80 - (int32_t)(memory->scroll*0.3f + i*97) % (GPU_X+80)) - 40);
        int16_t cy = (int16_t)(30 + i*28);
        GPU_DrawFilledCircle(C_WHITE, cx, cy, 12);
        GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx+12), cy, 9);
        GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx-12), cy, 9);
    }
    /* distant hills */
    for (uint8_t i = 0; i < 6; i++)
    {
        int16_t hx = (int16_t)((GPU_X + 120 - (int32_t)(memory->scroll*0.5f + i*80) % (GPU_X+120)) - 60);
        GPU_DrawFilledCircle(RGB(90,170,110), hx, FB_GROUND, 34);
    }
    for (uint8_t i = 0; i < FB_NUM_PIPES; i++)
        DrawPipe(&memory->pipes[i]);
    /* ground */
    GPU_DrawFilledSquare(RGB(200,170,90), 0, FB_GROUND, GPU_X, GPU_Y-FB_GROUND);
    GPU_DrawFilledSquare(RGB(120,190,80), 0, FB_GROUND, GPU_X, 5);
    DrawBird();

    /* score */
    sprintf(text, "%u", (unsigned)memory->score);
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fh, GPU_X/2, 16, text);
    sprintf(text, "BEST %u", (unsigned)bestScore);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)strlen(text));

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 90, "FLAPPY BIRD");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 130, "tap or press UP to flap");
    }
    else if (memory->state == 2 && memory->deadTick > 20)
    {
        GPU_DrawPanel(RGB(20,16,8), C_ORANGE, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-40), 180, 80);
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "GAME OVER");
        sprintf(text, "SCORE %u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-6), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+14), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G26_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };