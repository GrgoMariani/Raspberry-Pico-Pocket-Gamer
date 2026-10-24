// TUNNEL RUSH - fly through a spinning neon tunnel

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 40

#define TR_SEGMENTS 12                 /* angular resolution of the tunnel */
#define TR_RADIUS FX(3.0)
#define TR_INNER FX(1.05)              /* obstacles reach from the wall to here */
#define TR_RING_SPACING FX(2.0)
#define TR_RINGS 20
#define TR_PLAYER_Z FX(3.0)
#define TR_OBSTACLES 7
#define TR_OBSTACLE_SPACING FX(9.0)
#define TR_FAR FX(34.0)                /* do not draw anything behind this */
#define TR_START_LIVES 3
/* the tunnel is drawn with segments of ANG_FULL/TR_SEGMENTS degrees, so the
   segment a given angle falls into has to be computed the same way */
#define TR_SEGMENT_OF(angle) ((uint8_t)(((int32_t)(angle) / (ANG_FULL/TR_SEGMENTS)) % TR_SEGMENTS))

#define TR_STEP ANG(4)                 /* how fast the player turns */

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 30)

typedef enum {
    TS_READY = 0,
    TS_FLYING,
    TS_CRASHED,
    TS_GAME_OVER
} TunnelState;

typedef struct {
    fixed z;
    uint8_t gapStart;                  /* first free segment */
    uint8_t gapWidth;                  /* number of free segments */
    GPU_Color color;
    uint8_t passed;
} Obstacle;

typedef struct {
    Obstacle _obstacles[TR_OBSTACLES];
    int32_t _playerAngle;              /* in 1024ths of a turn */
    fixed _distance;
    fixed _speed;
    int32_t _roll;
    int8_t _rollDirection;
    uint32_t _rollSwitch;
    uint32_t _score;
    uint32_t _bonus;
    uint8_t _lives;
    TunnelState _state;
    uint8_t _stateTick;
} Game19Memory;

static Game19Memory * const memory = (Game19Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static const GPU_Color _obstacleColors[4] = { C_MAGENTA, C_YELLOW, C_CYAN, C_ORANGE };

/* ------------------------------------------------------------------- logic */

static void PlaceObstacle(Obstacle * o, fixed z)
{
    o->z = z;
    o->gapWidth = (uint8_t)(2 + Random_Range(2));       /* 2 or 3 free segments */
    o->gapStart = (uint8_t)Random_Range(TR_SEGMENTS);
    o->color = _obstacleColors[Random_Range(4)];
    o->passed = 0;
}

static void StartRun(uint8_t keepScore)
{
    if (!keepScore)
    {
        memory->_score = 0;
        memory->_bonus = 0;
    }
    memory->_distance = 0;
    memory->_speed = FX(0.16);
    memory->_playerAngle = ANG(90);
    memory->_roll = 0;
    memory->_rollDirection = 1;
    memory->_rollSwitch = 900;
    for (uint8_t i = 0; i < TR_OBSTACLES; i++)
        PlaceObstacle(&memory->_obstacles[i], FX(14.0) + FX_MUL(TR_OBSTACLE_SPACING, FX_INT(i)));
    memory->_state = TS_READY;
    memory->_stateTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game19Memory));
    memory->_lives = TR_START_LIVES;
    StartRun(0);
}

static uint8_t SegmentBlocked(const Obstacle * o, uint8_t segment)
{
    uint8_t offset = (uint8_t)((segment - o->gapStart) & (TR_SEGMENTS-1));
    return (uint8_t)(offset >= o->gapWidth);
}

static void Crash(void)
{
    memory->_state = TS_CRASHED;
    memory->_stateTick = 0;
    if (memory->_lives)
        memory->_lives--;
}

static void Tick(void)
{
    switch (memory->_state)
    {
    case TS_READY:
        memory->_stateTick++;
        if (memory->_stateTick > 25)
        {
            memory->_state = TS_FLYING;
            memory->_stateTick = 0;
        }
        break;
    case TS_FLYING:
    {
        KeyPressedEnum held = Keyboard_GetHeldKeys();
        if ((held & KEY_LEFT) || (mainMemory.touchPressed && mainMemory.touch_X < GPU_X/2))
            memory->_playerAngle += TR_STEP;
        if ((held & KEY_RIGHT) || (mainMemory.touchPressed && mainMemory.touch_X >= GPU_X/2))
            memory->_playerAngle -= TR_STEP;
        memory->_playerAngle &= (ANG_FULL-1);

        memory->_distance += memory->_speed;
        memory->_score = (uint32_t)FX_TO_INT(memory->_distance)*2 + memory->_bonus;

        /* the tunnel spins, and the spin flips from time to time */
        memory->_roll += memory->_rollDirection*(ANG(1) + (int32_t)(memory->_speed >> 14));
        memory->_roll &= (ANG_FULL-1);
        if (memory->_rollSwitch)
            memory->_rollSwitch--;
        else
        {
            memory->_rollDirection = (int8_t)-memory->_rollDirection;
            memory->_rollSwitch = 600 + (uint32_t)Random_Range(500);
        }

        /* speed slowly grows with the distance */
        if (memory->_speed < FX(0.55))
            memory->_speed += FX(0.00035);

        for (uint8_t i = 0; i < TR_OBSTACLES; i++)
        {
            Obstacle * o = &memory->_obstacles[i];
            o->z -= memory->_speed;
            /* the player just flew through it */
            if (!o->passed && o->z <= TR_PLAYER_Z)
            {
                o->passed = 1;
                uint8_t segment = TR_SEGMENT_OF(memory->_playerAngle);
                if (SegmentBlocked(o, segment))
                {
                    Crash();
                    return;
                }
                memory->_bonus += 50;
            }
            if (o->z < FX(1.0))
            {
                /* recycle to the far end of the tunnel */
                fixed furthest = 0;
                for (uint8_t j = 0; j < TR_OBSTACLES; j++)
                    if (memory->_obstacles[j].z > furthest)
                        furthest = memory->_obstacles[j].z;
                PlaceObstacle(o, furthest + TR_OBSTACLE_SPACING);
            }
        }
        break;
    }
    case TS_CRASHED:
        memory->_stateTick++;
        if (memory->_stateTick > 20)
        {
            if (memory->_lives == 0)
            {
                memory->_state = TS_GAME_OVER;
                memory->_stateTick = 0;
                mainMemory._gameState = GS_DONE;
            }
            else
                StartRun(1);
        }
        break;
    case TS_GAME_OVER:
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

typedef struct {
    int16_t x[TR_SEGMENTS+1];
    int16_t y[TR_SEGMENTS+1];
    uint8_t ok[TR_SEGMENTS+1];
} Ring;

static void ProjectRing(Ring * ring, fixed radius, fixed z)
{
    for (uint8_t i = 0; i <= TR_SEGMENTS; i++)
    {
        int32_t angle = (int32_t)(i*(ANG_FULL/TR_SEGMENTS));
        Vec3 world;
        world.x = FX_MUL(radius, GPU3D_Cos(angle));
        world.y = FX_MUL(radius, GPU3D_Sin(angle));
        world.z = z;
        Vec3 cam;
        GPU3D_WorldToCamera(&world, &cam);
        ring->ok[i] = GPU3D_Project(&cam, &ring->x[i], &ring->y[i]);
    }
}

static void DrawRingPolyline(const Ring * ring, GPU_Color color)
{
    for (uint8_t i = 0; i < TR_SEGMENTS; i++)
    {
        if (ring->ok[i] && ring->ok[i+1])
            GPU_DrawLine(color, ring->x[i], ring->y[i], ring->x[i+1], ring->y[i+1]);
    }
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    char text[24];
    int len;

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(4,4,24), C_BLACK, 0, 0, GPU_X, GPU_Y);

    GPU3D_Init();
    GPU3D_SetPerspective(GPU_X/2, GPU_Y/2, FX(140.0), FX(0.4));
    GPU3D_SetCameraOrientation(0, 0, memory->_roll);
    GPU3D_SetCameraPosition(0, 0, 0);
    GPU3D_SetLightDirection(FX(0.3), FX(0.6), FX(-0.7));
    GPU3D_SetAmbient(140);

    /* the tunnel: rings that scroll towards the camera plus a few long lines */
    Ring ring, ringFar;
    fixed scroll = memory->_distance % TR_RING_SPACING;
    for (int32_t r = TR_RINGS; r >= 0; r--)
    {
        fixed z = FX_MUL(TR_RING_SPACING, FX_INT(r)) + TR_RING_SPACING - scroll;
        if (z < FX(0.8) || z > TR_FAR)
            continue;
        int32_t depth = FX_TO_INT(z);
        GPU_Color color = GPU_ShadeColor(C_CYAN, (int16_t)(250 - depth*7));
        ProjectRing(&ring, TR_RADIUS, z);
        DrawRingPolyline(&ring, color);
        /* connect every second ring with the next one so the speed is visible */
        if ((r & 1) == 0 && z + TR_RING_SPACING <= TR_FAR)
        {
            ProjectRing(&ringFar, TR_RADIUS, z + TR_RING_SPACING);
            for (uint8_t i = 0; i <= TR_SEGMENTS; i += 2)
            {
                if (ring.ok[i] && ringFar.ok[i])
                    GPU_DrawLine(GPU_ShadeColor(color, 150), ring.x[i], ring.y[i],
                                 ringFar.x[i], ringFar.y[i]);
            }
        }
    }

    /* obstacles: a wall with a gap, drawn as a grid so that it stays cheap */
    for (uint8_t i = 0; i < TR_OBSTACLES; i++)
    {
        Obstacle * o = &memory->_obstacles[i];
        if (o->z < FX(0.9) || o->z > TR_FAR)
            continue;
        int32_t depth = FX_TO_INT(o->z);
        GPU_Color color = GPU_ShadeColor(o->color, (int16_t)(255 - depth*5));
        Ring outer, inner;
        ProjectRing(&outer, TR_RADIUS, o->z);
        ProjectRing(&inner, TR_INNER, o->z);
        for (uint8_t s = 0; s < TR_SEGMENTS; s++)
        {
            if (!SegmentBlocked(o, s))
                continue;
            uint8_t s2 = (uint8_t)(s+1);
            if (outer.ok[s] && outer.ok[s2])
                GPU_DrawLine(color, outer.x[s], outer.y[s], outer.x[s2], outer.y[s2]);
            if (inner.ok[s] && inner.ok[s2])
                GPU_DrawLine(GPU_ShadeColor(color, 170), inner.x[s], inner.y[s], inner.x[s2], inner.y[s2]);
            if (outer.ok[s] && inner.ok[s])
                GPU_DrawLine(color, outer.x[s], outer.y[s], inner.x[s], inner.y[s]);
        }
    }

    /* the player */
    Vec3 pos;
    pos.x = FX_MUL(FX_MUL(TR_RADIUS, FX(0.55)), GPU3D_Cos(memory->_playerAngle));
    pos.y = FX_MUL(FX_MUL(TR_RADIUS, FX(0.55)), GPU3D_Sin(memory->_playerAngle));
    pos.z = TR_PLAYER_Z;
    if (memory->_state == TS_CRASHED)
    {
        for (uint8_t i = 0; i < 10; i++)
        {
            Vec3 piece = pos;
            int32_t a = (int32_t)(i*(ANG_FULL/10)) + memory->_stateTick*ANG(9);
            piece.x += FX_MUL(FX_MUL(FX(0.2), FX_INT(memory->_stateTick)), GPU3D_Cos(a));
            piece.y += FX_MUL(FX_MUL(FX(0.2), FX_INT(memory->_stateTick)), GPU3D_Sin(a));
            piece.z -= FX_MUL(FX(0.1), FX_INT(memory->_stateTick));
            GPU3D_DrawBox((i & 1) ? C_YELLOW : C_ORANGE, &piece, FX(0.18), FX(0.18), FX(0.18),
                          a, a/2, 0, 0);
        }
    }
    else
    {
        /* the pyramid is turned so that its tip points into the tunnel */
        GPU3D_DrawMeshTinted(&GPU3D_PyramidMesh, &pos, NULL, ANG(90), 0,
                             (int32_t)(memory->_playerAngle - ANG(90)), C_WHITE, 0);
        Vec3 body = pos;
        body.z -= FX(0.35);
        GPU3D_DrawBox(C_C1, &body, FX(0.3), FX(0.3), FX(0.4), 0, 0,
                      (int32_t)(memory->_playerAngle - ANG(90)), 0);
    }

    /* HUD */
    GPU_DrawPanel(RGB(6,6,30), C_C2, 0, 0, GPU_X, 22);
    len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)len);
    len = sprintf(text, "SPEED %3d", (int)FX_TO_INT(FX_MUL(memory->_speed, FX_INT(100))));
    GPU_DrawText(C_CYAN, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 4, text, (uint16_t)len);
    for (uint8_t i = 0; i < memory->_lives && i < 4; i++)
        GPU_DrawFilledCircle(C_MAGENTA, (int16_t)(GPU_X - 54 + i*15), 11, 5);
    {
        const char * legend = "LEFT/RIGHT turn (or touch left/right half)";
        GPU_DrawText(C_GRAY_DARK, fm, 6, (int16_t)(GPU_Y-14), legend, (uint16_t)strlen(legend));
    }

    if (memory->_state == TS_READY)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fh, GPU_X/2, (int16_t)(GPU_Y/2-60), "TUNNEL RUSH");
    else if (memory->_state == TS_CRASHED)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-60), "CRASH");
    else if (memory->_state == TS_GAME_OVER)
    {
        GPU_DrawPanel(RGB(6,6,30), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-44), 180, 92);
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

const Callbacks G19_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
