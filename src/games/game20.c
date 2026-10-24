// MISSILE DEFENSE - protect the cities from the incoming warheads

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#define NUM_TICKS 40

#define GROUND_Y 208
#define NUM_CITIES 5
#define NUM_BATTERIES 3
#define AMMO_PER_WAVE 10

#define MAX_ENEMY 26
#define MAX_SHOT 6
#define MAX_BOOM 12

typedef struct {
    float x, y;          /* current position */
    float sx, sy;        /* where it came from */
    float vx, vy;
    uint8_t active;
} Missile;

typedef struct {
    float x, y;          /* target of the interceptor */
    float cx, cy;        /* current position */
    float vx, vy;
    uint8_t active;
} Shot;

typedef struct {
    float x, y;
    float radius;
    float maxRadius;
    uint8_t active;
    uint8_t chain;
} Boom;

typedef struct {
    Missile _enemy[MAX_ENEMY];
    Shot _shots[MAX_SHOT];
    Boom _booms[MAX_BOOM];
    uint8_t _cityAlive[NUM_CITIES];
    uint8_t _batteryAlive[NUM_BATTERIES];
    int8_t _ammo[NUM_BATTERIES];
    uint8_t _toSpawn;
    uint16_t _spawnDelay;
    float _crossX, _crossY;
    uint8_t _usingKeys;
    uint32_t _score;
    uint8_t _wave;
    uint8_t _state;         /* 0 ready, 1 wave, 2 between, 3 over */
    uint8_t _stateTick;
} Game20Memory;

static Game20Memory * const memory = (Game20Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static const int16_t _cityX[NUM_CITIES] = { 62, 104, 200, 242, 284 };
static const int16_t _batteryX[NUM_BATTERIES] = { 20, 152, 300 };

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 30)

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* ------------------------------------------------------------------- logic */

static void StartWave(uint8_t wave)
{
    memory->_wave = wave;
    memory->_toSpawn = (uint8_t)(6 + wave*3);
    if (memory->_toSpawn > MAX_ENEMY)
        memory->_toSpawn = MAX_ENEMY;
    memory->_spawnDelay = 0;
    for (uint8_t i = 0; i < NUM_BATTERIES; i++)
    {
        memory->_ammo[i] = AMMO_PER_WAVE;
        memory->_batteryAlive[i] = 1;
    }
    for (uint8_t i = 0; i < MAX_ENEMY; i++)
        memory->_enemy[i].active = 0;
    for (uint8_t i = 0; i < MAX_SHOT; i++)
        memory->_shots[i].active = 0;
    for (uint8_t i = 0; i < MAX_BOOM; i++)
        memory->_booms[i].active = 0;
    /* every third wave one city is rebuilt */
    if (wave > 1 && (wave % 3) == 1)
        for (uint8_t i = 0; i < NUM_CITIES; i++)
            if (!memory->_cityAlive[i])
            {
                memory->_cityAlive[i] = 1;
                break;
            }
    memory->_state = 1;
    memory->_stateTick = 0;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game20Memory));
    memory->_score = 0;
    for (uint8_t i = 0; i < NUM_CITIES; i++)
        memory->_cityAlive[i] = 1;
    memory->_crossX = GPU_X/2;
    memory->_crossY = GPU_Y/2;
    StartWave(1);
    memory->_state = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void AddBoom(float x, float y, float maxRadius, uint8_t chain)
{
    for (uint8_t i = 0; i < MAX_BOOM; i++)
    {
        if (memory->_booms[i].active)
            continue;
        memory->_booms[i].x = x;
        memory->_booms[i].y = y;
        memory->_booms[i].radius = 2.0f;
        memory->_booms[i].maxRadius = maxRadius;
        memory->_booms[i].active = 1;
        memory->_booms[i].chain = chain;
        return;
    }
}

static void SpawnEnemy(void)
{
    for (uint8_t i = 0; i < MAX_ENEMY; i++)
    {
        if (memory->_enemy[i].active)
            continue;
        Missile * m = &memory->_enemy[i];
        m->active = 1;
        m->sx = (float)(Random_Range(GPU_X));
        m->sy = 0;
        m->x = m->sx;
        m->y = m->sy;
        /* aim at a city, a battery or a random spot on the ground */
        uint32_t pick = Random_Range(10);
        float tx;
        if (pick < 5)
        {
            uint8_t c = (uint8_t)Random_Range(NUM_CITIES);
            tx = (float)_cityX[c];
        }
        else if (pick < 8)
        {
            uint8_t b = (uint8_t)Random_Range(NUM_BATTERIES);
            tx = (float)_batteryX[b];
        }
        else
        {
            tx = (float)Random_Range(GPU_X);
        }
        float speed = 0.35f + 0.06f*(float)memory->_wave + 0.15f*(float)Random_Range(10)/10.0f;
        float dx = tx - m->sx;
        float dy = (float)GROUND_Y - m->sy;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 1.0f) len = 1.0f;
        m->vx = dx/len*speed;
        m->vy = dy/len*speed;
        return;
    }
}

static void Launch(float tx, float ty)
{
    if (ty > GROUND_Y-6)
        ty = GROUND_Y-6;
    if (ty < 10)
        ty = 10;
    /* the closest battery that still has ammo */
    int8_t best = -1;
    float bestDist = 1e9f;
    for (uint8_t b = 0; b < NUM_BATTERIES; b++)
    {
        if (!memory->_batteryAlive[b] || memory->_ammo[b] <= 0)
            continue;
        float d = fabsf((float)_batteryX[b] - tx);
        if (d < bestDist)
        {
            bestDist = d;
            best = (int8_t)b;
        }
    }
    if (best < 0)
        return;
    for (uint8_t i = 0; i < MAX_SHOT; i++)
    {
        if (memory->_shots[i].active)
            continue;
        Shot * s = &memory->_shots[i];
        s->active = 1;
        s->cx = (float)_batteryX[best];
        s->cy = (float)GROUND_Y;
        s->x = tx;
        s->y = ty;
        float dx = tx - s->cx;
        float dy = ty - s->cy;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 1.0f) len = 1.0f;
        float speed = 3.2f;
        s->vx = dx/len*speed;
        s->vy = dy/len*speed;
        memory->_ammo[best]--;
        return;
    }
}

static void GroundHit(Missile * m)
{
    AddBoom(m->x, m->y, 26.0f, 1);
    for (uint8_t i = 0; i < NUM_CITIES; i++)
        if (memory->_cityAlive[i] && fabsf((float)_cityX[i] - m->x) < 18.0f)
            memory->_cityAlive[i] = 0;
    for (uint8_t i = 0; i < NUM_BATTERIES; i++)
        if (memory->_batteryAlive[i] && fabsf((float)_batteryX[i] - m->x) < 14.0f)
            memory->_batteryAlive[i] = 0;
}

static void Tick(void)
{
    switch (memory->_state)
    {
    case 0:      /* get ready */
        memory->_stateTick++;
        if (memory->_stateTick > 20)
            memory->_state = 1;
        break;
    case 1:      /* wave running */
    {
        /* input */
        KeyPressedEnum held = Keyboard_GetHeldKeys();
        float step = 2.4f;
        if (held & KEY_LEFT)  { memory->_crossX -= step; memory->_usingKeys = 1; }
        if (held & KEY_RIGHT) { memory->_crossX += step; memory->_usingKeys = 1; }
        if (held & KEY_UP)    { memory->_crossY -= step; memory->_usingKeys = 1; }
        if (held & KEY_DOWN)  { memory->_crossY += step; memory->_usingKeys = 1; }
        if (memory->_crossX < 4) memory->_crossX = 4;
        if (memory->_crossX > GPU_X-4) memory->_crossX = GPU_X-4;
        if (memory->_crossY < 8) memory->_crossY = 8;
        if (memory->_crossY > GROUND_Y-8) memory->_crossY = GROUND_Y-8;
        uint8_t fire = 0;
        float fx = memory->_crossX, fy = memory->_crossY;
        if (Keyboard_TakeKeyRelease(KEY_LEFT) || Keyboard_TakeKeyRelease(KEY_RIGHT) ||
            Keyboard_TakeKeyRelease(KEY_UP) || Keyboard_TakeKeyRelease(KEY_DOWN))
            fire = 1;
        if (mainMemory.touchPressedNew)
        {
            fire = 1;
            fx = (float)mainMemory.touch_X;
            fy = (float)mainMemory.touch_Y;
            memory->_usingKeys = 0;
        }
        if (fire)
            Launch(fx, fy);

        /* spawn */
        if (memory->_toSpawn)
        {
            if (memory->_spawnDelay)
                memory->_spawnDelay--;
            else
            {
                SpawnEnemy();
                memory->_toSpawn--;
                memory->_spawnDelay = (uint16_t)(30 - memory->_wave*2);
                if (memory->_spawnDelay < 6)
                    memory->_spawnDelay = 6;
            }
        }

        /* enemy missiles */
        for (uint8_t i = 0; i < MAX_ENEMY; i++)
        {
            Missile * m = &memory->_enemy[i];
            if (!m->active)
                continue;
            m->x += m->vx;
            m->y += m->vy;
            if (m->y >= GROUND_Y)
            {
                m->active = 0;
                GroundHit(m);
            }
        }

        /* interceptors */
        for (uint8_t i = 0; i < MAX_SHOT; i++)
        {
            Shot * s = &memory->_shots[i];
            if (!s->active)
                continue;
            s->cx += s->vx;
            s->cy += s->vy;
            float dx = s->x - s->cx;
            float dy = s->y - s->cy;
            if (dx*dx + dy*dy < 9.0f || s->cy <= s->y)
            {
                s->active = 0;
                AddBoom(s->x, s->y, 30.0f, 0);
            }
        }

        /* explosions grow, shrink and destroy */
        for (uint8_t i = 0; i < MAX_BOOM; i++)
        {
            Boom * b = &memory->_booms[i];
            if (!b->active)
                continue;
            if (b->radius < b->maxRadius)
                b->radius += 1.1f;
            else
            {
                b->radius -= 0.9f;
                if (b->radius <= 1.0f)
                {
                    b->active = 0;
                    continue;
                }
            }
            for (uint8_t e = 0; e < MAX_ENEMY; e++)
            {
                Missile * m = &memory->_enemy[e];
                if (!m->active)
                    continue;
                float dx = m->x - b->x;
                float dy = m->y - b->y;
                if (dx*dx + dy*dy < b->radius*b->radius)
                {
                    m->active = 0;
                    memory->_score += 25;
                    if (b->chain)
                        AddBoom(m->x, m->y, 26.0f, 1);
                }
            }
        }

        /* wave finished? */
        uint8_t busy = memory->_toSpawn ? 1 : 0;
        if (!busy)
            for (uint8_t i = 0; i < MAX_ENEMY; i++)
                if (memory->_enemy[i].active)
                    busy = 1;
        if (!busy)
            for (uint8_t i = 0; i < MAX_BOOM; i++)
                if (memory->_booms[i].active)
                    busy = 1;
        if (!busy)
        {
            uint8_t cities = 0;
            for (uint8_t i = 0; i < NUM_CITIES; i++)
                if (memory->_cityAlive[i])
                    cities++;
            for (uint8_t b = 0; b < NUM_BATTERIES; b++)
                if (memory->_ammo[b] > 0)
                    memory->_score += (uint32_t)memory->_ammo[b]*5;
            memory->_score += 100*cities;
            if (cities == 0)
            {
                memory->_state = 3;
                memory->_stateTick = 0;
                mainMemory._gameState = GS_DONE;
            }
            else
            {
                memory->_state = 2;
                memory->_stateTick = 0;
            }
        }
        break;
    }
    case 2:      /* between waves */
        memory->_stateTick++;
        if (memory->_stateTick > 40)
            StartWave((uint8_t)(memory->_wave + 1));
        break;
    case 3:      /* game over */
        memory->_stateTick++;
        break;
    default:
        break;
    }
}

static void ResetButtonTouched(void)
{
    if (!mainMemory.touchPressedNew)
        return;
    if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W &&
        mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
        G_Init();
}

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    if (tick > mainMemory._lastRenderedTick + 4*NUM_TICKS)
        mainMemory._lastRenderedTick = tick - 4*NUM_TICKS;
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        if (mainMemory._gameState == GS_DONE)
        {
            ResetButtonTouched();
            continue;
        }
        Tick();
    }
}

/* ------------------------------------------------------------------ drawing */

static void DrawCity(int16_t x, uint8_t alive)
{
    if (!alive)
    {
        GPU_DrawFilledSquare(C_GRAY_DARK, (int16_t)(x-14), (int16_t)(GROUND_Y-4), 28, 4);
        GPU_DrawFilledSquare(RGB(60,40,30), (int16_t)(x-8), (int16_t)(GROUND_Y-7), 6, 7);
        GPU_DrawFilledSquare(RGB(50,34,26), (int16_t)(x+2), (int16_t)(GROUND_Y-6), 8, 6);
        return;
    }
    static const int8_t heights[5] = { 14, 22, 18, 26, 16 };
    for (uint8_t i = 0; i < 5; i++)
    {
        int16_t bx = (int16_t)(x - 14 + i*6);
        int16_t bh = heights[i];
        GPU_DrawFilledSquare(RGB(70,80,110), bx, (int16_t)(GROUND_Y-bh), 5, bh);
        for (int16_t wy = GROUND_Y-bh+2; wy < GROUND_Y-2; wy += 4)
            GPU_DrawPixel(C_YELLOW, (uint16_t)(bx+2), (uint16_t)wy);
    }
}

static void DrawBattery(uint8_t index)
{
    int16_t x = _batteryX[index];
    if (!memory->_batteryAlive[index])
    {
        GPU_DrawFilledSquare(C_GRAY_DARK, (int16_t)(x-10), (int16_t)(GROUND_Y-3), 20, 3);
        return;
    }
    GPU_DrawFilledCircle(C_GRAY_MEDIUM, x, GROUND_Y, 10);
    GPU_DrawFilledSquare(RGB(20,20,26), x-10, GROUND_Y, 20, 8);
    GPU_DrawFilledCircle(C_C1, x, (int16_t)(GROUND_Y-4), 4);
    /* ammo pips */
    for (int8_t a = 0; a < memory->_ammo[index] && a < AMMO_PER_WAVE; a++)
        GPU_DrawPixel(C_YELLOW, (uint16_t)(x-9 + a*2), (uint16_t)(GROUND_Y+4));
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(4,4,20), RGB(20,8,30), 0, 0, GPU_X, GROUND_Y);
    for (uint8_t i = 0; i < 50; i++)
    {
        uint32_t seed = (uint32_t)i*2654435761u;
        GPU_DrawPixel(C_GRAY_DARK, (uint16_t)((seed >> 5) % GPU_X), (uint16_t)((seed >> 15) % (GROUND_Y-40)));
    }
    /* ground */
    GPU_DrawVerticalGradient(RGB(30,60,30), RGB(10,24,10), 0, GROUND_Y, GPU_X, (int16_t)(GPU_Y-GROUND_Y));
    GPU_DrawLine(C_GREEN, 0, GROUND_Y, GPU_X-1, GROUND_Y);

    for (uint8_t i = 0; i < NUM_CITIES; i++)
        DrawCity(_cityX[i], memory->_cityAlive[i]);
    for (uint8_t i = 0; i < NUM_BATTERIES; i++)
        DrawBattery(i);

    /* enemy missiles and their trails */
    for (uint8_t i = 0; i < MAX_ENEMY; i++)
    {
        Missile * m = &memory->_enemy[i];
        if (!m->active)
            continue;
        GPU_DrawLine(C_RED, (int16_t)m->sx, (int16_t)m->sy, (int16_t)m->x, (int16_t)m->y);
        GPU_DrawFilledCircle(C_YELLOW, (int16_t)m->x, (int16_t)m->y, 2);
    }
    for (uint8_t i = 0; i < MAX_SHOT; i++)
    {
        Shot * s = &memory->_shots[i];
        if (!s->active)
            continue;
        GPU_DrawLine(C_CYAN, (int16_t)s->cx, (int16_t)s->cy, (int16_t)(s->cx - s->vx*4), (int16_t)(s->cy - s->vy*4));
        GPU_DrawFilledCircle(C_WHITE, (int16_t)s->cx, (int16_t)s->cy, 1);
    }
    for (uint8_t i = 0; i < MAX_BOOM; i++)
    {
        Boom * b = &memory->_booms[i];
        if (!b->active)
            continue;
        GPU_DrawEmptyCircle(C_ORANGE, (int16_t)b->x, (int16_t)b->y, (int16_t)b->radius);
        GPU_DrawEmptyCircle(C_YELLOW, (int16_t)b->x, (int16_t)b->y, (int16_t)(b->radius*0.6f));
        GPU_DrawFilledCircle(C_WHITE, (int16_t)b->x, (int16_t)b->y, (int16_t)(b->radius*0.25f));
    }

    /* the crosshair when playing with the keys */
    if (memory->_usingKeys && memory->_state == 1)
    {
        int16_t cx = (int16_t)memory->_crossX, cy = (int16_t)memory->_crossY;
        GPU_DrawLine(C_C3, (int16_t)(cx-7), cy, (int16_t)(cx-2), cy);
        GPU_DrawLine(C_C3, (int16_t)(cx+2), cy, (int16_t)(cx+7), cy);
        GPU_DrawLine(C_C3, cx, (int16_t)(cy-7), cx, (int16_t)(cy-2));
        GPU_DrawLine(C_C3, cx, (int16_t)(cy+2), cx, (int16_t)(cy+7));
    }

    /* HUD */
    char text[24];
    int len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fm, 4, 2, text, (uint16_t)len);
    len = sprintf(text, "WAVE %u", memory->_wave);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X - len*fm->width - 4), 2, text, (uint16_t)len);

    if (memory->_state == 0)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 60, "MISSILE DEFENSE");
    else if (memory->_state == 2)
    {
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, 60, "WAVE CLEAR");
    }
    else if (memory->_state == 3)
    {
        GPU_DrawPanel(RGB(16,8,8), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-44), 180, 92);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-38), "ALL CITIES LOST");
        len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-12), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
    else
    {
        const char * hint = "touch: tap target  keys: aim + release fires";
        GPU_DrawText(C_GRAY_DARK, &fontDescription[0], 4, (int16_t)(GPU_Y-11), hint, (uint16_t)strlen(hint));
    }
}

static void G_Deinit(void)
{

}

const Callbacks G20_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
