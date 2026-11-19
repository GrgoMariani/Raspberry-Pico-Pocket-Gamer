// DINO RUN - an endless runner: jump the cacti, duck the pterodactyls

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 30
#define DR_GROUND_Y 202          /* the ground line on screen */
#define DR_DINO_X 46             /* the dino stays at this x, the world scrolls */
#define DR_GRAVITY 0.85f
#define DR_FASTFALL 2.2f         /* gravity while DOWN is held in the air */
#define DR_JUMP_V 10.4f
#define DR_BASE_SPEED 5.0f
#define DR_MAX_SPEED 9.6f
#define DR_NUM_OBSTACLES 6
#define DR_DUCK_ZONE_Y 186       /* touch below this line = duck */

/* obstacle types */
#define DR_CACTUS_SMALL 0        /* w14 h26 */
#define DR_CACTUS_BIG 1          /* w18 h36 */
#define DR_CACTUS_CLUSTER 2      /* w34 h26 */
#define DR_BIRD 3                /* w22 h14, flies at one of three heights */

typedef struct {
    float x;
    uint8_t type;
    uint8_t w, h;
    uint8_t airH;                /* bird: height of its belly above the ground */
    uint8_t active;
} Obstacle;

typedef struct {
    float y;                     /* height of the dino's feet above the ground */
    float vy;
    float dist;                  /* scrolled distance in pixels */
    float nextGap;               /* pixels until the next obstacle spawns */
    Obstacle obs[DR_NUM_OBSTACLES];
    uint32_t score;
    uint32_t milestone;          /* last celebrated hundred */
    uint8_t state;               /* 0 ready, 1 running, 2 dead */
    uint8_t ducking;
    uint8_t animTick;
    uint8_t flashTicks;          /* score flash after a milestone */
    uint32_t overTime;           /* when the run ended (restart delay) */
} Game39Memory;

static Game39Memory * const memory = (Game39Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static float Speed(void)
{
    float s = DR_BASE_SPEED + 0.55f*(float)(memory->score/100);
    return s > DR_MAX_SPEED ? DR_MAX_SPEED : s;
}

static uint8_t OnGround(void)
{
    return (uint8_t)(memory->y <= 0.01f && memory->vy <= 0.0f);
}

static uint8_t Night(void)
{
    return (uint8_t)((memory->score/400) & 1);
}

static void SpawnObstacle(void)
{
    int slot = -1;
    for (uint8_t i = 0; i < DR_NUM_OBSTACLES; i++)
        if (!memory->obs[i].active) { slot = i; break; }
    if (slot < 0)
        return;
    Obstacle * o = &memory->obs[slot];
    o->active = 1;
    o->x = GPU_X + 30;
    o->airH = 0;
    if (memory->score >= 100 && Random_Range(10) >= 5)
    {
        /* pterodactyl: low = jump, middle = duck (or jump), high = run under.
           They show up earlier and more often than before (score 100, 50%). */
        static const uint8_t heights[3] = { 2, 16, 40 };
        o->type = DR_BIRD;
        o->w = 22;
        o->h = 14;
        o->airH = heights[Random_Range(3)];
    }
    else
    {
        uint32_t r = Random_Range(10);
        if (r < 4)
        {
            o->type = DR_CACTUS_SMALL;
            o->w = 14;
            o->h = 26;
        }
        else if (r < 7)
        {
            o->type = DR_CACTUS_BIG;
            o->w = 18;
            o->h = 36;
        }
        else
        {
            o->type = DR_CACTUS_CLUSTER;
            o->w = 34;
            o->h = 26;
        }
    }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game39Memory));
    memory->state = 0;
    memory->nextGap = 220;       /* a little grace before the first obstacle */
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void StartRun(void)
{
    memory->state = 1;
    memory->y = 0;
    memory->vy = 0;
}

static void Jump(void)
{
    if (memory->state == 1 && OnGround())
    {
        memory->vy = DR_JUMP_V;
        memory->y = 0.02f;
        memory->ducking = 0;
    }
}

static void Die(void)
{
    memory->state = 2;
    memory->overTime = IF_GetCurrentTime();
    if (memory->score > bestScore)
        bestScore = memory->score;
    mainMemory._gameState = GS_DONE;
}

/* the dino's collision box, a little smaller than the sprite so near misses
   feel fair */
static void DinoBox(int16_t * x, int16_t * y, int16_t * w, int16_t * h)
{
    int16_t fy = (int16_t)(DR_GROUND_Y - memory->y);
    if (memory->ducking)
    {
        *x = DR_DINO_X;
        *y = (int16_t)(fy - 11);
        *w = 26;
        *h = 11;
    }
    else
    {
        *x = DR_DINO_X + 2;
        *y = (int16_t)(fy - 24);
        *w = 16;
        *h = 24;
    }
}

static uint8_t Hits(const Obstacle * o)
{
    int16_t dx, dy, dw, dh;
    DinoBox(&dx, &dy, &dw, &dh);
    int16_t ox = (int16_t)(o->x + 2);
    int16_t ow = (int16_t)(o->w - 4);
    int16_t oy = (int16_t)(DR_GROUND_Y - o->airH - o->h + 2);
    int16_t oh = (int16_t)(o->h - 4);
    return (uint8_t)(dx < ox+ow && dx+dw > ox && dy < oy+oh && dy+dh > oy);
}

static void Tick(void)
{
    memory->animTick++;
    if (memory->state != 1)
        return;

    float speed = Speed();
    memory->dist += speed;
    memory->score = (uint32_t)(memory->dist/12.0f);
    if (memory->score/100 > memory->milestone)
    {
        memory->milestone = memory->score/100;
        memory->flashTicks = 24;
    }
    if (memory->flashTicks)
        memory->flashTicks--;

    /* duck: DOWN held, or a finger held on the bottom strip of the screen */
    uint8_t duckHeld = (Keyboard_GetHeldKeys() & KEY_DOWN) ? 1 : 0;
    if (mainMemory.touchPressed && mainMemory.touch_Y >= DR_DUCK_ZONE_Y)
        duckHeld = 1;
    memory->ducking = (uint8_t)(duckHeld && OnGround());

    /* vertical physics (positive y is up) */
    if (!OnGround())
    {
        memory->vy -= duckHeld ? DR_FASTFALL : DR_GRAVITY;
        memory->y += memory->vy;
        if (memory->y <= 0.0f)
        {
            memory->y = 0.0f;
            memory->vy = 0.0f;
        }
    }

    /* obstacles scroll towards the dino */
    memory->nextGap -= speed;
    if (memory->nextGap <= 0.0f)
    {
        SpawnObstacle();
        /* tighter gaps than before, but still room to land and jump again
           (at the 9.6 px/tick cap the minimum gap is ~25 ticks, a jump is ~24) */
        memory->nextGap = 115.0f + (float)Random_Range(115) + speed*13.5f;
    }
    for (uint8_t i = 0; i < DR_NUM_OBSTACLES; i++)
    {
        Obstacle * o = &memory->obs[i];
        if (!o->active)
            continue;
        o->x -= speed + ((o->type == DR_BIRD) ? 1.2f : 0.0f);
        if (o->x < -60.0f)
        {
            o->active = 0;
            continue;
        }
        if (Hits(o))
        {
            Die();
            return;
        }
    }
}

static void G_Update(void)
{
    /* input is handled once per main loop iteration; the press latch is
       drained every time so a stale press cannot restart the game later */
    uint8_t jumpPressed = Keyboard_TakeKey(KEY_UP);
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
    {
        /* a tap in the duck strip only ducks while running */
        if (memory->state != 1 || ty < DR_DUCK_ZONE_Y)
            jumpPressed = 1;
    }
    KeyPressedEnum rest = Keyboard_GetPressedKeys();

    if (memory->state == 0)
    {
        if (jumpPressed || rest != KEY_NONE)
            StartRun();
    }
    else if (memory->state == 1)
    {
        if (jumpPressed)
            Jump();
    }
    else
    {
        /* ~0.7s to read the score before a press restarts */
        if (IF_GetCurrentTime() - memory->overTime > 700000 &&
            (jumpPressed || rest != KEY_NONE || mainMemory.touchPressedNew))
        {
            G_Init();
            StartRun();
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

static void DrawCactus(const Obstacle * o)
{
    int16_t x = (int16_t)o->x;
    GPU_Color body = Night() ? RGB(30,110,50) : RGB(40,150,70);
    GPU_Color lit = Night() ? RGB(45,140,65) : RGB(70,190,95);
    uint8_t trunks = (o->type == DR_CACTUS_CLUSTER) ? 3 : 1;
    for (uint8_t t = 0; t < trunks; t++)
    {
        int16_t tx = (int16_t)(x + t*11);
        int16_t tw = (o->type == DR_CACTUS_BIG) ? 8 : 6;
        int16_t th = (int16_t)(o->h - (t == 1 ? 6 : 0));
        int16_t ty = (int16_t)(DR_GROUND_Y - th);
        GPU_DrawFilledSquare(body, (int16_t)(tx+4), ty, tw, th);
        GPU_DrawFilledSquare(lit, (int16_t)(tx+4), ty, 2, th);
        /* arms */
        GPU_DrawFilledSquare(body, tx, (int16_t)(ty+6), 4, 4);
        GPU_DrawFilledSquare(body, tx, (int16_t)(ty+2), 3, 6);
        GPU_DrawFilledSquare(body, (int16_t)(tx+4+tw), (int16_t)(ty+10), 4, 4);
        GPU_DrawFilledSquare(body, (int16_t)(tx+5+tw), (int16_t)(ty+6), 3, 8);
    }
}

static void DrawBird(const Obstacle * o)
{
    int16_t x = (int16_t)o->x;
    int16_t cy = (int16_t)(DR_GROUND_Y - o->airH - o->h/2);
    GPU_Color body = Night() ? C_GRAY_LIGHT : C_PURPLE;
    /* body + head + beak */
    GPU_DrawFilledCircle(body, (int16_t)(x+10), cy, 5);
    GPU_DrawFilledSquare(body, (int16_t)(x+12), (int16_t)(cy-4), 8, 5);
    GPU_DrawFilledTriangle(C_ORANGE, (int16_t)(x+20), (int16_t)(cy-3), (int16_t)(x+20), cy, (int16_t)(x+26), (int16_t)(cy-1));
    GPU_DrawPixel(C_WHITE, (uint16_t)(x+16), (uint16_t)(cy-3));
    /* flapping wing */
    if ((memory->animTick/4) & 1)
        GPU_DrawFilledTriangle(body, (int16_t)(x+4), (int16_t)(cy-2), (int16_t)(x+14), (int16_t)(cy-2), (int16_t)(x+8), (int16_t)(cy-12));
    else
        GPU_DrawFilledTriangle(body, (int16_t)(x+4), cy, (int16_t)(x+14), cy, (int16_t)(x+8), (int16_t)(cy+9));
}

static void DrawDino(void)
{
    int16_t x = DR_DINO_X;
    int16_t fy = (int16_t)(DR_GROUND_Y - memory->y);
    GPU_Color body = Night() ? C_WHITE : C_C3;
    GPU_Color dark = GPU_ShadeColor(body, 150);
    uint8_t leg = (uint8_t)((memory->animTick/4) & 1);
    uint8_t air = (uint8_t)!OnGround();

    if (memory->ducking)
    {
        /* long flat body, head stretched forward */
        GPU_DrawFilledTriangle(dark, (int16_t)(x-8), (int16_t)(fy-10), (int16_t)(x+2), (int16_t)(fy-12), (int16_t)(x+2), (int16_t)(fy-4));
        GPU_DrawFilledSquare(body, (int16_t)(x-2), (int16_t)(fy-12), 24, 8);
        GPU_DrawFilledSquare(body, (int16_t)(x+16), (int16_t)(fy-15), 14, 9);
        GPU_DrawPixel(C_BLACK, (uint16_t)(x+25), (uint16_t)(fy-13));
        if (!air)
        {
            GPU_DrawFilledSquare(dark, (int16_t)(x+2+(leg?2:0)), (int16_t)(fy-4), 4, 4);
            GPU_DrawFilledSquare(dark, (int16_t)(x+12+(leg?0:2)), (int16_t)(fy-4), 4, 4);
        }
    }
    else
    {
        /* tail, body, head */
        GPU_DrawFilledTriangle(dark, (int16_t)(x-7), (int16_t)(fy-16), (int16_t)(x+3), (int16_t)(fy-20), (int16_t)(x+3), (int16_t)(fy-9));
        GPU_DrawFilledSquare(body, x, (int16_t)(fy-20), 14, 14);
        GPU_DrawFilledSquare(body, (int16_t)(x+8), (int16_t)(fy-28), 14, 11);
        GPU_DrawFilledSquare(dark, (int16_t)(x+12), (int16_t)(fy-12), 3, 3);   /* tiny arm */
        /* eye (an X when dead) */
        if (memory->state == 2)
        {
            GPU_DrawLine(C_BLACK, (int16_t)(x+15), (int16_t)(fy-26), (int16_t)(x+18), (int16_t)(fy-23));
            GPU_DrawLine(C_BLACK, (int16_t)(x+18), (int16_t)(fy-26), (int16_t)(x+15), (int16_t)(fy-23));
        }
        else
        {
            GPU_DrawFilledSquare(C_WHITE, (int16_t)(x+15), (int16_t)(fy-26), 3, 3);
            GPU_DrawPixel(C_BLACK, (uint16_t)(x+17), (uint16_t)(fy-25));
        }
        /* legs: run cycle on the ground, tucked in the air */
        if (air)
        {
            GPU_DrawFilledSquare(dark, (int16_t)(x+2), (int16_t)(fy-6), 4, 4);
            GPU_DrawFilledSquare(dark, (int16_t)(x+9), (int16_t)(fy-6), 4, 4);
        }
        else if (memory->state == 1)
        {
            GPU_DrawFilledSquare(dark, (int16_t)(x+2), (int16_t)(fy-6), 4, (int16_t)(leg ? 6 : 4));
            GPU_DrawFilledSquare(dark, (int16_t)(x+9), (int16_t)(fy-6), 4, (int16_t)(leg ? 4 : 6));
        }
        else
        {
            GPU_DrawFilledSquare(dark, (int16_t)(x+2), (int16_t)(fy-6), 4, 6);
            GPU_DrawFilledSquare(dark, (int16_t)(x+9), (int16_t)(fy-6), 4, 6);
        }
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    uint8_t night = Night();
    int32_t dist = (int32_t)memory->dist;

    GPU_ClearFramebuffers();
    /* sky */
    if (night)
    {
        GPU_DrawVerticalGradient(RGB(4,4,20), RGB(22,26,60), 0, 0, GPU_X, DR_GROUND_Y);
        GPU_DrawStarfieldIn(77, 40, IF_GetCurrentTime(), GPU_X, 130);
        /* crescent moon */
        GPU_DrawFilledCircle(C_GRAY_LIGHT, 268, 36, 12);
        GPU_DrawFilledCircle(RGB(10,12,34), 274, 32, 11);
    }
    else
    {
        GPU_DrawVerticalGradient(RGB(50,110,190), RGB(150,195,230), 0, 0, GPU_X, DR_GROUND_Y);
        GPU_DrawGlow(C_YELLOW, 268, 36, 16);
        GPU_DrawFilledCircle(C_YELLOW, 268, 36, 9);
    }
    /* parallax clouds */
    for (uint8_t i = 0; i < 3; i++)
    {
        int16_t cx = (int16_t)((GPU_X + 90 - (dist/5 + i*137) % (GPU_X+90)) - 45);
        int16_t cy = (int16_t)(34 + i*30);
        GPU_Color cc = night ? RGB(40,44,80) : C_WHITE;
        GPU_DrawFilledCircle(cc, cx, cy, 10);
        GPU_DrawFilledCircle(cc, (int16_t)(cx+11), cy, 7);
        GPU_DrawFilledCircle(cc, (int16_t)(cx-11), cy, 7);
    }
    /* two layers of parallax dunes */
    for (uint8_t i = 0; i < 6; i++)
    {
        int16_t hx = (int16_t)((GPU_X + 140 - (dist/3 + i*105) % (GPU_X+140)) - 70);
        GPU_DrawFilledCircle(night ? RGB(26,30,52) : RGB(120,150,110), hx, DR_GROUND_Y, 38);
    }
    for (uint8_t i = 0; i < 7; i++)
    {
        int16_t hx = (int16_t)((GPU_X + 110 - (dist*2/3 + i*83) % (GPU_X+110)) - 55);
        GPU_DrawFilledCircle(night ? RGB(36,40,66) : RGB(95,130,90), hx, DR_GROUND_Y, 24);
    }
    /* ground */
    GPU_DrawFilledSquare(night ? RGB(50,46,60) : RGB(196,170,110), 0, DR_GROUND_Y, GPU_X, GPU_Y-DR_GROUND_Y);
    GPU_DrawLine(night ? C_GRAY_MEDIUM : RGB(120,100,60), 0, DR_GROUND_Y, GPU_X-1, DR_GROUND_Y);
    /* scrolling pebbles and dashes */
    for (uint8_t i = 0; i < 24; i++)
    {
        uint32_t hh = (uint32_t)i * 2654435761u;
        int16_t px = (int16_t)((GPU_X + 20 - (dist + (int32_t)(hh % 340)) % (GPU_X+20)) - 10);
        int16_t py = (int16_t)(DR_GROUND_Y + 5 + (hh>>9) % 4 * 8);
        GPU_Color pc = night ? RGB(70,66,84) : RGB(150,128,84);
        GPU_DrawFilledSquare(pc, px, py, (int16_t)(2 + (hh>>5)%3), 2);
    }

    /* obstacles */
    for (uint8_t i = 0; i < DR_NUM_OBSTACLES; i++)
    {
        const Obstacle * o = &memory->obs[i];
        if (!o->active)
            continue;
        if (o->type == DR_BIRD)
            DrawBird(o);
        else
            DrawCactus(o);
    }

    DrawDino();

    /* HUD: chrome-style zero padded score, best next to it */
    sprintf(text, "%05u", (unsigned)memory->score);
    GPU_Color sc = (memory->flashTicks && (memory->flashTicks & 2)) ? C_YELLOW : (night ? C_WHITE : C_GRAY_DARK);
    GPU_DrawText(sc, fm, (int16_t)(GPU_X - 6 - 5*fm->width), 6, text, 5);
    if (bestScore)
    {
        /* "HI xxxxx" (8 chars) sits left of the score with a 10px gap */
        sprintf(text, "HI %05u", (unsigned)bestScore);
        GPU_DrawText(night ? C_GRAY_MEDIUM : C_GRAY_DARK, fm,
                     (int16_t)(GPU_X - 6 - 5*fm->width - 10 - 8*fm->width), 6, text, 8);
    }
    if (memory->state == 1)
        GPU_DrawText(night ? C_GRAY_DARK : RGB(150,128,84), fs, GPU_X-40, GPU_Y-10, "DUCK", 4);

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 78, "DINO RUN");
        GPU_DrawTextCentered(night ? C_WHITE : C_GRAY_DARK, fm, GPU_X/2, 112, "UP / tap = jump");
        GPU_DrawTextCentered(night ? C_WHITE : C_GRAY_DARK, fm, GPU_X/2, 126, "DOWN / hold bottom = duck");
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 148, "press UP or tap to run");
    }
    else if (memory->state == 2 && IF_GetCurrentTime() - memory->overTime > 300000)
    {
        GPU_DrawPanel(RGB(16,16,32), C_ORANGE, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-46), 180, 84);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-40), "GAME OVER");
        sprintf(text, "SCORE %05u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-12), text);
        sprintf(text, "BEST %05u", (unsigned)bestScore);
        GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, (int16_t)(GPU_Y/2+2), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+20), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G39_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };