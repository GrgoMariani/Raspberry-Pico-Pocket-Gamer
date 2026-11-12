// DOODLE JUMP - bounce up the platforms and climb as high as you can

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 30
#define DJ_GRAVITY 0.3f
#define DJ_JUMP 7.6f
#define DJ_MOVE 3.6f
#define DJ_PLAT_W 56
#define DJ_NUM_PLATS 16

typedef struct {
    float x, h, w, vx;
    uint8_t active;
    uint8_t moving;
} Platform;

typedef struct {
    Platform plats[DJ_NUM_PLATS];
    float charX, charY, vy;
    float cameraY, maxHeight, genH;
    uint32_t score;
    uint8_t state;       /* 0 ready, 1 playing, 2 over */
    uint32_t overTime;   /* when the run ended (real-time game-over delay) */
    uint8_t face;        /* animation frame */
} Game30Memory;

static Game30Memory * const memory = (Game30Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static int16_t ScreenY(float h)
{
    return (int16_t)(GPU_Y - (h - memory->cameraY));
}

static void GenPlatforms(void)
{
    while (memory->genH < memory->cameraY + 300)
    {
        int slot = -1;
        for (uint8_t i = 0; i < DJ_NUM_PLATS; i++)
            if (!memory->plats[i].active) { slot = i; break; }
        if (slot < 0)
            break;
        /* gaps widen a little as you climb */
        float gap = 42.0f + (float)Random_Range(30);
        if (memory->genH > 800) gap += 12.0f;
        memory->genH += gap;
        Platform * p = &memory->plats[slot];
        p->active = 1;
        p->h = memory->genH;
        p->w = DJ_PLAT_W;
        p->x = (float)Random_Range((uint32_t)(GPU_X - DJ_PLAT_W));
        p->moving = (uint8_t)(memory->genH > 300 && Random_Range(6) == 0);
        p->vx = p->moving ? ((Random_Range(2)) ? 1.2f : -1.2f) : 0.0f;
    }
    for (uint8_t i = 0; i < DJ_NUM_PLATS; i++)
        if (memory->plats[i].active && memory->plats[i].h < memory->cameraY - 40)
            memory->plats[i].active = 0;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game30Memory));
    memory->charX = 160;
    memory->charY = 20;
    memory->cameraY = 0;
    memory->genH = 20;
    /* a guaranteed starting platform under the character */
    memory->plats[0].active = 1;
    memory->plats[0].h = 20;
    memory->plats[0].w = DJ_PLAT_W;
    memory->plats[0].x = 160 - DJ_PLAT_W/2;
    GenPlatforms();
    memory->state = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void Tick(void)
{
    if (memory->state != 1)
        return;
    memory->face = (uint8_t)((memory->face + 1) & 7);
    /* horizontal control (touch: left/right half, keys: left/right) */
    KeyPressedEnum held = Keyboard_GetHeldKeys();
    int8_t dir = 0;
    if (held & KEY_LEFT) dir = -1;
    if (held & KEY_RIGHT) dir = 1;
    if (mainMemory.touchPressed)
        dir = (mainMemory.touch_X < GPU_X/2) ? -1 : 1;
    memory->charX += dir*DJ_MOVE;
    if (memory->charX < -10) memory->charX += GPU_X + 20;
    if (memory->charX > GPU_X + 10) memory->charX -= GPU_X + 20;

    /* vertical physics */
    float prevY = memory->charY;
    memory->vy -= DJ_GRAVITY;
    memory->charY += memory->vy;
    if (memory->vy < 0)
    {
        for (uint8_t i = 0; i < DJ_NUM_PLATS; i++)
        {
            Platform * p = &memory->plats[i];
            if (!p->active)
                continue;
            if (prevY >= p->h && memory->charY <= p->h &&
                memory->charX + 9 > p->x && memory->charX - 9 < p->x + p->w)
            {
                memory->charY = p->h;
                memory->vy = DJ_JUMP;
                break;
            }
        }
    }
    /* moving platforms */
    for (uint8_t i = 0; i < DJ_NUM_PLATS; i++)
    {
        Platform * p = &memory->plats[i];
        if (!p->active || !p->moving)
            continue;
        p->x += p->vx;
        if (p->x < 0) { p->x = 0; p->vx = -p->vx; }
        if (p->x > GPU_X - p->w) { p->x = GPU_X - p->w; p->vx = -p->vx; }
    }
    /* camera follows upward only */
    if (memory->charY - 120 > memory->cameraY)
        memory->cameraY = memory->charY - 120;
    if (memory->charY > memory->maxHeight)
        memory->maxHeight = memory->charY;
    memory->score = (uint32_t)(memory->maxHeight/10);
    GenPlatforms();
    /* fell below the screen */
    if (memory->charY < memory->cameraY - 12)
    {
        memory->state = 2;
        memory->overTime = IF_GetCurrentTime();
        if (memory->score > bestScore)
            bestScore = memory->score;
        mainMemory._gameState = GS_DONE;
    }
}

static void G_Update(void)
{
    if (memory->state == 0)
    {
        if (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE)
        {
            memory->state = 1;
            memory->vy = DJ_JUMP;
        }
        else
            Touch_ClearTap();
    }
    else if (memory->state == 2)
    {
        /* give the player ~0.7s to read the score before a tap restarts (the
           old counter ran on main loop iterations and restarted almost instantly) */
        if (IF_GetCurrentTime() - memory->overTime > 700000 && (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE))
            G_Init();
    }
    else
    {
        Keyboard_GetPressedKeys();
        Touch_ClearTap();
    }
    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

static void DrawChar(void)
{
    int16_t cx = (int16_t)memory->charX;
    int16_t cy = ScreenY(memory->charY);
    GPU_DrawGlow(C_GREEN, cx, (int16_t)(cy-8), 14);
    GPU_DrawFilledCircle(C_GREEN, cx, (int16_t)(cy-8), 10);
    /* eyes look in the direction of travel */
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx-4), (int16_t)(cy-12), 3);
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx+4), (int16_t)(cy-12), 3);
    GPU_DrawPixel(C_BLACK, (uint16_t)(cx-4), (uint16_t)(cy-12));
    GPU_DrawPixel(C_BLACK, (uint16_t)(cx+4), (uint16_t)(cy-12));
    /* little feet */
    int16_t fy = (int16_t)(cy - ((memory->face < 4) ? 0 : 2));
    GPU_DrawFilledSquare(C_GREEN, (int16_t)(cx-7), fy, 5, 3);
    GPU_DrawFilledSquare(C_GREEN, (int16_t)(cx+2), fy, 5, 3);
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(40,60,120), RGB(90,120,180), 0, 0, GPU_X, GPU_Y);
    /* parallax clouds */
    for (uint8_t i = 0; i < 5; i++)
    {
        int16_t cx = (int16_t)((i*73 + 30) % GPU_X);
        int16_t cy = (int16_t)((GPU_Y + 60 - (int32_t)(memory->cameraY*0.3f + i*67) % (GPU_Y+60)) - 30);
        GPU_DrawFilledCircle(GPU_ShadeColor(C_WHITE, 150), cx, cy, 12);
        GPU_DrawFilledCircle(GPU_ShadeColor(C_WHITE, 150), (int16_t)(cx+12), cy, 9);
    }
    /* platforms */
    for (uint8_t i = 0; i < DJ_NUM_PLATS; i++)
    {
        Platform * p = &memory->plats[i];
        if (!p->active)
            continue;
        int16_t py = ScreenY(p->h);
        if (py < -10 || py > GPU_Y+10)
            continue;
        GPU_Color pc = p->moving ? C_C1 : C_GREEN;
        GPU_DrawBevelSquare(pc, (int16_t)p->x, py, (int16_t)p->w, 8);
    }
    DrawChar();

    /* HUD */
    sprintf(text, "HEIGHT %u", (unsigned)memory->score);
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fm, 6, 4, text);
    sprintf(text, "BEST %u", (unsigned)bestScore);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X - 6 - strlen(text)*fm->width), 4, text, (uint16_t)strlen(text));

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 90, "DOODLE JUMP");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 120, "left/right to steer, auto-bounce");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fm, GPU_X/2, 136, "tap or press a key to jump");
    }
    else if (memory->state == 2 && IF_GetCurrentTime() - memory->overTime > 300000)
    {
        GPU_DrawPanel(RGB(16,16,32), C_C1, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-40), 180, 80);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "YOU FELL");
        sprintf(text, "HEIGHT %u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-6), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+14), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G30_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };