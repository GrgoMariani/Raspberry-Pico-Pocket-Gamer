// BOMBERMAN - smooth-moving bombermen duel: lay bombs, dodge the flames

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#define BM_COLS 15
#define BM_ROWS 11
#define BM_CELL 16
#define BM_OX ((GPU_X - BM_COLS*BM_CELL)/2)
#define BM_OY 28

#define NUM_TICKS 30
#define BOMB_FUSE 60          /* ~2 s */
#define FLAME_TIME 16         /* ~0.5 s */
#define BM_HALF 6             /* actor is 12x12 px inside a 16 px cell */
#define BM_MAX_BOMBS 14
#define BM_MAX_ENEMIES 6
#define START_LIVES 3

#define BM_BTN_X 246
#define BM_BTN_Y 210
#define BM_BTN_W 70
#define BM_BTN_H 26

/* terrain */
#define T_EMPTY 0
#define T_WALL 1
#define T_BLOCK 2
/* power-ups (hidden under blocks until they are blasted) */
#define P_NONE 0
#define P_BOMB 1
#define P_FLAME 2
#define P_SPEED 3
#define P_SHIELD 4
#define P_LIFE 5
#define P_PIERCE 6
#define P_EXIT 7

typedef struct {
    float x, y;           /* pixel centre, any position between the blocks */
    uint8_t dir;          /* 0 up, 1 right, 2 down, 3 left */
    uint8_t alive;
    uint8_t kind;
    uint8_t thinkCd;
    uint8_t bombCd;
    uint8_t maxBombs;
    uint8_t range;
    float speed;
} BmEnemy;

typedef struct {
    uint8_t x, y;         /* tile */
    uint8_t active;
    uint8_t owner;        /* 0 = player, i+1 = enemy i */
    uint8_t pierce;       /* flames run through blocks */
    uint16_t timer;
    uint8_t range;
} BmBomb;

typedef struct {
    uint8_t terrain[BM_ROWS][BM_COLS];
    uint8_t power[BM_ROWS][BM_COLS];
    uint8_t flame[BM_ROWS][BM_COLS];
    BmBomb bombs[BM_MAX_BOMBS];
    BmEnemy enemies[BM_MAX_ENEMIES];
    float px, py;
    uint8_t pdir;
    float speed;
    uint8_t maxBombs;
    uint8_t flameRange;
    uint8_t shield;       /* absorbs one flame hit */
    uint8_t pierce;
    uint16_t invuln;
    uint8_t lives;
    uint8_t level;
    uint8_t enemiesLeft;
    uint32_t score;
    uint8_t state;        /* 0 ready, 1 playing, 2 dying, 3 clear, 4 over */
    uint8_t stateTick;
    uint8_t bombHeld;
} Game37Memory;

static Game37Memory * const memory = (Game37Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static const int8_t _dx[4] = { 0, 1, 0, -1 };
static const int8_t _dy[4] = { -1, 0, 1, 0 };

static uint32_t CalculateTick(void)
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static uint8_t InBounds(int x, int y)
{
    return (uint8_t)(x >= 0 && x < BM_COLS && y >= 0 && y < BM_ROWS);
}
static int TileX(float x) { return (int)floorf((x - BM_OX)/BM_CELL); }
static int TileY(float y) { return (int)floorf((y - BM_OY)/BM_CELL); }
static float CenterX(int c) { return (float)(BM_OX + c*BM_CELL + BM_CELL/2); }
static float CenterY(int r) { return (float)(BM_OY + r*BM_CELL + BM_CELL/2); }

static uint8_t SolidTile(int tx, int ty)
{
    if (!InBounds(tx, ty))
        return 1;
    return (uint8_t)(memory->terrain[ty][tx] != T_EMPTY);
}

static uint8_t BombAt(int x, int y)
{
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
        if (memory->bombs[i].active && memory->bombs[i].x == x && memory->bombs[i].y == y)
            return 1;
    return 0;
}

static uint8_t CountBombs(uint8_t owner)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
        if (memory->bombs[i].active && memory->bombs[i].owner == owner) n++;
    return n;
}

/* AABB against solid tiles and bombs. A bomb the actor is already standing on
   is ignored so it can walk off it; any other bomb blocks. */
static uint8_t RectBlocked(float nx, float ny, float cx, float cy)
{
    int x0 = (int)floorf((nx - BM_HALF - BM_OX)/BM_CELL);
    int x1 = (int)floorf((nx + BM_HALF - BM_OX)/BM_CELL);
    int y0 = (int)floorf((ny - BM_HALF - BM_OY)/BM_CELL);
    int y1 = (int)floorf((ny + BM_HALF - BM_OY)/BM_CELL);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (SolidTile(tx, ty))
                return 1;
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
    {
        BmBomb * b = &memory->bombs[i];
        if (!b->active) continue;
        float bx0 = (float)(BM_OX + b->x*BM_CELL), by0 = (float)(BM_OY + b->y*BM_CELL);
        float bx1 = bx0 + BM_CELL, by1 = by0 + BM_CELL;
        uint8_t newHit = (uint8_t)(nx + BM_HALF > bx0 && nx - BM_HALF < bx1 &&
                                   ny + BM_HALF > by0 && ny - BM_HALF < by1);
        if (!newHit) continue;
        uint8_t curHit = (uint8_t)(cx + BM_HALF > bx0 && cx - BM_HALF < bx1 &&
                                   cy + BM_HALF > by0 && cy - BM_HALF < by1);
        if (!curHit)
            return 1;
    }
    return 0;
}

static float NearestCenter(float pos, float origin, int maxIdx)
{
    int idx = (int)floorf((pos - origin - BM_CELL/2.0f)/BM_CELL + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > maxIdx) idx = maxIdx;
    return origin + idx*BM_CELL + BM_CELL/2.0f;
}

/* smooth movement with corner assist: when a straight move is blocked, slide
   the perpendicular axis toward the nearest lane centre so actors round corners
   instead of getting stuck on them. Returns 1 if any movement happened. */
static uint8_t MoveActor(float * x, float * y, int dir, float speed)
{
    float vx = _dx[dir]*speed, vy = _dy[dir]*speed;
    if (!RectBlocked(*x + vx, *y + vy, *x, *y))
    {
        *x += vx; *y += vy;
        return 1;
    }
    uint8_t moved = 0;
    if (dir == 1 || dir == 3)
    {
        float lane = NearestCenter(*y, BM_OY, BM_ROWS-1);
        float ny = (lane > *y) ? (*y + speed < lane ? *y + speed : lane)
                               : (*y - speed > lane ? *y - speed : lane);
        if (!RectBlocked(*x, ny, *x, *y)) *y = ny;
        if (!RectBlocked(*x + vx, *y, *x, *y)) { *x += vx; moved = 1; }
    }
    else
    {
        float lane = NearestCenter(*x, BM_OX, BM_COLS-1);
        float nx = (lane > *x) ? (*x + speed < lane ? *x + speed : lane)
                               : (*x - speed > lane ? *x - speed : lane);
        if (!RectBlocked(nx, *y, *x, *y)) *x = nx;
        if (!RectBlocked(*x, *y + vy, *x, *y)) { *y += vy; moved = 1; }
    }
    return moved;
}

/* does the actor's body touch any flame tile? (slightly shrunk for fairness) */
static uint8_t TouchesFlame(float x, float y)
{
    const float h = BM_HALF - 2;
    int x0 = (int)floorf((x - h - BM_OX)/BM_CELL);
    int x1 = (int)floorf((x + h - BM_OX)/BM_CELL);
    int y0 = (int)floorf((y - h - BM_OY)/BM_CELL);
    int y1 = (int)floorf((y + h - BM_OY)/BM_CELL);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (InBounds(tx, ty) && memory->flame[ty][tx])
                return 1;
    return 0;
}

/* is a tile covered by the blast of any live bomb? (used by the enemy AI) */
static uint8_t IsDangerous(int x, int y)
{
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
    {
        BmBomb * b = &memory->bombs[i];
        if (!b->active) continue;
        if (b->x == x && b->y == y) return 1;
        for (uint8_t d = 0; d < 4; d++)
            for (uint8_t s = 1; s <= b->range; s++)
            {
                int nx = b->x + _dx[d]*s, ny = b->y + _dy[d]*s;
                if (!InBounds(nx, ny) || memory->terrain[ny][nx] == T_WALL) break;
                if (nx == x && ny == y) return 1;
                if (memory->terrain[ny][nx] == T_BLOCK && !b->pierce) break;
            }
    }
    return 0;
}

/* would a hypothetical bomb at (bx,by) cover (x,y)? (enemy bombs never pierce) */
static uint8_t InBlast(int x, int y, int bx, int by, int range)
{
    if (x == bx && y == by) return 1;
    for (uint8_t d = 0; d < 4; d++)
        for (uint8_t s = 1; s <= range; s++)
        {
            int nx = bx + _dx[d]*s, ny = by + _dy[d]*s;
            if (!InBounds(nx, ny) || memory->terrain[ny][nx] == T_WALL) break;
            if (nx == x && ny == y) return 1;
            if (memory->terrain[ny][nx] == T_BLOCK) break;
        }
    return 0;
}

/* a tile the AI may route through: open, no bomb, no live flame */
static uint8_t Walkable(int x, int y)
{
    if (!InBounds(x, y)) return 0;
    if (memory->terrain[y][x] != T_EMPTY) return 0;
    if (BombAt(x, y)) return 0;
    if (memory->flame[y][x]) return 0;
    return 1;
}

/* is the rival's whole body (not just its centre tile) inside a bomb's blast?
   Matches the flame kill test, so a rival never stops straddling a blast edge. */
static uint8_t BodyInDanger(float x, float y)
{
    const float h = BM_HALF - 2;
    int x0 = (int)floorf((x - h - BM_OX)/BM_CELL);
    int x1 = (int)floorf((x + h - BM_OX)/BM_CELL);
    int y0 = (int)floorf((y - h - BM_OY)/BM_CELL);
    int y1 = (int)floorf((y + h - BM_OY)/BM_CELL);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (InBounds(tx, ty) && IsDangerous(tx, ty))
                return 1;
    return 0;
}

/*
 * Breadth-first search from (ex,ey) for the nearest tile that is safe from every
 * live bomb and (when useExtra) from a hypothetical bomb at (bx,by). Returns the
 * first step of the path to it, or 255 when no safe tile is reachable in time.
 * This is what keeps the rival bombermen from blowing themselves up: they only
 * drop a bomb when this finds an escape, and they flee along the path it gives.
 */
static uint8_t FindSafeDir(int ex, int ey, int bx, int by, int brange, uint8_t useExtra, int maxDepth)
{
    static uint8_t visited[BM_ROWS][BM_COLS];
    static int16_t qx[BM_ROWS*BM_COLS], qy[BM_ROWS*BM_COLS];
    static uint8_t qdepth[BM_ROWS*BM_COLS], qdir[BM_ROWS*BM_COLS];
    memset(visited, 0, sizeof(visited));
    int head = 0, tail = 0;
    visited[ey][ex] = 1;
    qx[tail] = (int16_t)ex; qy[tail] = (int16_t)ey; qdepth[tail] = 0; qdir[tail] = 255; tail++;
    while (head < tail)
    {
        int x = qx[head], y = qy[head];
        uint8_t depth = qdepth[head], fd = qdir[head];
        head++;
        uint8_t safe = (uint8_t)!IsDangerous(x, y);
        if (useExtra && InBlast(x, y, bx, by, brange)) safe = 0;
        if (safe) return fd;
        if (depth >= maxDepth) continue;          /* must be reachable before the fuse */
        for (uint8_t d = 0; d < 4; d++)
        {
            int nx = x + _dx[d], ny = y + _dy[d];
            if (!InBounds(nx, ny) || visited[ny][nx] || !Walkable(nx, ny))
                continue;
            visited[ny][nx] = 1;
            qx[tail] = (int16_t)nx; qy[tail] = (int16_t)ny;
            qdepth[tail] = (uint8_t)(depth + 1);
            qdir[tail] = (fd == 255) ? d : fd;
            tail++;
        }
    }
    return 255;
}

/* ------------------------------------------------------------------ level */

static void ClearArea(int x, int y)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            int nx = x + dx, ny = y + dy;
            if (InBounds(nx, ny) && memory->terrain[ny][nx] == T_BLOCK)
                memory->terrain[ny][nx] = T_EMPTY;
        }
}

static void StartLevel(uint8_t level)
{
    memset(memory->terrain, 0, sizeof(memory->terrain));
    memset(memory->power, 0, sizeof(memory->power));
    memset(memory->flame, 0, sizeof(memory->flame));
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++) memory->bombs[i].active = 0;
    for (uint8_t i = 0; i < BM_MAX_ENEMIES; i++) memory->enemies[i].alive = 0;

    memory->level = level;
    for (uint8_t y = 0; y < BM_ROWS; y++)
        for (uint8_t x = 0; x < BM_COLS; x++)
        {
            if (x == 0 || y == 0 || x == BM_COLS-1 || y == BM_ROWS-1)
                memory->terrain[y][x] = T_WALL;
            else if ((x % 2) == 0 && (y % 2) == 0)
                memory->terrain[y][x] = T_WALL;
            else if (Random_Range(100) < 62)
                memory->terrain[y][x] = T_BLOCK;
        }
    memory->px = CenterX(1); memory->py = CenterY(1);
    memory->pdir = 1;
    ClearArea(1, 1);

    static const uint8_t ex[BM_MAX_ENEMIES] = { 13, 1, 13, 7, 13, 1 };
    static const uint8_t ey[BM_MAX_ENEMIES] = { 1, 9, 9, 1, 5, 5 };
    uint8_t count = (uint8_t)(3 + (level-1));
    if (count > BM_MAX_ENEMIES) count = BM_MAX_ENEMIES;
    float espeed = 1.1f + 0.06f*(level-1);
    if (espeed > 1.8f) espeed = 1.8f;
    for (uint8_t i = 0; i < count; i++)
    {
        BmEnemy * e = &memory->enemies[i];
        e->x = CenterX(ex[i]); e->y = CenterY(ey[i]);
        e->dir = (uint8_t)Random_Range(4);
        e->alive = 1;
        e->kind = (uint8_t)(i % 3);
        e->thinkCd = (uint8_t)(i*4);
        e->bombCd = (uint8_t)(40 + Random_Range(60));
        e->maxBombs = (uint8_t)(1 + (level >= 4 ? 1 : 0));
        e->range = (uint8_t)(2 + (level >= 6 ? 1 : 0));
        e->speed = espeed;
        ClearArea(ex[i], ey[i]);
    }
    memory->enemiesLeft = count;

    /* hide a varied set of power-ups under random blocks */
    uint8_t bx[BM_ROWS*BM_COLS], by[BM_ROWS*BM_COLS];
    uint8_t nb = 0;
    for (uint8_t y = 0; y < BM_ROWS; y++)
        for (uint8_t x = 0; x < BM_COLS; x++)
            if (memory->terrain[y][x] == T_BLOCK) { bx[nb] = x; by[nb] = y; nb++; }
    static const uint8_t types[9] = {
        P_SHIELD, P_BOMB, P_FLAME, P_SPEED, P_SHIELD, P_PIERCE, P_LIFE, P_FLAME, P_BOMB
    };
    for (uint8_t t = 0; t < 9 && nb > 0; t++)
    {
        if (types[t] == P_LIFE && Random_Range(100) < 45) continue;  /* heart is rare */
        uint8_t idx = (uint8_t)Random_Range(nb);
        memory->power[by[idx]][bx[idx]] = types[t];
        bx[idx] = bx[nb-1]; by[idx] = by[nb-1]; nb--;
    }

    memory->invuln = 60;
    memory->bombHeld = 0;
    memory->state = 0;
    memory->stateTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game37Memory));
    memory->lives = START_LIVES;
    memory->maxBombs = 1;
    memory->flameRange = 2;
    memory->speed = 2.0f;
    memory->score = 0;
    Touch_ClearTap();
    Keyboard_GetPressedKeys();
    StartLevel(1);
}

/* ------------------------------------------------------------------ bombs */

static void SpawnBomb(int tx, int ty, uint8_t owner, uint8_t range, uint8_t pierce)
{
    if (!InBounds(tx, ty) || BombAt(tx, ty))
        return;
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
        if (!memory->bombs[i].active)
        {
            memory->bombs[i].active = 1;
            memory->bombs[i].x = (uint8_t)tx;
            memory->bombs[i].y = (uint8_t)ty;
            memory->bombs[i].owner = owner;
            memory->bombs[i].pierce = pierce;
            memory->bombs[i].timer = BOMB_FUSE;
            memory->bombs[i].range = range;
            return;
        }
}

static void PlacePlayerBomb(void)
{
    if (memory->state != 1)
        return;
    if (CountBombs(0) >= memory->maxBombs)
        return;
    SpawnBomb(TileX(memory->px), TileY(memory->py), 0, memory->flameRange, memory->pierce);
}

static void DestroyBlock(int x, int y)
{
    memory->terrain[y][x] = T_EMPTY;
    memory->score += 10;
}

static void AddFlame(int x, int y)
{
    if (InBounds(x, y))
        memory->flame[y][x] = FLAME_TIME;
}

static void ExplodeBomb(uint8_t i)
{
    BmBomb * b = &memory->bombs[i];
    b->active = 0;
    AddFlame(b->x, b->y);
    for (uint8_t d = 0; d < 4; d++)
        for (uint8_t s = 1; s <= b->range; s++)
        {
            int nx = b->x + _dx[d]*s, ny = b->y + _dy[d]*s;
            if (!InBounds(nx, ny) || memory->terrain[ny][nx] == T_WALL)
                break;
            AddFlame(nx, ny);
            if (memory->terrain[ny][nx] == T_BLOCK)
            {
                DestroyBlock(nx, ny);
                if (!b->pierce) break;
            }
        }
}

static void ProcessExplosions(void)
{
    uint8_t changed = 1;
    while (changed)
    {
        changed = 0;
        for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
        {
            BmBomb * b = &memory->bombs[i];
            if (b->active && (b->timer == 0 || memory->flame[b->y][b->x] > 0))
            {
                ExplodeBomb(i);
                changed = 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ actors */

static void KillEnemy(uint8_t i)
{
    memory->enemies[i].alive = 0;
    if (memory->enemiesLeft) memory->enemiesLeft--;
    memory->score += 100;
}

static void KillPlayer(void)
{
    if (memory->state != 1)
        return;
    memory->state = 2;
    memory->stateTick = 0;
    if (memory->lives) memory->lives--;
}

/* head along the shortest path to a safe, open tile no live bomb can reach */
static uint8_t CenterDir(BmEnemy * e, int ex, int ey)
{
    float dx = CenterX(ex) - e->x, dy = CenterY(ey) - e->y;
    float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax > ay) return (uint8_t)(dx > 0 ? 1 : 3);
    return (uint8_t)(dy > 0 ? 2 : 0);
}

static uint8_t FleeDir(BmEnemy * e, int ex, int ey)
{
    (void)e;
    uint8_t d = FindSafeDir(ex, ey, 0, 0, 0, 0, 4);
    if (d != 255) return d;
    /* no safe tile in reach: at least step to an open neighbour */
    uint8_t any[4];
    uint8_t na = 0;
    for (uint8_t k = 0; k < 4; k++)
        if (Walkable(ex + _dx[k], ey + _dy[k])) any[na++] = k;
    if (na) return any[Random_Range(na)];
    return 255;
}

/* hunt the player or wander, but never wander into a blast or live flame */
static uint8_t HuntDir(BmEnemy * e, int ex, int ey)
{
    (void)e;
    uint8_t opts[4];
    uint8_t n = 0;
    for (uint8_t d = 0; d < 4; d++)
    {
        int nx = ex + _dx[d], ny = ey + _dy[d];
        if (Walkable(nx, ny) && !IsDangerous(nx, ny))
            opts[n++] = d;
    }
    if (n == 0) return 255;   /* no safe step: wait rather than walk into a blast */
    if (Random_Range(100) < 65)
    {
        int ptx = TileX(memory->px), pty = TileY(memory->py);
        uint8_t best = opts[0];
        int bestDist = 0x7fff;
        for (uint8_t k = 0; k < n; k++)
        {
            int nx = ex + _dx[opts[k]], ny = ey + _dy[opts[k]];
            int dist = (nx > ptx ? nx - ptx : ptx - nx) + (ny > pty ? ny - pty : pty - ny);
            if (dist < bestDist) { bestDist = dist; best = opts[k]; }
        }
        return best;
    }
    return opts[Random_Range(n)];
}

static uint8_t EnemyWantsBomb(BmEnemy * e, int ex, int ey)
{
    (void)e;
    int ptx = TileX(memory->px), pty = TileY(memory->py);
    int dist = (ptx > ex ? ptx - ex : ex - ptx) + (pty > ey ? pty - ey : ey - pty);
    if (dist <= 3) return 1;                      /* attack the player */
    for (uint8_t d = 0; d < 4; d++)               /* dig through a block */
    {
        int nx = ex + _dx[d], ny = ey + _dy[d];
        if (InBounds(nx, ny) && memory->terrain[ny][nx] == T_BLOCK)
            return 1;
    }
    return 0;
}

static void MoveEnemies(void)
{
    for (uint8_t i = 0; i < BM_MAX_ENEMIES; i++)
    {
        BmEnemy * e = &memory->enemies[i];
        if (!e->alive) continue;
        int ex = TileX(e->x), ey = TileY(e->y);
        if (TouchesFlame(e->x, e->y)) { KillEnemy(i); continue; }

        uint8_t danger = BodyInDanger(e->x, e->y);

        /* Decide to bomb BEFORE moving, and only when an escape route to open
           safe ground exists, so a rival never traps itself in its own blast.
           Friendly fire still applies - the AI just tries to survive it. */
        if (e->bombCd) e->bombCd--;
        else if (!danger && CountBombs((uint8_t)(i+1)) < e->maxBombs &&
                 !BombAt(ex, ey) && EnemyWantsBomb(e, ex, ey) &&
                 FindSafeDir(ex, ey, ex, ey, e->range, 1, 3) != 255)
        {
            SpawnBomb(ex, ey, (uint8_t)(i+1), e->range, 0);
            e->bombCd = (uint8_t)(80 + Random_Range(70));
            danger = 1;
        }

        if (danger)
        {
            /* centre tile already safe but the body still clips a blast edge:
               slide into the middle of the tile instead of hovering on the edge */
            if (!IsDangerous(ex, ey))
                e->dir = CenterDir(e, ex, ey);
            else
                e->dir = FleeDir(e, ex, ey);
        }
        else if (e->thinkCd == 0)
        {
            e->dir = HuntDir(e, ex, ey);
            e->thinkCd = (uint8_t)(10 + Random_Range(15));
        }
        if (e->thinkCd) e->thinkCd--;
        /* a fleeing rival hurries; with no safe step it waits out the fuse */
        if (e->dir != 255)
        {
            float spd = danger ? (e->speed + 0.9f) : e->speed;
            if (!MoveActor(&e->x, &e->y, e->dir, spd))
                e->thinkCd = 0;                   /* blocked: rethink */
        }
        if (TouchesFlame(e->x, e->y)) KillEnemy(i);
    }
}

static int8_t PlayerDir(void)
{
    if (mainMemory.touchPressed && mainMemory.touch_Y < BM_OY + BM_ROWS*BM_CELL)
    {
        float dx = (float)mainMemory.touch_X - memory->px;
        float dy = (float)mainMemory.touch_Y - memory->py;
        float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax < 3 && ay < 3) return -1;
        if (ax > ay) return (int8_t)(dx > 0 ? 1 : 3);
        return (int8_t)(dy > 0 ? 2 : 0);
    }
    KeyPressedEnum held = Keyboard_GetHeldKeys();
    if (held & KEY_UP)    return 0;
    if (held & KEY_RIGHT) return 1;
    if (held & KEY_DOWN)  return 2;
    if (held & KEY_LEFT)  return 3;
    return -1;
}

static void PickUpPower(void)
{
    int tx = TileX(memory->px), ty = TileY(memory->py);
    if (!InBounds(tx, ty)) return;
    uint8_t p = memory->power[ty][tx];
    if (p == P_NONE) return;
    memory->power[ty][tx] = P_NONE;
    memory->score += 50;
    if (p == P_BOMB && memory->maxBombs < 5) memory->maxBombs++;
    else if (p == P_FLAME && memory->flameRange < 6) memory->flameRange++;
    else if (p == P_SPEED && memory->speed < 3.0f) memory->speed += 0.3f;
    else if (p == P_SHIELD) memory->shield = 1;
    else if (p == P_PIERCE) memory->pierce = 1;
    else if (p == P_LIFE && memory->lives < 5) memory->lives++;
}

static void MovePlayer(void)
{
    KeyPressedEnum held = Keyboard_GetHeldKeys();
    uint8_t bombCombo = (uint8_t)((held & KEY_UP) && (held & KEY_DOWN));
    if (bombCombo && !memory->bombHeld)
        PlacePlayerBomb();
    memory->bombHeld = bombCombo;

    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty) &&
        tx >= BM_BTN_X && tx < BM_BTN_X+BM_BTN_W && ty >= BM_BTN_Y && ty < BM_BTN_Y+BM_BTN_H)
        PlacePlayerBomb();

    if (memory->invuln) memory->invuln--;
    if (!bombCombo)
    {
        int8_t d = PlayerDir();
        if (d >= 0)
        {
            memory->pdir = (uint8_t)d;
            MoveActor(&memory->px, &memory->py, d, memory->speed);
        }
    }
    /* the player is only hurt by flame, never by touching an enemy */
    if (TouchesFlame(memory->px, memory->py))
    {
        if (memory->invuln) { /* grace period */ }
        else if (memory->shield) { memory->shield = 0; memory->invuln = 60; }
        else { KillPlayer(); return; }
    }
    PickUpPower();
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
                float sp = memory->speed; uint8_t mb = memory->maxBombs, fr = memory->flameRange;
                uint8_t sh = memory->shield, pi = memory->pierce;
                uint32_t sc = memory->score; uint8_t lv = memory->lives, level = memory->level;
                StartLevel(level);
                memory->speed = sp; memory->maxBombs = mb; memory->flameRange = fr;
                memory->shield = sh; memory->pierce = pi;
                memory->score = sc; memory->lives = lv;
            }
        }
        return;
    case 3:
        memory->stateTick++;
        if (memory->stateTick > 45)
        {
            float sp = memory->speed; uint8_t mb = memory->maxBombs, fr = memory->flameRange;
            uint8_t sh = memory->shield, pi = memory->pierce;
            uint32_t sc = memory->score; uint8_t lv = memory->lives;
            StartLevel((uint8_t)(memory->level + 1));
            memory->speed = sp; memory->maxBombs = mb; memory->flameRange = fr;
            memory->shield = sh; memory->pierce = pi;
            memory->score = sc; memory->lives = lv;
        }
        return;
    case 4:
        return;
    default:
        break;
    }

    /* playing */
    Keyboard_GetPressedKeys();   /* drain press latch (movement uses held keys) */
    for (uint8_t y = 0; y < BM_ROWS; y++)
        for (uint8_t x = 0; x < BM_COLS; x++)
            if (memory->flame[y][x]) memory->flame[y][x]--;
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
        if (memory->bombs[i].active && memory->bombs[i].timer)
            memory->bombs[i].timer--;
    ProcessExplosions();
    MoveEnemies();
    if (memory->state == 1)
        MovePlayer();
    /* the level is cleared the moment every rival bomberman is eliminated */
    if (memory->state == 1 && memory->enemiesLeft == 0)
    {
        memory->score += 200 + memory->level*50;
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

static void DrawActor(int16_t cx, int16_t cy, GPU_Color body, uint8_t isPlayer)
{
    GPU_DrawGlow(body, cx, cy, 8);
    GPU_DrawFilledCircle(body, cx, cy, 6);
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx-2), (int16_t)(cy-2), 2);
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(cx+2), (int16_t)(cy-2), 2);
    GPU_DrawPixel(C_BLACK, (uint16_t)(cx-2), (uint16_t)(cy-2));
    GPU_DrawPixel(C_BLACK, (uint16_t)(cx+2), (uint16_t)(cy-2));
    if (isPlayer)
        GPU_DrawFilledSquare(C_WHITE, (int16_t)(cx-5), (int16_t)(cy-7), 10, 3);
}

static void DrawPowerIcon(uint8_t p, int16_t cx, int16_t cy)
{
    GPU_Color c; const char * label;
    switch (p)
    {
    case P_BOMB:   c = C_C1;     label = "B"; break;
    case P_FLAME:  c = C_ORANGE; label = "F"; break;
    case P_SPEED:  c = C_C3;     label = "S"; break;
    case P_SHIELD: c = C_CYAN;   label = "V"; break;
    case P_LIFE:   c = C_RED;    label = "+"; break;
    case P_PIERCE: c = C_MAGENTA;label = "P"; break;
    default:       c = C_YELLOW; label = "E"; break;
    }
    GPU_DrawGlow(c, cx, cy, 7);
    GPU_DrawBevelSquare(c, (int16_t)(cx-6), (int16_t)(cy-6), 12, 12);
    GPU_DrawTextCentered(C_BLACK, &fontDescription[0], cx, (int16_t)(cy-4), label);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(12,10,26), C_BLACK, 0, 0, GPU_X, GPU_Y);

    for (uint8_t y = 0; y < BM_ROWS; y++)
        for (uint8_t x = 0; x < BM_COLS; x++)
        {
            int16_t px = (int16_t)(BM_OX + x*BM_CELL);
            int16_t py = (int16_t)(BM_OY + y*BM_CELL);
            uint8_t t = memory->terrain[y][x];
            if (t == T_WALL)
                GPU_DrawBevelSquare(RGB(70,72,96), px, py, BM_CELL-1, BM_CELL-1);
            else if (t == T_BLOCK)
                GPU_DrawBevelSquare(RGB(150,96,44), px, py, BM_CELL-1, BM_CELL-1);
            else
                GPU_DrawFilledSquare(RGB(20,22,36), px, py, BM_CELL-1, BM_CELL-1);
        }
    for (uint8_t y = 0; y < BM_ROWS; y++)
        for (uint8_t x = 0; x < BM_COLS; x++)
        {
            uint8_t p = memory->power[y][x];
            if (p == P_NONE || memory->terrain[y][x] != T_EMPTY) continue;
            DrawPowerIcon(p, (int16_t)(BM_OX + x*BM_CELL + BM_CELL/2),
                             (int16_t)(BM_OY + y*BM_CELL + BM_CELL/2));
        }
    for (uint8_t i = 0; i < BM_MAX_BOMBS; i++)
    {
        BmBomb * b = &memory->bombs[i];
        if (!b->active) continue;
        int16_t cx = (int16_t)(BM_OX + b->x*BM_CELL + BM_CELL/2);
        int16_t cy = (int16_t)(BM_OY + b->y*BM_CELL + BM_CELL/2);
        uint8_t pulse = (uint8_t)(b->timer < 20 && (b->timer & 2));
        GPU_Color body = b->owner == 0 ? C_BLACK : RGB(60,20,20);
        GPU_DrawFilledCircle(pulse ? C_RED : body, cx, cy, 6);
        GPU_DrawEmptyCircle(C_GRAY_MEDIUM, cx, cy, 6);
        GPU_DrawLine(C_GRAY_LIGHT, cx, (int16_t)(cy-6), (int16_t)(cx+3), (int16_t)(cy-9));
        GPU_DrawGlow(C_ORANGE, (int16_t)(cx+3), (int16_t)(cy-9), 3);
    }
    for (uint8_t y = 0; y < BM_ROWS; y++)
        for (uint8_t x = 0; x < BM_COLS; x++)
            if (memory->flame[y][x])
            {
                int16_t px = (int16_t)(BM_OX + x*BM_CELL);
                int16_t py = (int16_t)(BM_OY + y*BM_CELL);
                GPU_DrawFilledSquare(C_ORANGE, px, py, BM_CELL-1, BM_CELL-1);
                GPU_DrawFilledSquare(C_YELLOW, (int16_t)(px+3), (int16_t)(py+3), BM_CELL-7, BM_CELL-7);
            }
    static const GPU_Color enemyColor[3] = { C_RED, C_MAGENTA, C_CYAN };
    for (uint8_t i = 0; i < BM_MAX_ENEMIES; i++)
    {
        BmEnemy * e = &memory->enemies[i];
        if (!e->alive) continue;
        DrawActor((int16_t)e->x, (int16_t)e->y, enemyColor[e->kind], 0);
    }
    if (memory->state != 2 || (memory->stateTick & 2))
    {
        if (!memory->invuln || (memory->invuln & 4))
            DrawActor((int16_t)memory->px, (int16_t)memory->py, C_WHITE, 1);
        if (memory->shield)
            GPU_DrawEmptyCircle(C_CYAN, (int16_t)memory->px, (int16_t)memory->py, 8);
    }

    /* HUD */
    GPU_DrawPanel(RGB(12,12,32), C_GRAY_DARK, 0, 0, GPU_X, 24);
    sprintf(text, "SCORE %u", (unsigned)memory->score);
    GPU_DrawText(C_WHITE, fm, 4, 4, text, (uint16_t)strlen(text));
    sprintf(text, "FOES %u", (unsigned)memory->enemiesLeft);
    GPU_DrawText(memory->enemiesLeft ? C_RED : C_C3, fs, 78, 6, text, (uint16_t)strlen(text));
    sprintf(text, "LV %u", (unsigned)memory->level);
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 4, text);
    sprintf(text, "B%u F%u", (unsigned)memory->maxBombs, (unsigned)memory->flameRange);
    GPU_DrawText(C_C1, fs, 190, 6, text, (uint16_t)strlen(text));
    if (memory->shield) GPU_DrawText(C_CYAN, fs, 232, 6, "V", 1);
    if (memory->pierce) GPU_DrawText(C_MAGENTA, fs, 242, 6, "P", 1);
    for (uint8_t i = 0; i < memory->lives && i < 5; i++)
        GPU_DrawFilledCircle(C_WHITE, (int16_t)(GPU_X - 10 - i*12), 11, 4);

    GPU_DrawButton(C_ORANGE, C_BLACK, fm, "BOMB", BM_BTN_X, BM_BTN_Y, BM_BTN_W, BM_BTN_H);
    GPU_DrawText(C_GRAY_DARK, fs, 6, 212, "arrows move", 11);
    GPU_DrawText(C_GRAY_DARK, fs, 6, 224, "UP+DOWN bomb", 12);
    GPU_DrawText(C_GRAY_MEDIUM, fs, 110, 212, "kill all rivals", 15);
    GPU_DrawText(C_GRAY_DARK, fs, 110, 224, "only flame hurts", 16);

    if (memory->state == 0)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, (int16_t)(BM_OY + 40), "GET READY");
    else if (memory->state == 2)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(BM_OY + 40), "OUCH!");
    else if (memory->state == 3)
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(BM_OY + 40), "LEVEL CLEAR");
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

const Callbacks G37_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };