// ASTEROIDS - the rocks are real 3d bodies tumbling in space

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 30

#define FIELD_W (GPU_X/2)          /* the field goes from -160 to +160 */
#define FIELD_H (GPU_Y/2)          /* and from -120 to +120 */
#define ROCK_Z FX(10.0)            /* everything happens on this plane */

#define MAX_ROCKS 14
#define MAX_BULLETS 5
#define MAX_SPARKS 30
#define BULLET_LIFE 55
#define START_LIVES 3
#define BONUS_EVERY 10000

#define SHIP_R FX(9.0)
#define THRUST FX(0.055)
#define MAX_SPEED FX(3.2)
#define ROT_STEP ANG(7)

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 26)

typedef struct {
    fixed x, y, vx, vy;
    uint8_t active;
    uint8_t size;              /* 0 big, 1 medium, 2 small */
    int32_t rotX, rotY, rotZ;
    int8_t spinX, spinY, spinZ;
    Vec3 verts[6];
} Rock;

typedef struct {
    fixed x, y, vx, vy;
    uint8_t life;
} Bullet;

typedef struct {
    fixed x, y, vx, vy;
    uint8_t life;
    GPU_Color color;
} Spark;

typedef struct {
    Rock _rocks[MAX_ROCKS];
    Bullet _bullets[MAX_BULLETS];
    Spark _sparks[MAX_SPARKS];
    fixed _shipX, _shipY, _shipVX, _shipVY;
    int32_t _heading;
    uint8_t _thrusting;
    uint8_t _lives;
    uint32_t _score;
    uint32_t _nextBonus;
    uint8_t _wave;
    uint8_t _invulnerable;
    uint8_t _fireCooldown;
    uint8_t _respawn;
    uint8_t _gameOver;
    uint8_t _waveBanner;
} Game17Memory;

static Game17Memory * const memory = (Game17Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* the rocks all share the topology of an octahedron, only the radii differ */
static const GPU3D_Face _rockFaces[8] = {
    { 3, {3,2,0}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {4,3,0}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {5,4,0}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {2,5,0}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {2,3,1}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {3,4,1}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {4,5,1}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {5,2,1}, {0}, {0}, C_WHITE, 0, 0, 1 },
};

/* the ship is a triangular prism, so it stays three dimensional while turning */
static const Vec3 _shipVerts[6] = {
    { FX(11.0), 0,  FX(3.0) },
    { FX(-8.0), FX(6.5), FX(3.0) },
    { FX(-8.0), FX(-6.5), FX(3.0) },
    { FX(11.0), 0,  FX(-3.0) },
    { FX(-8.0), FX(6.5), FX(-3.0) },
    { FX(-8.0), FX(-6.5), FX(-3.0) },
};

static const GPU3D_Face _shipFaces[5] = {
    { 3, {0,1,2}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 3, {3,5,4}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 4, {0,3,4,1}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 4, {0,2,5,3}, {0}, {0}, C_WHITE, 0, 0, 1 },
    { 4, {1,4,5,2}, {0}, {0}, C_WHITE, 0, 0, 1 },
};

static const GPU3D_Mesh _shipMesh = { _shipVerts, 6, _shipFaces, 5 };

static const fixed _rockRadius[3] = { FX(26.0), FX(16.0), FX(9.0) };
static const uint32_t _rockScore[3] = { 20, 50, 100 };
static const GPU_Color _rockColor[3] = { C_GRAY_LIGHT, C_C1, C_C3 };

/* squared distance in Q16, computed in 64 bits: the field is 320 units wide,
   so dx*dx alone does not fit into a Q16 fixed point value */
static int64_t DistSq(fixed dx, fixed dy)
{
    return (((int64_t)dx*dx) + ((int64_t)dy*dy)) >> 16;
}

static void MakeRockVerts(Rock * rock, uint8_t size)
{
    fixed r = _rockRadius[size];
    /* vertex order has to match _rockFaces: 0 = +y, 1 = -y, 2 = -z, 3 = +x,
       4 = +z, 5 = -x, every spike gets its own random length */
    for (uint8_t i = 0; i < 6; i++)
    {
        fixed jitter = r/2 + (fixed)Random_Range((uint32_t)r);   /* 0.5r .. 1.5r */
        Vec3 v = { 0, 0, 0 };
        switch (i)
        {
        case 0: v.y = jitter; break;
        case 1: v.y = -jitter; break;
        case 2: v.z = -jitter; break;
        case 3: v.x = jitter; break;
        case 4: v.z = jitter; break;
        default: v.x = -jitter; break;
        }
        rock->verts[i] = v;
    }
}

static Rock * NewRock(void)
{
    for (uint8_t i = 0; i < MAX_ROCKS; i++)
        if (!memory->_rocks[i].active)
            return &memory->_rocks[i];
    return NULL;
}

static void SpawnRock(uint8_t size, fixed x, fixed y)
{
    Rock * rock = NewRock();
    if (!rock)
        return;
    rock->active = 1;
    rock->size = size;
    rock->x = x;
    rock->y = y;
    fixed speed = FX(0.4) + (fixed)Random_Range((uint32_t)FX(0.9)) + FX_MUL(FX(0.25), FX_INT(size));
    int32_t angle = (int32_t)Random_Range(ANG_FULL);
    rock->vx = FX_MUL(speed, GPU3D_Cos(angle));
    rock->vy = FX_MUL(speed, GPU3D_Sin(angle));
    rock->rotX = (int32_t)Random_Range(ANG_FULL);
    rock->rotY = (int32_t)Random_Range(ANG_FULL);
    rock->rotZ = (int32_t)Random_Range(ANG_FULL);
    rock->spinX = (int8_t)Random_Range(9) - 4;
    rock->spinY = (int8_t)Random_Range(9) - 4;
    rock->spinZ = (int8_t)Random_Range(9) - 4;
    MakeRockVerts(rock, size);
}

static void AddSpark(fixed x, fixed y, GPU_Color color)
{
    for (uint8_t i = 0; i < MAX_SPARKS; i++)
    {
        if (memory->_sparks[i].life)
            continue;
        memory->_sparks[i].x = x;
        memory->_sparks[i].y = y;
        int32_t angle = (int32_t)Random_Range(ANG_FULL);
        fixed speed = FX(0.3) + (fixed)Random_Range((uint32_t)FX(1.4));
        memory->_sparks[i].vx = FX_MUL(speed, GPU3D_Cos(angle));
        memory->_sparks[i].vy = FX_MUL(speed, GPU3D_Sin(angle));
        memory->_sparks[i].life = (uint8_t)(6 + Random_Range(10));
        memory->_sparks[i].color = color;
        return;
    }
}

static void Explode(fixed x, fixed y, uint8_t count, GPU_Color color)
{
    for (uint8_t i = 0; i < count; i++)
        AddSpark(x, y, color);
}

static void StartWave(uint8_t wave)
{
    memory->_wave = wave;
    memory->_waveBanner = 40;
    uint8_t count = (uint8_t)(2 + wave);
    if (count > 7)
        count = 7;
    for (uint8_t i = 0; i < count; i++)
    {
        /* spawn at the border, but not on top of the ship */
        fixed x = 0, y = 0;
        for (uint8_t attempt = 0; attempt < 20; attempt++)
        {
            if (Random_Range(2))
            {
                x = Random_Range(2) ? FX(FIELD_W-10) : FX(-FIELD_W+10);
                y = (fixed)Random_Range(2*FIELD_H) - FX(FIELD_H);
            }
            else
            {
                y = Random_Range(2) ? FX(FIELD_H-10) : FX(-FIELD_H+10);
                x = (fixed)Random_Range(2*FIELD_W) - FX(FIELD_W);
            }
            fixed dx = x - memory->_shipX;
            fixed dy = y - memory->_shipY;
            if (DistSq(dx, dy) > FX_MUL(FX(70.0), FX(70.0)))
                break;
        }
        SpawnRock(0, x, y);
    }
}

static void RespawnShip(void)
{
    memory->_shipX = 0;
    memory->_shipY = 0;
    memory->_shipVX = 0;
    memory->_shipVY = 0;
    memory->_heading = ANG(90);
    memory->_invulnerable = 60;
    memory->_respawn = 0;
    memory->_thrusting = 0;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game17Memory));
    memory->_lives = START_LIVES;
    memory->_nextBonus = BONUS_EVERY;
    RespawnShip();
    StartWave(1);
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static fixed WrapX(fixed x)
{
    if (x > FX(FIELD_W)) return x - FX(2*FIELD_W);
    if (x < FX(-FIELD_W)) return x + FX(2*FIELD_W);
    return x;
}

static fixed WrapY(fixed y)
{
    if (y > FX(FIELD_H)) return y - FX(2*FIELD_H);
    if (y < FX(-FIELD_H)) return y + FX(2*FIELD_H);
    return y;
}

static void Fire(void)
{
    if (memory->_fireCooldown)
        return;
    for (uint8_t i = 0; i < MAX_BULLETS; i++)
    {
        if (memory->_bullets[i].life)
            continue;
        memory->_bullets[i].x = memory->_shipX + FX_MUL(FX(11.0), GPU3D_Cos(memory->_heading));
        memory->_bullets[i].y = memory->_shipY + FX_MUL(FX(11.0), GPU3D_Sin(memory->_heading));
        memory->_bullets[i].vx = memory->_shipVX + FX_MUL(FX(6.0), GPU3D_Cos(memory->_heading));
        memory->_bullets[i].vy = memory->_shipVY + FX_MUL(FX(6.0), GPU3D_Sin(memory->_heading));
        memory->_bullets[i].life = BULLET_LIFE;
        memory->_fireCooldown = 6;
        return;
    }
}

static void BreakRock(Rock * rock)
{
    memory->_score += _rockScore[rock->size];
    if (memory->_score >= memory->_nextBonus && memory->_lives < 6)
    {
        memory->_lives++;
        memory->_nextBonus += BONUS_EVERY;
    }
    Explode(rock->x, rock->y, (uint8_t)(6 + 4*(2-rock->size)), C_GRAY_LIGHT);
    if (rock->size < 2)
    {
        uint8_t newSize = (uint8_t)(rock->size + 1);
        SpawnRock(newSize, rock->x, rock->y);
        SpawnRock(newSize, rock->x, rock->y);
    }
    rock->active = 0;
}

static void Tick(void)
{
    if (memory->_gameOver)
        return;

    if (memory->_fireCooldown)
        memory->_fireCooldown--;
    if (memory->_invulnerable)
        memory->_invulnerable--;
    if (memory->_waveBanner)
        memory->_waveBanner--;

    KeyPressedEnum held = Keyboard_GetHeldKeys();
    KeyPressedEnum pressed = Keyboard_GetPressedKeys();

    /* touch: the top third is the engine, the outer thirds turn, a tap in the
       middle fires */
    uint8_t touchThrust = 0, touchLeft = 0, touchRight = 0, touchFire = 0;
    if (mainMemory.touchPressed)
    {
        if (mainMemory.touch_Y < GPU_Y/3)
            touchThrust = 1;
        else if (mainMemory.touch_X < GPU_X/3)
            touchLeft = 1;
        else if (mainMemory.touch_X > 2*GPU_X/3)
            touchRight = 1;
        else if (mainMemory.touchPressedNew)
            touchFire = 1;
    }

    if (memory->_respawn)
    {
        memory->_respawn--;
        if (memory->_respawn == 0)
        {
            if (memory->_lives == 0)
            {
                memory->_gameOver = 1;
                mainMemory._gameState = GS_DONE;
                return;
            }
            RespawnShip();
        }
    }
    else
    {
        if ((held & KEY_LEFT) || touchLeft)   memory->_heading += ROT_STEP;
        if ((held & KEY_RIGHT) || touchRight) memory->_heading -= ROT_STEP;
        memory->_heading &= (ANG_FULL-1);
        memory->_thrusting = ((held & KEY_UP) || touchThrust) ? 1 : 0;
        if (memory->_thrusting)
        {
            memory->_shipVX += FX_MUL(THRUST, GPU3D_Cos(memory->_heading));
            memory->_shipVY += FX_MUL(THRUST, GPU3D_Sin(memory->_heading));
            if ((mainMemory._lastRenderedTick & 1) == 0)
                AddSpark(memory->_shipX - FX_MUL(FX(9.0), GPU3D_Cos(memory->_heading)),
                         memory->_shipY - FX_MUL(FX(9.0), GPU3D_Sin(memory->_heading)),
                         C_ORANGE);
        }
        fixed speed2 = FX_MUL(memory->_shipVX, memory->_shipVX) + FX_MUL(memory->_shipVY, memory->_shipVY);
        if (speed2 > FX_MUL(MAX_SPEED, MAX_SPEED))
        {
            memory->_shipVX = FX_MUL(memory->_shipVX, FX(0.96));
            memory->_shipVY = FX_MUL(memory->_shipVY, FX(0.96));
        }
        memory->_shipX = WrapX(memory->_shipX + memory->_shipVX);
        memory->_shipY = WrapY(memory->_shipY + memory->_shipVY);

        if ((pressed & KEY_DOWN) || touchFire)
            Fire();
    }

    /* bullets */
    for (uint8_t i = 0; i < MAX_BULLETS; i++)
    {
        if (!memory->_bullets[i].life)
            continue;
        memory->_bullets[i].life--;
        memory->_bullets[i].x = WrapX(memory->_bullets[i].x + memory->_bullets[i].vx);
        memory->_bullets[i].y = WrapY(memory->_bullets[i].y + memory->_bullets[i].vy);
    }

    /* rocks */
    uint8_t alive = 0;
    for (uint8_t i = 0; i < MAX_ROCKS; i++)
    {
        Rock * rock = &memory->_rocks[i];
        if (!rock->active)
            continue;
        alive++;
        rock->x = WrapX(rock->x + rock->vx);
        rock->y = WrapY(rock->y + rock->vy);
        rock->rotX = (rock->rotX + rock->spinX) & (ANG_FULL-1);
        rock->rotY = (rock->rotY + rock->spinY) & (ANG_FULL-1);
        rock->rotZ = (rock->rotZ + rock->spinZ) & (ANG_FULL-1);

        uint8_t destroyed = 0;
        for (uint8_t b = 0; b < MAX_BULLETS; b++)
        {
            if (!memory->_bullets[b].life)
                continue;
            fixed dx = memory->_bullets[b].x - rock->x;
            fixed dy = memory->_bullets[b].y - rock->y;
            fixed r = _rockRadius[rock->size];
            if (DistSq(dx, dy) < FX_MUL(r,r))
            {
                memory->_bullets[b].life = 0;
                BreakRock(rock);
                destroyed = 1;
                break;
            }
        }
        if (destroyed)
        {
            alive--;
            continue;
        }
        if (!memory->_respawn && !memory->_invulnerable)
        {
            fixed dx = memory->_shipX - rock->x;
            fixed dy = memory->_shipY - rock->y;
            fixed r = _rockRadius[rock->size] + SHIP_R/2;
            if (DistSq(dx, dy) < FX_MUL(r,r))
            {
                Explode(memory->_shipX, memory->_shipY, 18, C_YELLOW);
                Explode(memory->_shipX, memory->_shipY, 8, C_RED);
                if (memory->_lives)
                    memory->_lives--;
                memory->_respawn = 45;
                memory->_thrusting = 0;
            }
        }
    }
    if (alive == 0 && !memory->_respawn && !memory->_gameOver)
        StartWave((uint8_t)(memory->_wave + 1));

    for (uint8_t i = 0; i < MAX_SPARKS; i++)
    {
        if (!memory->_sparks[i].life)
            continue;
        memory->_sparks[i].life--;
        memory->_sparks[i].x += memory->_sparks[i].vx;
        memory->_sparks[i].y += memory->_sparks[i].vy;
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

static int16_t ScreenX(fixed x) { return (int16_t)(GPU_X/2 + FX_TO_INT(x)); }
static int16_t ScreenY(fixed y) { return (int16_t)(GPU_Y/2 - FX_TO_INT(y)); }

static void DrawRock(Rock * rock, fixed offsetX, fixed offsetY)
{
    GPU3D_Mesh mesh;
    mesh.verts = rock->verts;
    mesh.nVerts = 6;
    mesh.faces = _rockFaces;
    mesh.nFaces = 8;
    Vec3 pos = GPU3D_Vec(rock->x + offsetX, rock->y + offsetY, ROCK_Z);
    GPU3D_DrawMeshTinted(&mesh, &pos, NULL, rock->rotX, rock->rotY, rock->rotZ,
                         _rockColor[rock->size], 1);
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    GPU_ClearFramebuffers();

    /* stars */
    for (uint8_t i = 0; i < 40; i++)
    {
        uint32_t seed = (uint32_t)i*2654435761u;
        int16_t sx = (int16_t)((seed >> 7) % GPU_X);
        int16_t sy = (int16_t)((seed >> 17) % GPU_Y);
        GPU_DrawPixel(((seed >> 3) & 1) ? C_GRAY_DARK : C_GRAY_MEDIUM, (uint16_t)sx, (uint16_t)sy);
    }

    GPU3D_Init();
    GPU3D_SetOrtho(GPU_X/2, GPU_Y/2, FX_ONE);
    GPU3D_SetCameraOrientation(0, 0, 0);
    GPU3D_SetCameraPosition(0, 0, 0);
    GPU3D_SetAmbient(256);

    for (uint8_t i = 0; i < MAX_ROCKS; i++)
    {
        Rock * rock = &memory->_rocks[i];
        if (!rock->active)
            continue;
        DrawRock(rock, 0, 0);
        /* a rock that touches a border is drawn on the other side as well */
        fixed r = _rockRadius[rock->size];
        if (rock->x > FX(FIELD_W) - r) DrawRock(rock, FX(-2*FIELD_W), 0);
        if (rock->x < FX(-FIELD_W) + r) DrawRock(rock, FX(2*FIELD_W), 0);
        if (rock->y > FX(FIELD_H) - r) DrawRock(rock, 0, FX(-2*FIELD_H));
        if (rock->y < FX(-FIELD_H) + r) DrawRock(rock, 0, FX(2*FIELD_H));
    }

    /* ship */
    if (!memory->_respawn && (!memory->_invulnerable || ((mainMemory._lastRenderedTick >> 2) & 1)))
    {
        Vec3 pos = GPU3D_Vec(memory->_shipX, memory->_shipY, ROCK_Z);
        GPU3D_DrawMeshTinted(&_shipMesh, &pos, NULL, ANG(14), 0, memory->_heading, C_WHITE, 1);
        if (memory->_thrusting && (mainMemory._lastRenderedTick & 1))
        {
            fixed bx = memory->_shipX - FX_MUL(FX(10.0), GPU3D_Cos(memory->_heading));
            fixed by = memory->_shipY - FX_MUL(FX(10.0), GPU3D_Sin(memory->_heading));
            fixed tx = bx - FX_MUL(FX(8.0), GPU3D_Cos(memory->_heading));
            fixed ty = by - FX_MUL(FX(8.0), GPU3D_Sin(memory->_heading));
            GPU_DrawLine(C_ORANGE, ScreenX(bx), ScreenY(by), ScreenX(tx), ScreenY(ty));
            GPU_DrawLine(C_YELLOW, ScreenX(bx), ScreenY(by),
                         ScreenX(bx - FX_MUL(FX(5.0), GPU3D_Cos(memory->_heading))),
                         ScreenY(by - FX_MUL(FX(5.0), GPU3D_Sin(memory->_heading))));
        }
    }

    for (uint8_t i = 0; i < MAX_BULLETS; i++)
    {
        if (!memory->_bullets[i].life)
            continue;
        GPU_DrawFilledCircle(C_YELLOW, ScreenX(memory->_bullets[i].x), ScreenY(memory->_bullets[i].y), 1);
    }

    for (uint8_t i = 0; i < MAX_SPARKS; i++)
    {
        if (!memory->_sparks[i].life)
            continue;
        int16_t x = ScreenX(memory->_sparks[i].x);
        int16_t y = ScreenY(memory->_sparks[i].y);
        if (x >= 0 && x < GPU_X && y >= 0 && y < GPU_Y)
            GPU_DrawPixel(memory->_sparks[i].color, (uint16_t)x, (uint16_t)y);
    }

    /* HUD */
    char text[24];
    int len = sprintf(text, "%6u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fb, 6, 3, text, (uint16_t)len);
    for (uint8_t i = 0; i < memory->_lives && i < 6; i++)
    {
        Vec3 pos = GPU3D_Vec(FX(-GPU_X/2 + 22 + i*18), FX(GPU_Y/2 - 26), ROCK_Z);
        GPU3D_DrawMeshTinted(&_shipMesh, &pos, NULL, ANG(14), 0, ANG(90), C_C1, 1);
    }
    len = sprintf(text, "WAVE %u", memory->_wave);
    GPU_DrawText(C_GRAY_LIGHT, fm, (int16_t)(GPU_X - len*fm->width - 6), 6, text, (uint16_t)len);

    if (memory->_waveBanner > 24)
    {
        len = sprintf(text, "WAVE %u", memory->_wave);
        GPU_DrawTextOutlined(C_CYAN, C_BLACK, fb, GPU_X/2, GPU_Y/2 - 60, text);
    }

    if (memory->_gameOver)
    {
        GPU_DrawPanel(RGB(8,8,32), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-44), 180, 90);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-38), "GAME OVER");
        len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-14), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G17_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
