// FROGGER - crossing the road and the river, seen from a 3d camera

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 30

#define LANES 14                 /* 0 start, 1..6 road, 7 median, 8..12 river, 13 homes */
#define ROAD_START 1
#define ROAD_END 6
#define RIVER_START 8
#define RIVER_END 12
#define HOME_LANE 13
#define HOME_SLOTS 5

#define FIELD_HALF 6             /* the frog walks from -6 to +6 */
#define LANE_DEPTH FX(1.0)
#define MAX_CARS 10
#define MAX_LOGS 10
#define HOP_TICKS 5
#define START_LIVES 3
#define START_TIME (30*NUM_TICKS)

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 30)

typedef struct {
    fixed x;
    uint8_t lane;
    uint8_t length;      /* in field units */
    fixed speed;         /* signed, per tick */
    GPU_Color color;
} Mover;

typedef enum {
    FS_READY = 0,
    FS_PLAYING,
    FS_HOPPING,
    FS_DYING,
    FS_HOME,
    FS_GAME_OVER
} FrogState;

typedef struct {
    Mover _cars[MAX_CARS];
    Mover _logs[MAX_LOGS];
    uint8_t _homes[HOME_SLOTS];
    fixed _frogX;
    int8_t _frogLane;
    fixed _hopFromX;
    int8_t _hopFromLane;
    fixed _hopToX;
    int8_t _hopToLane;
    uint8_t _hopTick;
    uint8_t _riding;
    FrogState _state;
    uint8_t _stateTick;
    uint8_t _deathKind;    /* 0 = car, 1 = water, 2 = timeout, 3 = lost */
    uint8_t _lives;
    uint32_t _score;
    uint8_t _level;
    uint16_t _timeLeft;
} Game18Memory;

static Game18Memory * const memory = (Game18Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* the road, the river and the verges all use the shared horizontal quad */
#define _quadMesh GPU3D_QuadMesh

static const fixed _homeX[HOME_SLOTS] = { FX(-5), FX(-2.5), FX(0), FX(2.5), FX(5) };

/* --------------------------------------------------------------- level set up */

static void BuildLevel(uint8_t level)
{
    uint8_t car = 0, log = 0;
    fixed speedUp = FX(0.02)*(int32_t)(level-1);
    if (speedUp > FX(0.06))
        speedUp = FX(0.06);
    static const GPU_Color carColors[6] = { C_RED, C_YELLOW, C_C1, C_ORANGE, C_MAGENTA, C_CYAN };
    for (int32_t lane = ROAD_START; lane <= ROAD_END; lane++)
    {
        uint8_t count = (lane == ROAD_START+2) ? 3 : 2;
        fixed speed = FX(0.035) + FX(0.012)*(int32_t)((lane*7) % 4) + speedUp;
        int8_t dir = (lane & 1) ? 1 : -1;
        for (uint8_t c = 0; c < count && car < MAX_CARS; c++)
        {
            Mover * m = &memory->_cars[car++];
            m->lane = (uint8_t)lane;
            m->length = 2;
            m->speed = speed*dir;
            m->color = carColors[(lane + c) % 6];
            m->x = FX(-8) + FX(16.0/2)*(int32_t)c + (fixed)Random_Range((uint32_t)FX(3.0));
        }
    }
    for (int32_t lane = RIVER_START; lane <= RIVER_END; lane++)
    {
        uint8_t count = (lane == RIVER_START+2) ? 3 : 2;
        fixed speed = FX(0.025) + FX(0.010)*(int32_t)((lane*5) % 3) + speedUp/2;
        int8_t dir = (lane & 1) ? -1 : 1;
        for (uint8_t l = 0; l < count && log < MAX_LOGS; l++)
        {
            Mover * m = &memory->_logs[log++];
            m->lane = (uint8_t)lane;
            m->length = (lane & 1) ? 3 : 4;
            m->speed = speed*dir;
            m->color = RGB(140, 90, 30);
            m->x = FX(-9) + FX(18.0/(count))*(int32_t)l + (fixed)Random_Range((uint32_t)FX(2.0));
        }
    }
    for (uint8_t i = car; i < MAX_CARS; i++)
        memory->_cars[i].length = 0;
    for (uint8_t i = log; i < MAX_LOGS; i++)
        memory->_logs[i].length = 0;
}

static void PlaceFrog(void)
{
    memory->_frogX = 0;
    memory->_frogLane = 0;
    memory->_hopTick = 0;
    memory->_riding = 0;
    memory->_timeLeft = START_TIME;
    memory->_state = FS_READY;
    memory->_stateTick = 0;
}

static void StartLevel(uint8_t level)
{
    memory->_level = level;
    BuildLevel(level);
    for (uint8_t i = 0; i < HOME_SLOTS; i++)
        memory->_homes[i] = 0;
    PlaceFrog();
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game18Memory));
    memory->_lives = START_LIVES;
    memory->_score = 0;
    StartLevel(1);
}

/* ------------------------------------------------------------------ logic */

static uint8_t LaneIsRoad(uint8_t lane)  { return (uint8_t)(lane >= ROAD_START && lane <= ROAD_END); }
static uint8_t LaneIsRiver(uint8_t lane) { return (uint8_t)(lane >= RIVER_START && lane <= RIVER_END); }

static Mover * LogUnderFrog(void)
{
    for (uint8_t i = 0; i < MAX_LOGS; i++)
    {
        Mover * m = &memory->_logs[i];
        if (!m->length || m->lane != (uint8_t)memory->_frogLane)
            continue;
        fixed half = FX((int32_t)m->length)/2;
        if (memory->_frogX + FX(0.3) > m->x - half && memory->_frogX - FX(0.3) < m->x + half)
            return m;
    }
    return NULL;
}

static uint8_t CarHitsFrog(void)
{
    for (uint8_t i = 0; i < MAX_CARS; i++)
    {
        Mover * m = &memory->_cars[i];
        if (!m->length || m->lane != (uint8_t)memory->_frogLane)
            continue;
        fixed half = FX((int32_t)m->length)/2;
        if (memory->_frogX + FX(0.3) > m->x - half && memory->_frogX - FX(0.3) < m->x + half)
            return 1;
    }
    return 0;
}

static void Kill(uint8_t kind)
{
    memory->_deathKind = kind;
    memory->_state = FS_DYING;
    memory->_stateTick = 0;
}

static void StartHop(int8_t dLane, int8_t dX)
{
    memory->_hopFromX = memory->_frogX;
    memory->_hopFromLane = memory->_frogLane;
    fixed targetX = memory->_frogX + FX_INT(dX);
    int8_t targetLane = (int8_t)(memory->_frogLane + dLane);
    if (targetLane < 0) targetLane = 0;
    if (targetLane > HOME_LANE) targetLane = HOME_LANE;
    if (targetX > FX(FIELD_HALF)) targetX = FX(FIELD_HALF);
    if (targetX < FX(-FIELD_HALF)) targetX = FX(-FIELD_HALF);
    memory->_hopToX = targetX;
    memory->_hopToLane = targetLane;
    memory->_hopTick = 1;
    if (dLane > 0)
        memory->_score += 10;
}

static void FinishHop(void)
{
    memory->_hopTick = 0;
    memory->_frogX = memory->_hopToX;
    memory->_frogLane = memory->_hopToLane;

    if (memory->_frogLane == HOME_LANE)
    {
        /* one of the five homes has to be free and close by */
        for (uint8_t i = 0; i < HOME_SLOTS; i++)
        {
            if (memory->_homes[i])
                continue;
            fixed d = memory->_frogX - _homeX[i];
            if (d < 0) d = -d;
            if (d < FX(1.2))
            {
                memory->_homes[i] = 1;
                memory->_score += 100 + memory->_timeLeft/10;
                memory->_state = FS_HOME;
                memory->_stateTick = 0;
                uint8_t all = 1;
                for (uint8_t j = 0; j < HOME_SLOTS; j++)
                    if (!memory->_homes[j])
                        all = 0;
                if (all)
                {
                    memory->_score += 1000;
                    memory->_stateTick = 20;      /* show the full row a moment longer */
                }
                return;
            }
        }
        Kill(3);
        return;
    }
    if (LaneIsRoad((uint8_t)memory->_frogLane) && CarHitsFrog())
    {
        Kill(0);
        return;
    }
    if (LaneIsRiver((uint8_t)memory->_frogLane))
    {
        Mover * log = LogUnderFrog();
        if (!log)
        {
            Kill(1);
            return;
        }
        memory->_riding = 1;
    }
    else
    {
        memory->_riding = 0;
    }
}

static void LoseLife(void)
{
    if (memory->_lives)
        memory->_lives--;
    if (memory->_lives == 0)
    {
        memory->_state = FS_GAME_OVER;
        memory->_stateTick = 0;
        mainMemory._gameState = GS_DONE;
        return;
    }
    PlaceFrog();
}

static void MoveTraffic(void)
{
    for (uint8_t i = 0; i < MAX_CARS; i++)
    {
        Mover * m = &memory->_cars[i];
        if (!m->length) continue;
        m->x += m->speed;
        if (m->x > FX(FIELD_HALF + 4)) m->x = FX(-FIELD_HALF - 4);
        if (m->x < FX(-FIELD_HALF - 4)) m->x = FX(FIELD_HALF + 4);
    }
    for (uint8_t i = 0; i < MAX_LOGS; i++)
    {
        Mover * m = &memory->_logs[i];
        if (!m->length) continue;
        m->x += m->speed;
        if (m->x > FX(FIELD_HALF + 5)) m->x = FX(-FIELD_HALF - 5);
        if (m->x < FX(-FIELD_HALF - 5)) m->x = FX(FIELD_HALF + 5);
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

/* screen position of the frog, also used to interpret touches */
static void FrogScreen(int16_t * sx, int16_t * sy);

static void Tick(void)
{
    switch (memory->_state)
    {
    case FS_READY:
        memory->_stateTick++;
        if (memory->_stateTick > 15)
        {
            memory->_state = FS_PLAYING;
            memory->_stateTick = 0;
        }
        break;
    case FS_PLAYING:
    {
        MoveTraffic();
        /* the frog drifts with its log */
        if (memory->_riding)
        {
            Mover * log = LogUnderFrog();
            if (!log)
            {
                Kill(1);
                break;
            }
            memory->_frogX += log->speed;
            if (memory->_frogX > FX(FIELD_HALF) || memory->_frogX < FX(-FIELD_HALF))
            {
                Kill(3);
                break;
            }
        }
        if (memory->_timeLeft)
            memory->_timeLeft--;
        if (memory->_timeLeft == 0)
        {
            Kill(2);
            break;
        }
        if (LaneIsRoad((uint8_t)memory->_frogLane) && CarHitsFrog())
        {
            Kill(0);
            break;
        }
        /* input */
        int8_t dLane = 0, dX = 0;
        KeyPressedEnum keys = Keyboard_GetPressedKeys();
        if (keys & KEY_UP)         dLane = 1;
        else if (keys & KEY_DOWN)  dLane = -1;
        else if (keys & KEY_LEFT)  dX = -1;
        else if (keys & KEY_RIGHT) dX = 1;
        if (dLane == 0 && dX == 0 && mainMemory.touchPressedNew)
        {
            int16_t fx, fy;
            FrogScreen(&fx, &fy);
            int32_t dx = (int32_t)mainMemory.touch_X - fx;
            int32_t dy = (int32_t)mainMemory.touch_Y - fy;
            if (dx*dx + dy*dy > 100)
            {
                if ((dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx))
                    dLane = (dy < 0) ? 1 : -1;
                else
                    dX = (dx < 0) ? -1 : 1;
            }
        }
        if (dLane || dX)
        {
            StartHop(dLane, dX);
            memory->_state = FS_HOPPING;
        }
        break;
    }
    case FS_HOPPING:
        /* the traffic keeps moving while the frog is in the air */
        MoveTraffic();
        memory->_hopTick++;
        if (memory->_hopTick > HOP_TICKS)
        {
            FinishHop();
            if (memory->_state == FS_HOPPING)
                memory->_state = FS_PLAYING;
        }
        break;
    case FS_DYING:
        memory->_stateTick++;
        if (memory->_stateTick > 18)
            LoseLife();
        break;
    case FS_HOME:
        memory->_stateTick++;
        if (memory->_stateTick > 30)
        {
            uint8_t all = 1;
            for (uint8_t i = 0; i < HOME_SLOTS; i++)
                if (!memory->_homes[i])
                    all = 0;
            if (all)
                StartLevel((uint8_t)(memory->_level + 1));
            else
                PlaceFrog();
        }
        break;
    case FS_GAME_OVER:
        memory->_stateTick++;
        break;
    default:
        break;
    }
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

static void SetupCamera(void)
{
    GPU3D_Init();
    GPU3D_SetPerspective(GPU_X/2, 168, FX(300.0), FX(0.5));
    GPU3D_SetCameraOrientation(ANG(52), 0, 0);
    GPU3D_SetCameraPosition(0, FX(13.0), FX(-9.0));
    GPU3D_SetLightDirection(FX(-0.3), FX(0.8), FX(-0.5));
    GPU3D_SetAmbient(120);
}

static Vec3 WorldPos(fixed x, int32_t lane, fixed height)
{
    Vec3 p;
    p.x = x;
    p.y = height;
    p.z = FX_MUL(LANE_DEPTH, FX_INT(lane));
    return p;
}

static void Project(const Vec3 * world, int16_t * sx, int16_t * sy)
{
    Vec3 cam;
    GPU3D_WorldToCamera(world, &cam);
    if (!GPU3D_Project(&cam, sx, sy))
    {
        *sx = -200;
        *sy = -200;
    }
}

static void FrogScreen(int16_t * sx, int16_t * sy)
{
    SetupCamera();
    Vec3 p = WorldPos(memory->_frogX, memory->_frogLane, FX(0.3));
    Project(&p, sx, sy);
}

static void DrawGround(void)
{
    Vec3 scale = GPU3D_Vec(FX(2*FIELD_HALF + 3), FX_ONE, LANE_DEPTH);
    for (int32_t lane = 0; lane <= HOME_LANE; lane++)
    {
        Vec3 p = WorldPos(0, lane, 0);
        GPU_Color color;
        if (lane == 0 || lane == 7)
            color = RGB(30, 120, 30);
        else if (LaneIsRoad((uint8_t)lane))
            color = (lane & 1) ? RGB(48, 48, 52) : RGB(40, 40, 46);
        else if (LaneIsRiver((uint8_t)lane))
            color = (uint8_t)(((IF_GetCurrentTime()/250000) + lane) & 1) ? RGB(20, 60, 170) : RGB(24, 70, 190);
        else
            color = RGB(20, 90, 20);
        GPU3D_DrawMeshTinted(&_quadMesh, &p, &scale, 0, 0, 0, color, 0);
        /* lane markings on the road */
        if (LaneIsRoad((uint8_t)lane) && (lane & 1))
        {
            Vec3 lineScale = GPU3D_Vec(FX(2*FIELD_HALF + 3), FX_ONE, FX(0.06));
            Vec3 lp = p;
            lp.z += LANE_DEPTH/2;
            lp.y = FX(0.01);
            GPU3D_DrawMeshTinted(&_quadMesh, &lp, &lineScale, 0, 0, 0, C_YELLOW, 0);
        }
    }
}

static void DrawHomes(void)
{
    Vec3 scale = GPU3D_Vec(FX(2*FIELD_HALF + 3), FX(0.6), LANE_DEPTH);
    Vec3 p = WorldPos(0, HOME_LANE, FX(0.3));
    GPU3D_DrawMeshTinted(&_quadMesh, &p, &scale, 0, 0, 0, RGB(16, 70, 16), 0);
    for (uint8_t i = 0; i < HOME_SLOTS; i++)
    {
        Vec3 slot = WorldPos(_homeX[i], HOME_LANE, FX(0.32));
        Vec3 slotScale = GPU3D_Vec(FX(1.7), FX(0.5), FX(0.85));
        GPU3D_DrawBox(memory->_homes[i] ? C_GREEN : RGB(0, 40, 0), &slot,
                      slotScale.x, slotScale.y, slotScale.z, 0, 0, 0, 0);
    }
}

/* where the frog is right now, including the hop interpolation */
static Vec3 FrogPosition(void)
{
    Vec3 p = WorldPos(memory->_frogX, memory->_frogLane, FX(0.3));
    if (memory->_hopTick)
    {
        fixed t = FX((int32_t)memory->_hopTick - 1)/HOP_TICKS;
        p.x = memory->_hopFromX + FX_MUL(t, memory->_hopToX - memory->_hopFromX);
        p.z = FX_MUL(LANE_DEPTH, FX_INT(memory->_hopFromLane))
            + FX_MUL(t, FX_MUL(LANE_DEPTH, FX_INT(memory->_hopToLane - memory->_hopFromLane)));
        fixed arc = FX_MUL(FX(4.0), FX_MUL(t, FX_ONE - t));
        p.y += FX_MUL(arc, FX(0.7));
    }
    return p;
}

/* the lane the frog is drawn in, so that it stays in the right depth order */
static int32_t FrogDrawLane(void)
{
    if (!memory->_hopTick)
        return memory->_frogLane;
    fixed t = FX((int32_t)memory->_hopTick - 1)/HOP_TICKS;
    fixed lane = FX_INT(memory->_hopFromLane)
               + FX_MUL(t, FX_INT(memory->_hopToLane - memory->_hopFromLane));
    return FX_TO_INT(lane + FX(0.5));
}

static void DrawMovers(void)
{
    /* far lanes first */
    for (int32_t lane = HOME_LANE; lane >= 0; lane--)
    {
        for (uint8_t i = 0; i < MAX_CARS; i++)
        {
            Mover * m = &memory->_cars[i];
            if (!m->length || m->lane != (uint8_t)lane)
                continue;
            Vec3 p = WorldPos(m->x, lane, FX(0.35));
            GPU3D_DrawBox(m->color, &p, FX((int32_t)m->length - 0.25), FX(0.6), FX(0.8), 0, 0, 0, 0);
            /* a little cabin so that it reads as a car */
            Vec3 cabin = p;
            cabin.y += FX(0.35);
            GPU3D_DrawBox(GPU_ShadeColor(m->color, 200), &cabin, FX((int32_t)m->length - 1.2), FX(0.35), FX(0.6), 0, 0, 0, 0);
        }
        for (uint8_t i = 0; i < MAX_LOGS; i++)
        {
            Mover * m = &memory->_logs[i];
            if (!m->length || m->lane != (uint8_t)lane)
                continue;
            Vec3 p = WorldPos(m->x, lane, FX(0.18));
            GPU3D_DrawBox(m->color, &p, FX((int32_t)m->length - 0.1), FX(0.35), FX(0.8), 0, 0, 0, 0);
            GPU3D_DrawBox(GPU_ShadeColor(m->color, 140), &p, FX((int32_t)m->length - 0.1), FX(0.1), FX(0.5), 0, 0, 0, 0);
        }
        if (lane == FrogDrawLane())
        {
            Vec3 p = FrogPosition();
            if (memory->_state == FS_DYING)
            {
                p.y -= FX_MUL(FX(0.05), FX_INT(memory->_stateTick));
                if (memory->_deathKind == 1)
                    p.y = FX(0.1);
            }
            GPU_Color frogColor = (memory->_state == FS_DYING) ? C_RED : C_GREEN;
            fixed squash = (memory->_state == FS_DYING) ? FX(0.4) : FX(0.7);
            GPU3D_DrawBox(frogColor, &p, FX(0.7), squash, FX(0.7), 0, 0, 0, 0);
            Vec3 head = p;
            head.y += FX(0.3);
            head.z += FX(0.2);
            GPU3D_DrawBox(frogColor, &head, FX(0.5), FX(0.35), FX(0.4), 0, 0, 0, 0);
            int16_t ex, ey;
            Project(&head, &ex, &ey);
            if (ex > 0)
            {
                GPU_DrawFilledCircle(C_WHITE, (int16_t)(ex-3), (int16_t)(ey-2), 2);
                GPU_DrawFilledCircle(C_WHITE, (int16_t)(ex+3), (int16_t)(ey-2), 2);
            }
        }
    }
}

static void DrawHud(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    char text[24];
    int len;
    GPU_DrawPanel(RGB(8,24,8), C_GREEN, 0, 0, GPU_X, 22);
    len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)len);
    len = sprintf(text, "LEVEL %u", memory->_level);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 4, text, (uint16_t)len);
    for (uint8_t i = 0; i < memory->_lives && i < 4; i++)
        GPU_DrawFilledCircle(C_GREEN, (int16_t)(GPU_X - 60 + i*14), 11, 5);
    /* the clock */
    int16_t barW = (int16_t)((int32_t)memory->_timeLeft*(GPU_X-16)/START_TIME);
    GPU_DrawFilledSquare(memory->_timeLeft < START_TIME/4 ? C_RED : C_CYAN, 8, (int16_t)(GPU_Y-8), barW, 4);
    GPU_DrawEmptySquare(C_GRAY_DARK, 8, (int16_t)(GPU_Y-8), GPU_X-16, 4);
    const char * legend = "UP forward  LEFT/RIGHT side  DOWN back  (or touch)";
    GPU_DrawText(C_GRAY_MEDIUM, fs, 8, (int16_t)(GPU_Y-20), legend, (uint16_t)strlen(legend));
}

static void G_Draw(void)
{
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,20,60), RGB(4,8,20), 0, 0, GPU_X, GPU_Y);

    SetupCamera();
    DrawGround();
    DrawHomes();
    DrawMovers();
    DrawHud();

    if (memory->_state == FS_READY)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 34, "GO!");
    else if (memory->_state == FS_DYING)
    {
        const char * msg = "OUCH";
        if (memory->_deathKind == 1) msg = "SPLASH";
        else if (memory->_deathKind == 2) msg = "TIME OUT";
        else if (memory->_deathKind == 3) msg = "LOST";
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, 34, msg);
    }
    else if (memory->_state == FS_HOME)
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 34, "HOME!");
    else if (memory->_state == FS_GAME_OVER)
    {
        GPU_DrawPanel(RGB(8,24,8), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-44), 180, 92);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fh, GPU_X/2, (int16_t)(GPU_Y/2-40), "GAME OVER");
        char text[24];
        sprintf(text, "SCORE %6u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, (int16_t)(GPU_Y/2-8), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G18_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
