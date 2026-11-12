// TANK FIGHT - blast the enemy tanks across a destructible arena

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#define TK_COLS 20
#define TK_ROWS 11
#define TK_CELL 16
#define TK_OX 0
#define TK_OY 26

#define NUM_TICKS 30
#define TK_HALF 6            /* tank is 12x12 px */
#define TK_MAX_ENEMIES 4
#define TK_MAX_BULLETS 14
#define TK_BULLET_SPEED 5.0f
#define TK_PLAYER_SPEED 1.7f
#define START_LIVES 3

#define TK_BTN_X 246
#define TK_BTN_Y 210
#define TK_BTN_W 70
#define TK_BTN_H 26

/* tiles */
#define TK_EMPTY 0
#define TK_BRICK 1
#define TK_STEEL 2

typedef struct {
    float x, y;
    uint8_t dir;          /* 0 up, 1 right, 2 down, 3 left */
    uint8_t alive;
    uint8_t fireCd;
    uint8_t thinkCd;
    uint8_t kind;
} TkEnemy;

typedef struct {
    float x, y;
    uint8_t dir;
    uint8_t active;
    uint8_t owner;        /* 0 player, 1 enemy */
} TkBullet;

typedef struct {
    uint8_t tile[TK_ROWS][TK_COLS];
    TkEnemy enemies[TK_MAX_ENEMIES];
    TkBullet bullets[TK_MAX_BULLETS];
    float px, py;
    uint8_t pdir;
    uint8_t fireCd;
    uint8_t fireHeld;
    uint16_t invuln;
    uint8_t lives;
    uint8_t wave;
    uint8_t spawned, killed, waveTotal;
    uint16_t spawnTimer;
    uint32_t score;
    uint8_t state;        /* 0 ready, 1 playing, 2 dying, 3 wave clear, 4 over */
    uint8_t stateTick;
} Game38Memory;

static Game38Memory * const memory = (Game38Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static const int8_t _dx[4] = { 0, 1, 0, -1 };
static const int8_t _dy[4] = { -1, 0, 1, 0 };

static uint32_t CalculateTick(void)
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static float CellCX(int c) { return (float)(TK_OX + c*TK_CELL + TK_CELL/2); }
static float CellCY(int r) { return (float)(TK_OY + r*TK_CELL + TK_CELL/2); }

static uint8_t SolidAt(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= TK_COLS || ty >= TK_ROWS)
        return 1;
    return (uint8_t)(memory->tile[ty][tx] != TK_EMPTY);
}

static uint8_t RectHitsSolid(float x, float y)
{
    int x0 = (int)floorf((x - TK_HALF - TK_OX)/TK_CELL);
    int x1 = (int)floorf((x + TK_HALF - TK_OX)/TK_CELL);
    int y0 = (int)floorf((y - TK_HALF - TK_OY)/TK_CELL);
    int y1 = (int)floorf((y + TK_HALF - TK_OY)/TK_CELL);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (SolidAt(tx, ty))
                return 1;
    return 0;
}

/* returns 1 if the tank could move */
static uint8_t MoveTank(float * x, float * y, uint8_t dir, float speed)
{
    float nx = *x + _dx[dir]*speed;
    float ny = *y + _dy[dir]*speed;
    if (RectHitsSolid(nx, ny))
        return 0;
    *x = nx; *y = ny;
    return 1;
}

/* ------------------------------------------------------------------ arena */

static void ClearArea(int cx, int cy, int r)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
        {
            int tx = cx + dx, ty = cy + dy;
            if (tx > 0 && ty > 0 && tx < TK_COLS-1 && ty < TK_ROWS-1)
                memory->tile[ty][tx] = TK_EMPTY;
        }
}

static void BuildArena(void)
{
    for (int y = 0; y < TK_ROWS; y++)
        for (int x = 0; x < TK_COLS; x++)
        {
            if (x == 0 || y == 0 || x == TK_COLS-1 || y == TK_ROWS-1)
                memory->tile[y][x] = TK_STEEL;
            else
                memory->tile[y][x] = TK_EMPTY;
        }
    /* scattered cover: mostly brick, a little steel, kept sparse so the arena
       stays open and manoeuvrable */
    for (int y = 1; y < TK_ROWS-1; y++)
        for (int x = 1; x < TK_COLS-1; x++)
        {
            uint32_t r = Random_Range(100);
            if (r < 11) memory->tile[y][x] = TK_BRICK;
            else if (r < 14) memory->tile[y][x] = TK_STEEL;
        }
    /* a couple of longer brick walls for cover */
    for (int i = 0; i < 4; i++)
    {
        int x = 2 + (int)Random_Range(TK_COLS-5);
        int y = 2 + (int)Random_Range(TK_ROWS-4);
        uint8_t horiz = (uint8_t)(Random_Range(2));
        for (int k = 0; k < 3; k++)
        {
            int tx = x + (horiz ? k : 0), ty = y + (horiz ? 0 : k);
            if (tx > 0 && ty > 0 && tx < TK_COLS-1 && ty < TK_ROWS-1)
                memory->tile[ty][tx] = TK_BRICK;
        }
    }
    /* keep the spawn zones clear */
    ClearArea(TK_COLS/2, TK_ROWS-2, 1);      /* player */
    ClearArea(2, 1, 1);
    ClearArea(TK_COLS/2, 1, 1);
    ClearArea(TK_COLS-3, 1, 1);
}

static void StartWave(uint8_t wave)
{
    memory->wave = wave;
    uint8_t total = (uint8_t)(2 + wave);
    if (total > 12) total = 12;
    memory->waveTotal = total;
    memory->spawned = 0;
    memory->killed = 0;
    memory->spawnTimer = 20;
    for (uint8_t i = 0; i < TK_MAX_ENEMIES; i++) memory->enemies[i].alive = 0;
    for (uint8_t i = 0; i < TK_MAX_BULLETS; i++) memory->bullets[i].active = 0;
    memory->state = 0;
    memory->stateTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game38Memory));
    memory->lives = START_LIVES;
    memory->score = 0;
    BuildArena();
    memory->px = CellCX(TK_COLS/2);
    memory->py = CellCY(TK_ROWS-2);
    memory->pdir = 0;
    memory->invuln = 60;
    Touch_ClearTap();
    Keyboard_GetPressedKeys();
    StartWave(1);
}

/* ------------------------------------------------------------------ combat */

static void Fire(uint8_t owner, float x, float y, uint8_t dir)
{
    if (owner == 0)
    {
        uint8_t n = 0;
        for (uint8_t i = 0; i < TK_MAX_BULLETS; i++)
            if (memory->bullets[i].active && memory->bullets[i].owner == 0) n++;
        if (n >= 2) return;
    }
    for (uint8_t i = 0; i < TK_MAX_BULLETS; i++)
        if (!memory->bullets[i].active)
        {
            memory->bullets[i].active = 1;
            memory->bullets[i].owner = owner;
            memory->bullets[i].dir = dir;
            memory->bullets[i].x = x + _dx[dir]*(TK_HALF+3);
            memory->bullets[i].y = y + _dy[dir]*(TK_HALF+3);
            return;
        }
}

static void SpawnEnemy(void)
{
    for (uint8_t i = 0; i < TK_MAX_ENEMIES; i++)
        if (!memory->enemies[i].alive)
        {
            static const uint8_t sc[3] = { 2, TK_COLS/2, TK_COLS-3 };
            uint8_t pick = (uint8_t)Random_Range(3);
            TkEnemy * e = &memory->enemies[i];
            e->alive = 1;
            e->x = CellCX(sc[pick]);
            e->y = CellCY(1);
            e->dir = 2;
            e->fireCd = (uint8_t)(30 + Random_Range(40));
            e->thinkCd = (uint8_t)(20 + Random_Range(30));
            e->kind = (uint8_t)(memory->spawned % 3);
            memory->spawned++;
            return;
        }
}

static void KillPlayer(void)
{
    if (memory->state != 1 || memory->invuln)
        return;
    memory->state = 2;
    memory->stateTick = 0;
    if (memory->lives) memory->lives--;
}

static void UpdateBullets(void)
{
    for (uint8_t i = 0; i < TK_MAX_BULLETS; i++)
    {
        TkBullet * b = &memory->bullets[i];
        if (!b->active) continue;
        b->x += _dx[b->dir]*TK_BULLET_SPEED;
        b->y += _dy[b->dir]*TK_BULLET_SPEED;
        int tx = (int)floorf((b->x - TK_OX)/TK_CELL);
        int ty = (int)floorf((b->y - TK_OY)/TK_CELL);
        if (tx < 0 || ty < 0 || tx >= TK_COLS || ty >= TK_ROWS)
        { b->active = 0; continue; }
        uint8_t t = memory->tile[ty][tx];
        if (t == TK_STEEL) { b->active = 0; continue; }
        if (t == TK_BRICK) { memory->tile[ty][tx] = TK_EMPTY; b->active = 0; continue; }
        if (b->owner == 0)
        {
            for (uint8_t k = 0; k < TK_MAX_ENEMIES; k++)
            {
                TkEnemy * e = &memory->enemies[k];
                if (!e->alive) continue;
                float dx = b->x - e->x, dy = b->y - e->y;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx < TK_HALF+2 && dy < TK_HALF+2)
                {
                    e->alive = 0;
                    b->active = 0;
                    memory->killed++;
                    memory->score += 100;
                    break;
                }
            }
        }
        else
        {
            float dx = b->x - memory->px, dy = b->y - memory->py;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx < TK_HALF+2 && dy < TK_HALF+2)
            {
                b->active = 0;
                KillPlayer();
            }
        }
    }
}

/* would a shot in `dir` reach the player? `throughBrick` lets the brute see
   past (and chew through) brick walls, the others need a clear line */
static uint8_t AimedAtPlayer(TkEnemy * e, uint8_t dir, uint8_t throughBrick)
{
    float x = e->x, y = e->y;
    for (int s = 0; s < 80; s++)
    {
        x += _dx[dir]*4.0f; y += _dy[dir]*4.0f;
        int tx = (int)floorf((x - TK_OX)/TK_CELL);
        int ty = (int)floorf((y - TK_OY)/TK_CELL);
        if (tx < 0 || ty < 0 || tx >= TK_COLS || ty >= TK_ROWS) return 0;
        uint8_t t = memory->tile[ty][tx];
        if (t == TK_STEEL) return 0;
        if (t == TK_BRICK && !throughBrick) return 0;
        float dx = x - memory->px, dy = y - memory->py;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < TK_HALF+2 && dy < TK_HALF+2) return 1;
    }
    return 0;
}

static uint8_t CanMoveDir(TkEnemy * e, uint8_t dir)
{
    return (uint8_t)!RectHitsSolid(e->x + _dx[dir]*3.0f, e->y + _dy[dir]*3.0f);
}

/* open direction that most reduces (toward=1) or increases (toward=0) the
   distance to the player */
static uint8_t SeekDir(TkEnemy * e, uint8_t toward)
{
    uint8_t opts[4];
    uint8_t n = 0;
    for (uint8_t d = 0; d < 4; d++)
        if (CanMoveDir(e, d)) opts[n++] = d;
    if (n == 0) return (uint8_t)Random_Range(4);
    if (Random_Range(100) < 20) return opts[Random_Range(n)];
    uint8_t best = opts[0];
    float bestD = toward ? 1e9f : -1e9f;
    for (uint8_t k = 0; k < n; k++)
    {
        float nx = e->x + _dx[opts[k]]*8.0f, ny = e->y + _dy[opts[k]]*8.0f;
        float dx = nx - memory->px, dy = ny - memory->py;
        float dist = dx*dx + dy*dy;
        if (toward ? (dist < bestD) : (dist > bestD)) { bestD = dist; best = opts[k]; }
    }
    return best;
}

/*
 * Each colour fights differently:
 *   kind 0 RED    - hunter: fast, chases hard, fires the moment it has a shot.
 *   kind 1 MAGENTA- ambusher: keeps its distance, snaps onto a clear line and
 *                   takes accurate aimed shots.
 *   kind 2 ORANGE - brute: slow, advances steadily and sprays forward, chewing
 *                   through brick walls to open a path.
 */
static void UpdateEnemies(void)
{
    float waveAdd = 0.05f*(memory->wave-1);
    for (uint8_t i = 0; i < TK_MAX_ENEMIES; i++)
    {
        TkEnemy * e = &memory->enemies[i];
        if (!e->alive) continue;
        float speed;
        if (e->kind == 0)      speed = 1.5f + waveAdd;
        else if (e->kind == 1) speed = 1.1f + waveAdd;
        else                   speed = 0.8f + waveAdd;
        if (speed > 2.0f) speed = 2.0f;

        float dx = memory->px - e->x, dy = memory->py - e->y;
        float dist = sqrtf(dx*dx + dy*dy);

        if (e->thinkCd) e->thinkCd--;
        else
        {
            if (e->kind == 0)            /* hunter: chase */
            {
                e->dir = SeekDir(e, 1);
                e->thinkCd = (uint8_t)(10 + Random_Range(12));
            }
            else if (e->kind == 1)       /* ambusher: hold a medium range */
            {
                if (dist < 70.0f)       e->dir = SeekDir(e, 0);
                else if (dist > 130.0f) e->dir = SeekDir(e, 1);
                else                    e->dir = SeekDir(e, (uint8_t)(Random_Range(2)));
                e->thinkCd = (uint8_t)(14 + Random_Range(18));
            }
            else                         /* brute: steady advance */
            {
                e->dir = SeekDir(e, 1);
                e->thinkCd = (uint8_t)(18 + Random_Range(20));
            }
        }
        if (!MoveTank(&e->x, &e->y, e->dir, speed))
            e->thinkCd = 0;

        if (e->fireCd) e->fireCd--;
        else
        {
            if (e->kind == 0)            /* hunter: fire on a clear shot, else spray */
            {
                if (AimedAtPlayer(e, e->dir, 0))
                {
                    Fire(1, e->x, e->y, e->dir);
                    e->fireCd = (uint8_t)(22 + Random_Range(20));
                }
                else if (Random_Range(100) < 25)
                {
                    Fire(1, e->x, e->y, e->dir);
                    e->fireCd = (uint8_t)(30 + Random_Range(25));
                }
                else e->fireCd = 4;
            }
            else if (e->kind == 1)       /* ambusher: snap to a clear line, snipe */
            {
                uint8_t fired = 0;
                for (uint8_t d = 0; d < 4; d++)
                    if (AimedAtPlayer(e, d, 0))
                    {
                        e->dir = d;
                        Fire(1, e->x, e->y, d);
                        e->fireCd = (uint8_t)(35 + Random_Range(30));
                        fired = 1;
                        break;
                    }
                if (!fired) e->fireCd = 6;
            }
            else                         /* brute: constant forward spray */
            {
                Fire(1, e->x, e->y, e->dir);
                e->fireCd = (uint8_t)(20 + Random_Range(18));
            }
        }
    }
}

static void PlayerInput(void)
{
    KeyPressedEnum held = Keyboard_GetHeldKeys();
    uint8_t fireCombo = (uint8_t)((held & KEY_UP) && (held & KEY_DOWN));
    if (fireCombo && !memory->fireHeld && !memory->fireCd)
    {
        Fire(0, memory->px, memory->py, memory->pdir);
        memory->fireCd = 12;
    }
    memory->fireHeld = fireCombo;
    if (memory->fireCd) memory->fireCd--;

    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty) &&
        tx >= TK_BTN_X && tx < TK_BTN_X+TK_BTN_W && ty >= TK_BTN_Y && ty < TK_BTN_Y+TK_BTN_H &&
        !memory->fireCd)
    {
        Fire(0, memory->px, memory->py, memory->pdir);
        memory->fireCd = 12;
    }

    if (fireCombo)
        return;
    int8_t dir = -1;
    if (mainMemory.touchPressed && mainMemory.touch_Y < TK_OY + TK_ROWS*TK_CELL)
    {
        float dx = (float)mainMemory.touch_X - memory->px;
        float dy = (float)mainMemory.touch_Y - memory->py;
        float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax > 4 || ay > 4)
            dir = (ax > ay) ? (int8_t)(dx > 0 ? 1 : 3) : (int8_t)(dy > 0 ? 2 : 0);
    }
    else
    {
        if (held & KEY_UP) dir = 0;
        else if (held & KEY_RIGHT) dir = 1;
        else if (held & KEY_DOWN) dir = 2;
        else if (held & KEY_LEFT) dir = 3;
    }
    if (dir >= 0)
    {
        memory->pdir = (uint8_t)dir;
        MoveTank(&memory->px, &memory->py, (uint8_t)dir, TK_PLAYER_SPEED);
    }
}

/* ------------------------------------------------------------------ tick */

static void Tick(void)
{
    switch (memory->state)
    {
    case 0:
        memory->stateTick++;
        if (memory->stateTick > 25) { memory->state = 1; memory->stateTick = 0; }
        return;
    case 2:
        memory->stateTick++;
        if (memory->stateTick > 40)
        {
            if (memory->lives == 0)
            {
                memory->state = 4;
                if (memory->score > bestScore) bestScore = memory->score;
                mainMemory._gameState = GS_DONE;
            }
            else
            {
                memory->px = CellCX(TK_COLS/2);
                memory->py = CellCY(TK_ROWS-2);
                memory->pdir = 0;
                memory->invuln = 90;
                for (uint8_t i = 0; i < TK_MAX_BULLETS; i++)
                    if (memory->bullets[i].owner == 1) memory->bullets[i].active = 0;
                memory->state = 1;
            }
        }
        return;
    case 3:
        memory->stateTick++;
        if (memory->stateTick > 45)
            StartWave((uint8_t)(memory->wave + 1));
        return;
    case 4:
        return;
    default:
        break;
    }

    /* playing */
    /* drive/fire use the held keys; drain the press latch so it cannot leak
       into the game-over screen and instantly restart the battle */
    Keyboard_GetPressedKeys();
    if (memory->invuln) memory->invuln--;
    if (memory->spawned < memory->waveTotal)
    {
        if (memory->spawnTimer) memory->spawnTimer--;
        else
        {
            uint8_t active = 0;
            for (uint8_t i = 0; i < TK_MAX_ENEMIES; i++)
                if (memory->enemies[i].alive) active++;
            if (active < TK_MAX_ENEMIES)
            {
                SpawnEnemy();
                memory->spawnTimer = 50;
            }
        }
    }
    PlayerInput();
    UpdateEnemies();
    UpdateBullets();
    if (memory->state == 1 && memory->killed >= memory->waveTotal)
    {
        memory->score += 200 + memory->wave*50;
        memory->state = 3;
        memory->stateTick = 0;
    }
}

static void G_Update(void)
{
    if (memory->state == 4)
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    uint32_t tick = CalculateTick();
    if (tick > mainMemory._lastRenderedTick + 2*NUM_TICKS)
        mainMemory._lastRenderedTick = tick - 2*NUM_TICKS;
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

/* ------------------------------------------------------------------ draw */

static void DrawTank(float x, float y, uint8_t dir, GPU_Color color)
{
    int16_t cx = (int16_t)x, cy = (int16_t)y;
    /* tracks */
    GPU_DrawFilledSquare(GPU_ShadeColor(color, 110), (int16_t)(cx-TK_HALF), (int16_t)(cy-TK_HALF), 12, 12);
    /* body */
    GPU_DrawBevelSquare(color, (int16_t)(cx-TK_HALF+1), (int16_t)(cy-TK_HALF+1), 10, 10);
    /* barrel */
    int16_t bx = (int16_t)(cx + _dx[dir]*7), by = (int16_t)(cy + _dy[dir]*7);
    if (_dx[dir] != 0)
        GPU_DrawFilledSquare(GPU_ShadeColor(color, 200), (int16_t)(cx < bx ? cx : bx), (int16_t)(cy-1), 8, 3);
    else
        GPU_DrawFilledSquare(GPU_ShadeColor(color, 200), (int16_t)(cx-1), (int16_t)(cy < by ? cy : by), 3, 8);
    /* turret */
    GPU_DrawFilledCircle(GPU_ShadeColor(color, 230), cx, cy, 3);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,10,18), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* tiles */
    for (int y = 0; y < TK_ROWS; y++)
        for (int x = 0; x < TK_COLS; x++)
        {
            int16_t px = (int16_t)(TK_OX + x*TK_CELL);
            int16_t py = (int16_t)(TK_OY + y*TK_CELL);
            uint8_t t = memory->tile[y][x];
            if (t == TK_BRICK)
            {
                GPU_DrawFilledSquare(RGB(150,70,40), px, py, TK_CELL, TK_CELL);
                GPU_DrawLine(RGB(90,40,24), px, (int16_t)(py+5), (int16_t)(px+TK_CELL-1), (int16_t)(py+5));
                GPU_DrawLine(RGB(90,40,24), px, (int16_t)(py+10), (int16_t)(px+TK_CELL-1), (int16_t)(py+10));
                GPU_DrawLine(RGB(90,40,24), (int16_t)(px+7), py, (int16_t)(px+7), (int16_t)(py+5));
                GPU_DrawLine(RGB(90,40,24), (int16_t)(px+3), (int16_t)(py+5), (int16_t)(px+3), (int16_t)(py+10));
                GPU_DrawLine(RGB(90,40,24), (int16_t)(px+11), (int16_t)(py+10), (int16_t)(px+11), (int16_t)(py+15));
            }
            else if (t == TK_STEEL)
            {
                GPU_DrawBevelSquare(RGB(120,124,140), px, py, TK_CELL-1, TK_CELL-1);
                GPU_DrawFilledSquare(RGB(180,184,200), (int16_t)(px+4), (int16_t)(py+4), 7, 7);
            }
        }

    /* bullets */
    for (uint8_t i = 0; i < TK_MAX_BULLETS; i++)
    {
        TkBullet * b = &memory->bullets[i];
        if (!b->active) continue;
        GPU_Color c = b->owner == 0 ? C_YELLOW : C_ORANGE;
        GPU_DrawGlow(c, (int16_t)b->x, (int16_t)b->y, 4);
        GPU_DrawFilledCircle(C_WHITE, (int16_t)b->x, (int16_t)b->y, 2);
    }

    /* enemies */
    static const GPU_Color enemyColor[3] = { C_RED, C_MAGENTA, RGB(200,120,40) };
    for (uint8_t i = 0; i < TK_MAX_ENEMIES; i++)
    {
        TkEnemy * e = &memory->enemies[i];
        if (!e->alive) continue;
        DrawTank(e->x, e->y, e->dir, enemyColor[e->kind]);
    }

    /* player (blink while invulnerable) */
    if (memory->state != 2 && (!memory->invuln || (memory->invuln & 4)))
        DrawTank(memory->px, memory->py, memory->pdir, C_C3);

    /* HUD */
    GPU_DrawPanel(RGB(12,12,28), C_GRAY_DARK, 0, 0, GPU_X, 22);
    sprintf(text, "SCORE %u", (unsigned)memory->score);
    GPU_DrawText(C_WHITE, fm, 4, 3, text, (uint16_t)strlen(text));
    sprintf(text, "WAVE %u", (unsigned)memory->wave);
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 3, text);
    sprintf(text, "LEFT %u", (unsigned)(memory->waveTotal - memory->killed));
    GPU_DrawText(C_C1, fs, 205, 5, text, (uint16_t)strlen(text));
    for (uint8_t i = 0; i < memory->lives && i < 4; i++)
        GPU_DrawFilledSquare(C_C3, (int16_t)(GPU_X - 14 - i*14), 6, 10, 10);

    /* footer */
    GPU_DrawButton(C_ORANGE, C_BLACK, fm, "FIRE", TK_BTN_X, TK_BTN_Y, TK_BTN_W, TK_BTN_H);
    GPU_DrawText(C_GRAY_DARK, fs, 6, 212, "arrows drive", 12);
    GPU_DrawText(C_GRAY_DARK, fs, 6, 224, "UP+DOWN fire", 12);
    GPU_DrawText(C_GRAY_MEDIUM, fs, 110, 212, "bricks break", 12);
    GPU_DrawText(C_GRAY_DARK, fs, 110, 224, "steel does not", 14);

    if (memory->state == 0)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, (int16_t)(TK_OY + 60), "WAVE READY");
    else if (memory->state == 2)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(TK_OY + 60), "TANK DOWN");
    else if (memory->state == 3)
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(TK_OY + 60), "WAVE CLEAR");
    else if (memory->state == 4)
    {
        GPU_DrawPanel(RGB(16,10,10), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-40), 180, 80);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "GAME OVER");
        sprintf(text, "SCORE %u  BEST %u", (unsigned)memory->score, (unsigned)bestScore);
        GPU_DrawTextCentered(C_WHITE, fs, GPU_X/2, (int16_t)(GPU_Y/2-6), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+14), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G38_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };