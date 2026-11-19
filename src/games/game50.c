// WAGON ORDER - a fully 3d educational train game for ages 5-7.
//
// Drive the steam loco from station to station. At every station the train
// stops and a PUZZLE opens that the child must solve to load a cargo wagon:
// counting, picture addition & subtraction, completing patterns, odd-one-out,
// missing numbers, bigger/smaller and letters. Solve it and a wagon of goods
// slides onto the train; fill the order (3-4 wagons) to deliver and roll on.
// The puzzles ARE the challenge - driving is the fun part in between.
//
// The whole yard is 3d (ground, tracks, sleepers, trees, poles, mountains,
// stations, coal, tender and a detailed locomotive with rotating spoked
// wheels and coupling rods). The far siding is scenery and scrolls slower
// than the main line for a proper parallax depth cue.
//
// RIGHT powers, LEFT brakes and reverses, DOWN toots the whistle. Steam is
// spent under throttle and built back up while coasting; coal lumps refill
// the tender. Puzzles have no timer and no fail state - a wrong answer just
// says "try again" and stays, so it stays encouraging in a classroom.

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 30
#define WO_FRONT_MX 2.6f
#define WO_ACCEL 66.0f
#define WO_BRAKE 150.0f
#define WO_REV_ACCEL 50.0f
#define WO_FRICTION 12.0f
#define WO_MAX_REV (-80.0f)
#define WO_NUM_PUFFS 12
#define WO_NUM_COAL 6
#define WO_MAX_CARGO 4
#define WO_SIDING_Z 4.5f
#define WO_SIDING_SCROLL 0.5f    /* the far track scrolls slower (parallax) */
#define WO_STATION_ZONE 70.0f    /* generous stop zone for young engineers */
#define WO_STOP_ASSIST 220.0f    /* coasting this close to a station glides to a halt */

/* ------------------------------------------------------------- puzzle types */

typedef enum {
    PC_COUNT = 0, PC_ADD, PC_SUB, PC_PATTERN, PC_ODD, PC_MISSING, PC_BIG, PC_LETTER
} PuzzleType;

#define PC_KINDS 8

typedef struct {
    uint8_t type;
    uint8_t phase;               /* 0 question, 1 correct pause, 2 wrong flash */
    uint8_t phaseTicks;
    uint8_t cursor;
    uint8_t wrongIdx;
    uint8_t nChoices;
    uint8_t correct;
    uint8_t choiceKind;          /* 0 number, 1 shape element, 2 letter */
    uint8_t val[4];              /* numbers or letter indices */
    uint8_t cshape[4], ccolor[4];/* element choices */
    uint8_t a, b, objType;       /* count / add / sub */
    uint8_t seqShape[6], seqColor[6], seqLen;   /* pattern */
    uint8_t seqVal[6], gapIdx;   /* missing number */
    uint8_t big;                 /* bigger/smaller */
    uint8_t letter, letterMode;  /* letter puzzle */
    uint32_t seed;
} Puzzle;

typedef struct {
    uint8_t item, load;          /* goods drawn on the wagon */
    uint8_t state;               /* 1 sliding on, 2 coupled */
    uint8_t slideT;
    uint8_t slot;
} Cargo;

typedef struct { float x, y; uint8_t age; } Puff;
typedef struct { float worldX; uint8_t taken; } Coal;

typedef struct {
    float dist, v;
    float steam;
    Puzzle pz;
    uint8_t puzzleActive;
    Cargo cargo[WO_MAX_CARGO];
    uint8_t numCargo;
    uint8_t loaded;              /* wagons loaded this order */
    uint8_t target;              /* wagons needed to deliver */
    float stationX;
    uint8_t stationDone;
    uint8_t missedNotified;      /* said "oops" once after passing it */
    uint8_t people;              /* friends waiting on the platform */
    uint8_t celebrate;
    uint8_t level;
    uint16_t deliveries;
    uint16_t stars;
    uint16_t solved;             /* puzzles solved overall (difficulty) */
    uint32_t score;
    uint8_t state;               /* 0 ready, 1 playing */
    uint8_t popupTicks;
    uint8_t popupType;           /* 0 loaded, 1 missed, 2 slow?, 3 delivered, 5 coal, 6 toot */
    uint8_t whistleTicks;
    uint8_t animTick;
    uint8_t braking;
    Coal coal[WO_NUM_COAL];
    Puff puffs[WO_NUM_PUFFS];
} Game50Memory;

static Game50Memory * const memory = (Game50Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static const GPU_Color _colorVal[6] = { C_RED, C_C1, C_GREEN, C_YELLOW, C_ORANGE, C_PURPLE };
static const char * const _words[20] = {
    "CAT", "DOG", "SUN", "HAT", "PIG", "COW", "BUS", "PEN", "BOX", "CUP",
    "JAM", "MAP", "FOX", "BEE", "ANT", "OWL", "EGG", "RAM", "LOG", "BED"
};
static const char * const _puzzleName[PC_KINDS] = {
    "COUNT THEM", "ADD THEM", "TAKE AWAY", "WHAT'S NEXT?",
    "ODD ONE OUT", "MISSING NUMBER", "BIG OR SMALL", "LETTERS"
};

/* ---------------------------------------------------------------- 3d meshes */

static const Vec3 _cylVerts[18] = {
    { FX(0.5), 0, FX(-0.5) }, { FX(0.354), FX(0.354), FX(-0.5) },
    { 0, FX(0.5), FX(-0.5) }, { FX(-0.354), FX(0.354), FX(-0.5) },
    { FX(-0.5), 0, FX(-0.5) }, { FX(-0.354), FX(-0.354), FX(-0.5) },
    { 0, FX(-0.5), FX(-0.5) }, { FX(0.354), FX(-0.354), FX(-0.5) },
    { FX(0.5), 0, FX(0.5) }, { FX(0.354), FX(0.354), FX(0.5) },
    { 0, FX(0.5), FX(0.5) }, { FX(-0.354), FX(0.354), FX(0.5) },
    { FX(-0.5), 0, FX(0.5) }, { FX(-0.354), FX(-0.354), FX(0.5) },
    { 0, FX(-0.5), FX(0.5) }, { FX(0.354), FX(-0.354), FX(0.5) },
    { 0, 0, FX(-0.5) }, { 0, 0, FX(0.5) }
};
static const GPU3D_Face _cylFaces[16] = {
    { 4, {0,1,9,8},   {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {1,2,10,9},  {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {2,3,11,10}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {3,4,12,11}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {4,5,13,12}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {5,6,14,13}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {6,7,15,14}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {7,0,8,15},  {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {16,1,0}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,2,1}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,3,2}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,4,3}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,5,4}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,6,5}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,7,6}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 3, {16,0,7}, {0}, {0}, C_WHITE, 0, 0, 0 }
};
static const GPU3D_Mesh _cylMesh = { _cylVerts, 18, _cylFaces, 16 };

static const Vec3 _flatVerts[8] = {
    { FX(-0.5), FX(-0.5), FX(-0.5) }, { FX(0.5), FX(-0.5), FX(-0.5) },
    { FX(0.5), FX(0.5), FX(-0.5) },   { FX(-0.5), FX(0.5), FX(-0.5) },
    { FX(-0.5), FX(-0.5), FX(0.5) },  { FX(0.5), FX(-0.5), FX(0.5) },
    { FX(0.5), FX(0.5), FX(0.5) },    { FX(-0.5), FX(0.5), FX(0.5) }
};
static const GPU3D_Face _flatFaces[6] = {
    { 4, {0,3,2,1}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 4, {4,5,6,7}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 4, {0,4,7,3}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 4, {1,2,6,5}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 4, {0,1,5,4}, {0}, {0}, C_WHITE, 0, 0, 0 },
    { 4, {3,7,6,2}, {0}, {0}, C_WHITE, 0, 0, 0 }
};
static const GPU3D_Mesh _flatMesh = { _flatVerts, 8, _flatFaces, 6 };

static void FlatBox(GPU_Color col, fixed x, fixed y, fixed z, fixed sx, fixed sy, fixed sz)
{
    Vec3 p = GPU3D_Vec(x, y, z);
    Vec3 s = GPU3D_Vec(sx, sy, sz);
    GPU3D_DrawMeshTinted(&_flatMesh, &p, &s, 0, 0, 0, col, 0);
}

/* ------------------------------------------------------------------- camera */

#define WO_3D_CENTER_X 244
#define WO_3D_CENTER_Y 196
#define WO_3D_SCALE FX(22.0)
#define WO_3D_PITCH ANG(24)
#define WO_3D_YAW ANG(-20)

static void Setup3D(uint8_t night)
{
    GPU3D_Init();
    GPU3D_SetOrtho(WO_3D_CENTER_X, WO_3D_CENTER_Y, WO_3D_SCALE);
    GPU3D_SetCameraOrientation(WO_3D_PITCH, WO_3D_YAW, 0);
    GPU3D_SetCameraPosition(0, 0, 0);
    GPU3D_SetLightDirection(FX(-0.45), FX(0.80), FX(-0.40));
    GPU3D_SetAmbient(night ? 75 : 115);
}

static uint8_t Project3D(fixed x, fixed y, fixed z, int16_t * sx, int16_t * sy)
{
    Vec3 w = GPU3D_Vec(x, y, z);
    Vec3 c;
    GPU3D_WorldToCamera(&w, &c);
    return GPU3D_Project(&c, sx, sy);
}

static int32_t WheelAngle(void)
{
    return (int32_t)memory->dist * 15;
}

static int32_t Wrap(int32_t a, int32_t m)
{
    int32_t r = a % m;
    return r < 0 ? r + m : r;
}

static float ScrollX(int32_t seed, float factor)
{
    int32_t d = (int32_t)(memory->dist*factor);
    int32_t x = Wrap(seed - d, 20*16);
    return (float)x/16.0f - 14.0f;
}

/* world positions are stored in pixels; the track/scenery use 16 px per model
   unit, so a relative pixel offset becomes a model x with the SAME factor -
   otherwise stations and coal would scroll ~20x faster than the world */
static float RelModel(float relPixels)
{
    return relPixels/16.0f + WO_FRONT_MX;
}

/* ------------------------------------------------------------------ vehicles */

static void DrawWheel3D(fixed x, fixed y, fixed z, fixed r, int32_t angle,
                        GPU_Color col, uint8_t spokes)
{
    Vec3 p = GPU3D_Vec(x, y, z);
    Vec3 s = GPU3D_Vec(FX_MUL(r, FX(2.0)), FX_MUL(r, FX(2.0)), FX(0.14));
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, 0, angle, col, 0);
    if (spokes)
    {
        Vec3 sp = GPU3D_Vec(x, y, z - FX(0.09));
        fixed bar = FX_MUL(r, FX(1.7));
        GPU3D_DrawBox(C_GRAY_LIGHT, &sp, bar, FX(0.07), FX(0.05), 0, 0, angle, 0);
        GPU3D_DrawBox(C_GRAY_LIGHT, &sp, bar, FX(0.07), FX(0.05), 0, 0, (int32_t)(angle + ANG(90)), 0);
    }
}

static void DrawGoods(uint8_t item, uint8_t load, fixed cx, fixed yBase, fixed zOff)
{
    for (uint8_t i = 0; i < load && i < 4; i++)
    {
        fixed gx = cx + FX(-0.45 + (double)i*0.3);
        Vec3 p = GPU3D_Vec(gx, yBase + FX(1.22), zOff);
        if (item == 0)
        {
            Vec3 s = GPU3D_Vec(FX(0.26), FX(0.26), FX(0.26));
            GPU3D_DrawMeshTinted(&GPU3D_BallMesh, &p, &s, 0, 0, 0, C_RED, 0);
            Vec3 lp = GPU3D_Vec(gx, yBase + FX(1.38), zOff);
            GPU3D_DrawBox(C_GREEN, &lp, FX(0.08), FX(0.06), FX(0.05), 0, 0, 0, 0);
        }
        else if (item == 1)
        {
            GPU3D_DrawBox(C_ORANGE, &p, FX(0.26), FX(0.26), FX(0.26), 0, 0, 0, 0);
        }
        else
        {
            GPU3D_DrawBox(C_WHITE, &p, FX(0.2), FX(0.32), FX(0.2), 0, 0, 0, 0);
            Vec3 lp = GPU3D_Vec(gx, yBase + FX(1.4), zOff);
            GPU3D_DrawBox(C_GRAY_LIGHT, &lp, FX(0.12), FX(0.05), FX(0.12), 0, 0, 0, 0);
        }
    }
}

/* a cargo wagon loaded by solving a puzzle */
static void DrawCargoWagon3D(const Cargo * c, fixed cx, int32_t angle, int16_t shade,
                             fixed yOff, fixed zOff, fixed scl)
{
    GPU_Color wood = GPU_ShadeColor(RGB(150,110,60), shade);
    GPU_Color dark = GPU_ShadeColor(RGB(25,25,32), shade);
    fixed wr = FX_MUL(FX(0.25), scl);
    fixed wy = FX_MUL(FX(0.25), scl) + yOff;
    DrawWheel3D(cx - FX_MUL(FX(0.5), scl), wy, zOff + FX(0.42), wr, angle, dark, 0);
    DrawWheel3D(cx + FX_MUL(FX(0.5), scl), wy, zOff + FX(0.42), wr, angle, dark, 0);
    Vec3 p = GPU3D_Vec(cx, FX_MUL(FX(0.36), scl) + yOff, zOff);
    GPU3D_DrawBox(dark, &p, FX_MUL(FX(1.55), scl), FX_MUL(FX(0.12), scl), FX_MUL(FX(0.85), scl), 0, 0, 0, 0);
    p = GPU3D_Vec(cx, FX_MUL(FX(0.78), scl) + yOff, zOff);
    GPU3D_DrawBox(wood, &p, FX_MUL(FX(1.5), scl), FX_MUL(FX(0.65), scl), FX_MUL(FX(0.9), scl), 0, 0, 0, 0);
    p = GPU3D_Vec(cx, FX_MUL(FX(0.78), scl) + yOff, zOff - FX_MUL(FX(0.46), scl));
    GPU3D_DrawBox(GPU_ShadeColor(RGB(95,70,40), shade), &p, FX_MUL(FX(1.5), scl), FX_MUL(FX(0.05), scl), FX(0.02), 0, 0, 0, 0);
    DrawGoods(c->item, c->load, cx, yOff, zOff);
    DrawWheel3D(cx - FX_MUL(FX(0.5), scl), wy, zOff - FX(0.42), wr, angle, dark, 0);
    DrawWheel3D(cx + FX_MUL(FX(0.5), scl), wy, zOff - FX(0.42), wr, angle, dark, 0);
}

/* the detailed steam locomotive + coal tender (unchanged showpiece) */
static void DrawLoco3D(uint8_t night)
{
    int16_t shade = night ? 150 : 256;
    int32_t a = WheelAngle();
    GPU_Color maroon = GPU_ShadeColor(RGB(150,45,55), shade);
    GPU_Color maroonLit = GPU_ShadeColor(RGB(195,75,85), shade);
    GPU_Color dark = GPU_ShadeColor(RGB(35,35,42), shade);
    GPU_Color brass = GPU_ShadeColor(RGB(200,160,60), shade);
    fixed pinX = FX_MUL(GPU3D_Cos(a), FX(0.28));
    fixed pinY = FX_MUL(GPU3D_Sin(a), FX(0.28));

    DrawWheel3D(FX(-2.35), FX(0.25), FX(0.42), FX(0.25), a, dark, 0);
    DrawWheel3D(FX(-3.15), FX(0.25), FX(0.42), FX(0.25), a, dark, 0);
    Vec3 p = GPU3D_Vec(FX(-2.75), FX(0.75), 0);
    GPU3D_DrawBox(GPU_ShadeColor(RGB(60,60,70), shade), &p, FX(1.35), FX(0.75), FX(0.95), 0, 0, 0, 0);
    fixed pile = FX(0.18) + FX_MUL(FX(0.3), FX(memory->steam)/100);
    p = GPU3D_Vec(FX(-2.75), FX(1.12), 0);
    Vec3 ps = GPU3D_Vec(FX(1.1), pile, FX(0.75));
    GPU3D_DrawMeshTinted(&GPU3D_BallMesh, &p, &ps, 0, 0, 0, GPU_ShadeColor(RGB(20,20,24), shade), 0);
    DrawWheel3D(FX(-2.35), FX(0.25), FX(-0.42), FX(0.25), a, dark, 0);
    DrawWheel3D(FX(-3.15), FX(0.25), FX(-0.42), FX(0.25), a, dark, 0);

    DrawWheel3D(FX(-1.5), FX(0.28), FX(0.42), FX(0.28), a, dark, 0);
    DrawWheel3D(FX(2.0), FX(0.28), FX(0.42), FX(0.28), a, dark, 0);
    DrawWheel3D(FX(-0.3), FX(0.45), FX(0.42), FX(0.45), a, dark, 0);
    DrawWheel3D(FX(0.9), FX(0.45), FX(0.42), FX(0.45), a, dark, 0);

    p = GPU3D_Vec(FX(0.2), FX(0.32), 0);
    GPU3D_DrawBox(dark, &p, FX(3.7), FX(0.16), FX(0.85), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(0.75), FX(0.95), 0);
    Vec3 s = GPU3D_Vec(FX(0.84), FX(0.84), FX(2.2));
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, ANG(90), 0, maroon, 0);
    s = GPU3D_Vec(FX(0.88), FX(0.88), FX(0.07));
    p = GPU3D_Vec(FX(0.15), FX(0.95), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, ANG(90), 0, maroonLit, 0);
    p = GPU3D_Vec(FX(1.35), FX(0.95), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, ANG(90), 0, maroonLit, 0);
    s = GPU3D_Vec(FX(0.84), FX(0.84), FX(0.5));
    p = GPU3D_Vec(FX(2.0), FX(0.95), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, ANG(90), 0, dark, 0);
    s = GPU3D_Vec(FX(0.6), FX(0.6), FX(0.06));
    p = GPU3D_Vec(FX(2.28), FX(0.95), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, ANG(90), 0, C_GRAY_MEDIUM, 0);
    s = GPU3D_Vec(FX(0.24), FX(0.24), FX(0.5));
    p = GPU3D_Vec(FX(2.0), FX(1.55), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, 0, 0, GPU_ShadeColor(RGB(20,20,26), shade), 0);
    s = GPU3D_Vec(FX(0.36), FX(0.36), FX(0.1));
    p = GPU3D_Vec(FX(2.0), FX(1.83), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, 0, 0, GPU_ShadeColor(RGB(20,20,26), shade), 0);
    p = GPU3D_Vec(FX(0.95), FX(1.4), 0);
    s = GPU3D_Vec(FX(0.32), FX(0.24), FX(0.32));
    GPU3D_DrawMeshTinted(&GPU3D_BallMesh, &p, &s, 0, 0, 0, brass, 0);
    p = GPU3D_Vec(FX(0.4), FX(1.42), 0);
    GPU3D_DrawBox(GPU_ShadeColor(RGB(170,130,50), shade), &p, FX(0.35), FX(0.22), FX(0.3), 0, 0, 0, 0);
    s = GPU3D_Vec(FX(0.1), FX(0.1), FX(0.2));
    p = GPU3D_Vec(FX(-0.15), FX(1.6), 0);
    GPU3D_DrawMeshTinted(&_cylMesh, &p, &s, 0, 0, 0, GPU_ShadeColor(RGB(220,180,80), shade), 0);
    p = GPU3D_Vec(FX(-1.35), FX(1.0), 0);
    GPU3D_DrawBox(GPU_ShadeColor(RGB(120,35,45), shade), &p, FX(1.1), FX(1.15), FX(1.0), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(-1.35), FX(1.62), 0);
    GPU3D_DrawBox(dark, &p, FX(1.3), FX(0.08), FX(1.15), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(-1.35), FX(1.15), FX(-0.52));
    GPU3D_DrawBox(night ? RGB(250,220,120) : RGB(140,200,230), &p, FX(0.5), FX(0.4), FX(0.06), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(2.5), FX(0.35), 0);
    s = GPU3D_Vec(FX(0.5), FX(0.55), FX(0.9));
    GPU3D_DrawMeshTinted(&GPU3D_PyramidMesh, &p, &s, 0, 0, ANG(-90), GPU_ShadeColor(RGB(60,60,68), shade), 0);
    p = GPU3D_Vec(FX(2.32), FX(0.55), 0);
    GPU3D_DrawBox(GPU_ShadeColor(RGB(160,40,40), shade), &p, FX(0.1), FX(0.3), FX(0.9), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(2.35), FX(1.15), 0);
    GPU3D_DrawBox(C_YELLOW, &p, FX(0.14), FX(0.2), FX(0.2), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(1.75), FX(0.45), FX(-0.42));
    GPU3D_DrawBox(GPU_ShadeColor(RGB(60,60,68), shade), &p, FX(0.5), FX(0.28), FX(0.25), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(0.6), FX(0.62), FX(-0.45));
    GPU3D_DrawBox(dark, &p, FX(2.6), FX(0.05), FX(0.12), 0, 0, 0, 0);
    {
        Vec3 ha = GPU3D_Vec(FX(-0.3), FX(1.12), FX(-0.44));
        Vec3 hb = GPU3D_Vec(FX(1.8), FX(1.12), FX(-0.44));
        GPU3D_DrawLineWorld(C_GRAY_LIGHT, &ha, &hb);
    }

    DrawWheel3D(FX(-1.5), FX(0.28), FX(-0.42), FX(0.28), a, dark, 0);
    DrawWheel3D(FX(2.0), FX(0.28), FX(-0.42), FX(0.28), a, dark, 0);
    DrawWheel3D(FX(-0.3), FX(0.45), FX(-0.42), FX(0.45), a, dark, 1);
    DrawWheel3D(FX(0.9), FX(0.45), FX(-0.42), FX(0.45), a, dark, 1);

    p = GPU3D_Vec(FX(0.3) + pinX, FX(0.45) + pinY, FX(-0.52));
    GPU3D_DrawBox(C_GRAY_LIGHT, &p, FX(1.2), FX(0.07), FX(0.06), 0, 0, 0, 0);
    p = GPU3D_Vec(FX(1.45) + pinX, FX(0.45), FX(-0.5));
    GPU3D_DrawBox(C_GRAY_MEDIUM, &p, FX(0.16), FX(0.12), FX(0.08), 0, 0, 0, 0);
    Vec3 ra = GPU3D_Vec(FX(1.45) + pinX, FX(0.45), FX(-0.5));
    Vec3 rb = GPU3D_Vec(FX(0.9) + pinX, FX(0.45) + pinY, FX(-0.5));
    GPU3D_DrawLineWorld(C_GRAY_LIGHT, &ra, &rb);

    int16_t sx, sy;
    if (Project3D(FX(-0.9), FX(0.35), FX(-0.5), &sx, &sy))
        GPU_DrawGlow(C_ORANGE, sx, sy, (int16_t)(4 + (memory->animTick % 3)));
    if (night && Project3D(FX(2.4), FX(1.15), FX(-0.1), &sx, &sy))
        GPU_DrawGlow(C_YELLOW, sx, sy, 7);
}

/* ------------------------------------------------------------------- puzzles */

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static float MaxV(void)
{
    float m = 240.0f + 10.0f*(float)(memory->level-1);
    if (m > 300.0f) m = 300.0f;
    return m * (0.45f + 0.55f*memory->steam/100.0f);
}

static uint8_t Tier(void)
{
    uint8_t t = (uint8_t)(memory->solved/4);
    return t > 2 ? 2 : t;
}

/* three numeric choices: the answer plus two nearby distractors */
static void NumChoices(uint8_t answer, uint8_t lo, uint8_t hi)
{
    Puzzle * z = &memory->pz;
    z->choiceKind = 0;
    z->nChoices = 3;
    z->val[0] = answer;
    uint8_t n = 1;
    while (n < 3)
    {
        int8_t v = (int8_t)((int8_t)answer + (int8_t)Random_Range(5) - 2);
        if (v < (int8_t)lo || v > (int8_t)hi || v == (int8_t)answer)
            continue;
        uint8_t dup = 0;
        for (uint8_t j = 0; j < n; j++) if (z->val[j] == (uint8_t)v) dup = 1;
        if (!dup) z->val[n++] = (uint8_t)v;
    }
    for (uint8_t i = 0; i < 3; i++)
    {
        uint8_t j = (uint8_t)Random_Range(3);
        uint8_t t = z->val[i]; z->val[i] = z->val[j]; z->val[j] = t;
    }
    for (uint8_t i = 0; i < 3; i++) if (z->val[i] == answer) z->correct = i;
}

static void NewPuzzle(void)
{
    Puzzle * z = &memory->pz;
    memset(z, 0, sizeof(Puzzle));
    uint8_t tier = Tier();
    z->type = (memory->solved < 2) ? PC_COUNT : (uint8_t)Random_Range(PC_KINDS);
    z->phase = 0;
    z->cursor = 0;
    z->seed = Random_Next();

    switch (z->type)
    {
        case PC_COUNT:
        {
            uint8_t maxN = tier == 0 ? 3 : (tier == 1 ? 5 : 10);
            z->a = (uint8_t)(1 + Random_Range(maxN));
            z->objType = (uint8_t)Random_Range(3);
            NumChoices(z->a, 1, 10);
            break;
        }
        case PC_ADD:
        {
            if (tier == 0) { z->a = (uint8_t)(1 + Random_Range(4)); z->b = (uint8_t)(1 + Random_Range(5 - z->a)); }
            else { z->a = (uint8_t)(1 + Random_Range(5)); z->b = (uint8_t)(1 + Random_Range(5)); }
            z->objType = 0;
            NumChoices((uint8_t)(z->a + z->b), 2, 10);
            break;
        }
        case PC_SUB:
        {
            z->a = tier == 0 ? (uint8_t)(2 + Random_Range(4)) : (uint8_t)(2 + Random_Range(9));
            z->b = (uint8_t)(1 + Random_Range(z->a - 1));
            z->objType = 0;
            NumChoices((uint8_t)(z->a - z->b), 1, 10);
            break;
        }
        case PC_PATTERN:
        {
            /* three distinct shape+colour elements */
            uint8_t es[3], ec[3];
            uint8_t c = 0;
            while (c < 3)
            {
                uint8_t s = (uint8_t)Random_Range(6), col = (uint8_t)Random_Range(6);
                uint8_t dup = 0;
                for (uint8_t j = 0; j < c; j++) if (es[j] == s && ec[j] == col) dup = 1;
                if (!dup) { es[c] = s; ec[c] = col; c++; }
            }
            uint8_t map[4], period;
            uint8_t kind = (uint8_t)Random_Range(tier == 0 ? 1 : (tier == 1 ? 3 : 4));
            if (kind == 0)      { period = 2; map[0] = 0; map[1] = 1; }
            else if (kind == 1) { period = 4; map[0] = 0; map[1] = 0; map[2] = 1; map[3] = 1; }
            else if (kind == 2) { period = 3; map[0] = 0; map[1] = 1; map[2] = 1; }
            else                { period = 3; map[0] = 0; map[1] = 1; map[2] = 2; }
            z->seqLen = 5;
            for (uint8_t i = 0; i < 5; i++)
            {
                z->seqShape[i] = es[map[i % period]];
                z->seqColor[i] = ec[map[i % period]];
            }
            uint8_t ans = map[5 % period];
            z->choiceKind = 1;
            z->nChoices = 3;
            uint8_t order[3] = { 0, 1, 2 };
            for (uint8_t i = 0; i < 3; i++)
            {
                uint8_t j = (uint8_t)Random_Range(3);
                uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
            }
            for (uint8_t i = 0; i < 3; i++)
            {
                z->cshape[i] = es[order[i]];
                z->ccolor[i] = ec[order[i]];
                if (order[i] == ans) z->correct = i;
            }
            break;
        }
        case PC_ODD:
        {
            uint8_t s = (uint8_t)Random_Range(6), col = (uint8_t)Random_Range(6);
            uint8_t os = s, oc = col;
            if (tier == 0) oc = (uint8_t)((col + 1 + Random_Range(5)) % 6);   /* other colour */
            else           os = (uint8_t)((s + 1 + Random_Range(5)) % 6);     /* other shape */
            z->choiceKind = 1;
            z->nChoices = 4;
            uint8_t odd = (uint8_t)Random_Range(4);
            for (uint8_t i = 0; i < 4; i++)
            {
                z->cshape[i] = (i == odd) ? os : s;
                z->ccolor[i] = (i == odd) ? oc : col;
            }
            z->correct = odd;
            break;
        }
        case PC_MISSING:
        {
            int8_t step = tier == 0 ? 1 : (tier == 1 ? (int8_t)(1 + Random_Range(2))
                                                       : (int8_t)(Random_Range(2) ? 2 : -1));
            int8_t start;
            if (step > 0) start = (int8_t)Random_Range(8);
            else          start = (int8_t)(8 + Random_Range(8));
            for (uint8_t i = 0; i < 5; i++)
            {
                int8_t v = (int8_t)(start + i*step);
                if (v < 0) v = 0;
                if (v > 20) v = 20;
                z->seqVal[i] = (uint8_t)v;
            }
            z->gapIdx = (uint8_t)(1 + Random_Range(3));
            NumChoices(z->seqVal[z->gapIdx], 0, 20);
            break;
        }
        case PC_BIG:
        {
            uint8_t maxN = tier == 0 ? 10 : 20;
            uint8_t a = (uint8_t)(1 + Random_Range(maxN));
            uint8_t b;
            do { b = (uint8_t)(1 + Random_Range(maxN)); } while (b == a);
            z->val[0] = a; z->val[1] = b;
            z->choiceKind = 0;
            z->nChoices = 2;
            z->big = tier == 0 ? 1 : (uint8_t)Random_Range(2);
            uint8_t bigger = a > b ? 0 : 1;
            z->correct = z->big ? bigger : (uint8_t)(1 - bigger);
            break;
        }
        default: /* PC_LETTER */
        {
            z->letterMode = tier;       /* 0 match capital, 1 find small, 2 first letter */
            if (z->letterMode == 2)
            {
                const char * w = _words[Random_Range(20)];
                z->letter = (uint8_t)(w[0] - 'A');
            }
            else
            {
                z->letter = (uint8_t)Random_Range(26);
            }
            z->choiceKind = 2;
            z->nChoices = 4;
            z->val[0] = z->letter;
            uint8_t n = 1;
            while (n < 4)
            {
                uint8_t v = (uint8_t)Random_Range(26);
                uint8_t dup = 0;
                for (uint8_t j = 0; j < n; j++) if (z->val[j] == v) dup = 1;
                if (!dup) z->val[n++] = v;
            }
            for (uint8_t i = 0; i < 4; i++)
            {
                uint8_t j = (uint8_t)Random_Range(4);
                uint8_t t = z->val[i]; z->val[i] = z->val[j]; z->val[j] = t;
            }
            for (uint8_t i = 0; i < 4; i++) if (z->val[i] == z->letter) z->correct = i;
            break;
        }
    }
}

static void Popup(uint8_t type)
{
    memory->popupType = type;
    memory->popupTicks = 40;
}

static void SpawnStation(void)
{
    memory->stationX = memory->dist + 800.0f + (float)Random_Range(400);
    memory->stationDone = 0;
    memory->missedNotified = 0;
    memory->people = (uint8_t)(1 + Random_Range(4));
}

/* solved the station puzzle: load a cargo wagon */
static void LoadCargo(void)
{
    Cargo * c = &memory->cargo[memory->loaded];
    c->item = (uint8_t)Random_Range(3);
    c->load = (uint8_t)(1 + Random_Range(4));
    c->state = 1;
    c->slideT = 0;
    c->slot = memory->loaded;
    memory->numCargo = (uint8_t)(memory->loaded + 1);
    memory->loaded++;
    memory->stars++;
    memory->solved++;
    memory->score += 50;
    memory->stationDone = 1;
    memory->puzzleActive = 0;
    if (memory->loaded >= memory->target)
    {
        memory->score += 200u + 25u*(memory->level-1);
        memory->deliveries++;
        memory->level = (uint8_t)(memory->deliveries/2 + 1);
        if (memory->level > 6) memory->level = 6;
        memory->celebrate = 70;
        memory->whistleTicks = 30;
        memory->loaded = 0;
        memory->target = (memory->deliveries < 3) ? 3 : 4;
        Popup(3);
    }
    else
    {
        memory->celebrate = 30;
        Popup(0);
    }
    if (memory->score > bestScore) bestScore = memory->score;
}

static void PuzzleAnswer(uint8_t idx)
{
    Puzzle * z = &memory->pz;
    if (!memory->puzzleActive || z->phase != 0 || idx >= z->nChoices)
        return;
    if (idx == z->correct)
    {
        z->phase = 1;
        z->phaseTicks = 25;
    }
    else
    {
        z->wrongIdx = idx;
        z->phase = 2;
        z->phaseTicks = 12;
    }
}

static void PuzzleTick(void)
{
    Puzzle * z = &memory->pz;
    if (z->phase == 0)
        return;
    if (z->phaseTicks)
        z->phaseTicks--;
    if (z->phaseTicks == 0)
    {
        if (z->phase == 1)
            LoadCargo();
        else
            z->phase = 0;
    }
}

/* --------------------------------------------------------------------- tick */

static void SpawnPuffAt(float x, float y)
{
    for (uint8_t i = 0; i < WO_NUM_PUFFS; i++)
        if (memory->puffs[i].age >= 40)
        {
            memory->puffs[i].x = x + (float)Random_Range(5) - 2.0f;
            memory->puffs[i].y = y;
            memory->puffs[i].age = 0;
            return;
        }
}

static void SpawnChimneyPuff(void)
{
    int16_t sx, sy;
    Setup3D(0);
    if (Project3D(FX(2.0), FX(1.9), 0, &sx, &sy))
        SpawnPuffAt((float)sx, (float)sy);
    else
        SpawnPuffAt(287.0f, 150.0f);
}

static void Whistle(void)
{
    memory->whistleTicks = 24;
    Popup(6);
    int16_t sx, sy;
    Setup3D(0);
    if (Project3D(FX(-0.15), FX(1.75), 0, &sx, &sy))
    {
        SpawnPuffAt((float)sx, (float)sy);
        SpawnPuffAt((float)sx, (float)sy);
    }
}

static void Tick(void)
{
    memory->animTick++;

    uint8_t throttle = 0, brake = 0;
    uint8_t driving = (uint8_t)(memory->state == 1 && !memory->celebrate && !memory->puzzleActive);
    if (driving)
    {
        KeyPressedEnum held = Keyboard_GetHeldKeys();
        throttle = (held & KEY_RIGHT) ? 1 : 0;
        brake = (held & KEY_LEFT) ? 1 : 0;
        if (mainMemory.touchPressed)
        {
            if (mainMemory.touch_X < GPU_X/2) brake = 1;
            else throttle = 1;
        }
    }
    memory->braking = (uint8_t)(brake && memory->v > 25.0f);

    if (memory->state == 1 && !memory->celebrate)
    {
        if (throttle) memory->steam -= 5.0f/NUM_TICKS;
        else          memory->steam += 7.0f/NUM_TICKS;
        if (memory->steam < 0.0f) memory->steam = 0.0f;
        if (memory->steam > 100.0f) memory->steam = 100.0f;
    }

    float av = memory->v < 0 ? -memory->v : memory->v;
    uint8_t puffEvery = throttle ? 3 : (av > 1.0f ? 6 : 14);
    if ((memory->animTick % puffEvery) == 0)
        SpawnChimneyPuff();
    for (uint8_t i = 0; i < WO_NUM_PUFFS; i++)
    {
        Puff * p = &memory->puffs[i];
        if (p->age >= 40) continue;
        p->age++;
        p->x -= memory->v/(float)NUM_TICKS*0.85f + 0.4f;
        p->y -= 0.55f;
    }
    if (memory->whistleTicks) memory->whistleTicks--;

    if (memory->state != 1)
        return;
    if (memory->popupTicks) memory->popupTicks--;

    /* cargo wagons sliding onto the train */
    for (uint8_t i = 0; i < memory->numCargo; i++)
    {
        Cargo * c = &memory->cargo[i];
        if (c->state == 1)
        {
            c->slideT++;
            if (c->slideT >= 20) c->state = 2;
        }
    }

    if (memory->puzzleActive)
    {
        PuzzleTick();
        return;
    }

    if (memory->celebrate)
    {
        memory->celebrate--;
        if (!memory->celebrate)
        {
            if (memory->loaded == 0)
                memory->numCargo = 0;       /* the cargo was delivered */
            SpawnStation();
        }
        return;
    }

    /* driving physics */
    if (throttle)
        memory->v += WO_ACCEL/NUM_TICKS;
    else if (brake)
        memory->v -= (memory->v > 0 ? WO_BRAKE : WO_REV_ACCEL)/NUM_TICKS;
    else
    {
        /* coasting: a gentle stop assist glides the train to a halt right at
           an upcoming station, so letting go of RIGHT is enough to stop */
        float decel = WO_FRICTION;
        float rel = memory->stationX - memory->dist;
        if (!memory->stationDone && memory->v > 0.0f &&
            rel < WO_STOP_ASSIST && rel > -WO_STATION_ZONE)
        {
            float need = (rel > 1.0f) ? (memory->v*memory->v/(2.0f*rel)) : 200.0f;
            if (need > decel) decel = need;
            if (decel > 200.0f) decel = 200.0f;
        }
        if (memory->v > 0) memory->v -= decel/NUM_TICKS;
        else if (memory->v < 0) memory->v += WO_FRICTION/NUM_TICKS;
        if (memory->v < 0.6f && memory->v > -0.6f) memory->v = 0.0f;
    }
    if (memory->v > MaxV()) memory->v = MaxV();
    if (memory->v < WO_MAX_REV) memory->v = WO_MAX_REV;
    memory->dist += memory->v/NUM_TICKS;
    if (memory->dist < -300.0f) { memory->dist = -300.0f; memory->v = 0.0f; }

    /* coal lumps refill the tender */
    for (uint8_t i = 0; i < WO_NUM_COAL; i++)
    {
        Coal * c = &memory->coal[i];
        if (!c->taken)
        {
            float d = c->worldX - memory->dist;
            if (d < 0) d = -d;
            if (d < 20.0f)
            {
                c->taken = 1;
                memory->score += 10;
                memory->steam += 25.0f;
                if (memory->steam > 100.0f) memory->steam = 100.0f;
                Popup(5);
                SpawnChimneyPuff();
                if (memory->score > bestScore) bestScore = memory->score;
            }
        }
        if (c->worldX < memory->dist - 120.0f)
        {
            c->worldX = memory->dist + 900.0f + (float)Random_Range(500);
            c->taken = 0;
        }
    }

    /* arriving at a station opens its puzzle */
    if (!memory->stationDone)
    {
        float rel = memory->stationX - memory->dist;
        float d = rel < 0 ? -rel : rel;
        if (memory->v == 0.0f && d <= WO_STATION_ZONE)
        {
            memory->puzzleActive = 1;
            memory->v = 0.0f;
            memory->missedNotified = 0;
            NewPuzzle();
        }
        else if (rel < -WO_STATION_ZONE)
        {
            /* passed it: the station STAYS so the child can reverse back to
               it - just a gentle nudge, no penalty and no new station */
            if (!memory->missedNotified)
            {
                memory->missedNotified = 1;
                Popup(1);
            }
        }
        else
        {
            memory->missedNotified = 0;
        }
    }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game50Memory));
    memory->level = 1;
    memory->steam = 100.0f;
    memory->target = 3;
    for (uint8_t i = 0; i < WO_NUM_PUFFS; i++) memory->puffs[i].age = 40;
    for (uint8_t i = 0; i < WO_NUM_COAL; i++)
    {
        memory->coal[i].worldX = 600.0f + i*520.0f;
        memory->coal[i].taken = 0;
    }
    SpawnStation();
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void G_Update(void)
{
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    uint16_t tx, ty;
    uint8_t tap = Touch_TakeTap(&tx, &ty);

    if (memory->state == 0)
    {
        if (keys != KEY_NONE || tap)
            memory->state = 1;
    }
    else if (memory->puzzleActive)
    {
        Puzzle * z = &memory->pz;
        if (z->phase == 0)
        {
            if (keys & KEY_LEFT)  z->cursor = (uint8_t)((z->cursor + z->nChoices - 1) % z->nChoices);
            if (keys & KEY_RIGHT) z->cursor = (uint8_t)((z->cursor + 1) % z->nChoices);
            if (keys & KEY_UP)    PuzzleAnswer(z->cursor);
            if (tap && ty >= 150 && ty < 206)
            {
                for (uint8_t i = 0; i < z->nChoices; i++)
                {
                    int16_t x = (z->nChoices == 2) ? (int16_t)(24 + i*152) :
                                (z->nChoices == 4) ? (int16_t)(8 + i*77) : (int16_t)(12 + i*102);
                    int16_t w = (z->nChoices == 2) ? 120 : (z->nChoices == 4) ? 68 : 88;
                    if (tx >= x && tx < x + w)
                    {
                        z->cursor = i;
                        PuzzleAnswer(i);
                        break;
                    }
                }
            }
        }
    }
    else
    {
        if ((keys & KEY_DOWN) || (keys & KEY_UP) || tap)
            Whistle();
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

/* ------------------------------------------------------------- 2d quiz art */

static void DrawStarShape(GPU_Color color, int16_t x, int16_t y, int16_t r)
{
    GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y+r*2/3), (int16_t)(x+r), (int16_t)(y+r*2/3), x, (int16_t)(y-r));
    GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y-r*2/3), (int16_t)(x+r), (int16_t)(y-r*2/3), x, (int16_t)(y+r));
}

static void DrawQuizObject(uint8_t type, uint8_t i, int16_t x, int16_t y)
{
    if (type == 0)
    {
        GPU_DrawFilledCircle(C_RED, x, y, 9);
        GPU_DrawFilledSquare(RGB(110,70,30), (int16_t)(x-1), (int16_t)(y-12), 2, 4);
        GPU_DrawFilledSquare(C_GREEN, (int16_t)(x+1), (int16_t)(y-11), 4, 3);
        GPU_DrawPixel(C_WHITE, (uint16_t)(x-3), (uint16_t)(y-3));
    }
    else if (type == 1)
    {
        GPU_Color c = _colorVal[i % 6];
        GPU_DrawFilledCircle(c, x, (int16_t)(y-2), 8);
        GPU_DrawLine(C_GRAY_LIGHT, x, (int16_t)(y+6), x, (int16_t)(y+13));
        GPU_DrawPixel(C_WHITE, (uint16_t)(x-3), (uint16_t)(y-5));
    }
    else
    {
        DrawStarShape(C_YELLOW, x, y, 10);
        GPU_DrawFilledCircle(C_ORANGE, x, y, 3);
    }
}

static void DrawShape(uint8_t shape, GPU_Color color, int16_t x, int16_t y, int16_t r)
{
    switch (shape)
    {
        case 0: GPU_DrawFilledCircle(color, x, y, r); break;
        case 1: GPU_DrawFilledSquare(color, (int16_t)(x-r*3/4), (int16_t)(y-r*3/4), (int16_t)(r*3/2), (int16_t)(r*3/2)); break;
        case 2: GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y+r*3/4), (int16_t)(x+r), (int16_t)(y+r*3/4), x, (int16_t)(y-r)); break;
        case 3: DrawStarShape(color, x, y, r); break;
        case 4:
            GPU_DrawFilledCircle(color, (int16_t)(x-r/2), (int16_t)(y-r/3), (int16_t)(r/2+1));
            GPU_DrawFilledCircle(color, (int16_t)(x+r/2), (int16_t)(y-r/3), (int16_t)(r/2+1));
            GPU_DrawFilledTriangle(color, (int16_t)(x-r), (int16_t)(y-r/6), (int16_t)(x+r), (int16_t)(y-r/6), x, (int16_t)(y+r));
            break;
        default:
            GPU_DrawFilledTriangle(color, (int16_t)(x-r*3/4), y, (int16_t)(x+r*3/4), y, x, (int16_t)(y-r));
            GPU_DrawFilledTriangle(color, (int16_t)(x-r*3/4), y, (int16_t)(x+r*3/4), y, x, (int16_t)(y+r));
            break;
    }
}

static void DrawApple(int16_t x, int16_t y, uint8_t crossed)
{
    GPU_Color body = crossed ? GPU_ShadeColor(C_RED, 110) : C_RED;
    GPU_DrawFilledCircle(body, x, y, 8);
    GPU_DrawFilledSquare(RGB(110,70,30), (int16_t)(x-1), (int16_t)(y-11), 2, 4);
    GPU_DrawFilledSquare(crossed ? GPU_ShadeColor(C_GREEN, 110) : C_GREEN, (int16_t)(x+1), (int16_t)(y-10), 4, 3);
    if (crossed)
    {
        GPU_DrawLine(C_RED, (int16_t)(x-8), (int16_t)(y-8), (int16_t)(x+8), (int16_t)(y+8));
        GPU_DrawLine(C_RED, (int16_t)(x-8), (int16_t)(y+8), (int16_t)(x+8), (int16_t)(y-8));
    }
    else GPU_DrawPixel(C_WHITE, (uint16_t)(x-3), (uint16_t)(y-3));
}

/* ------------------------------------------------------------- puzzle panel */

static void DrawPuzzleQuestion(void)
{
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    Puzzle * z = &memory->pz;
    char text[16];

    switch (z->type)
    {
        case PC_COUNT:
            for (uint8_t i = 0; i < z->a; i++)
            {
                uint32_t hh = (uint32_t)i*2654435761u + z->seed;
                int16_t x = (int16_t)(60 + (i % 5)*50 + (int16_t)(hh % 5) - 2);
                int16_t y = (int16_t)(72 + (i / 5)*40 + (int16_t)((hh>>3) % 5) - 2);
                DrawQuizObject(z->objType, i, x, y);
            }
            break;
        case PC_ADD:
            for (uint8_t i = 0; i < z->a; i++)
                DrawApple((int16_t)(70 + (i % 3)*22 - (z->a > 3 ? 11 : 0)), (i < 3) ? 72 : 100, 0);
            GPU_DrawFilledSquare(C_WHITE, 150, 80, 20, 6);
            GPU_DrawFilledSquare(C_WHITE, 157, 73, 6, 20);
            for (uint8_t i = 0; i < z->b; i++)
                DrawApple((int16_t)(210 + (i % 3)*22 - (z->b > 3 ? 11 : 0)), (i < 3) ? 72 : 100, 0);
            GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 118, "= ?");
            break;
        case PC_SUB:
            for (uint8_t i = 0; i < z->a; i++)
            {
                int16_t x = (int16_t)(56 + (i % 5)*52);
                int16_t y = (i < 5) ? 72 : 106;
                DrawApple(x, y, (uint8_t)(i >= z->a - z->b));
            }
            sprintf(text, "%u - %u = ?", (unsigned)z->a, (unsigned)z->b);
            GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 124, text);
            break;
        case PC_PATTERN:
            for (uint8_t i = 0; i < 6; i++)
            {
                int16_t x = (int16_t)(20 + i*48);
                GPU_DrawBevelSquare(RGB(40,40,70), x, 62, 44, 44);
                if (i < 5)
                    DrawShape(z->seqShape[i], _colorVal[z->seqColor[i]], (int16_t)(x+22), 84, 14);
                else
                    GPU_DrawTextCentered(C_YELLOW, fh, (int16_t)(x+22), 71, "?");
            }
            break;
        case PC_ODD:
            GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, 80, "TAP THE");
            GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, 102, "ODD ONE OUT");
            break;
        case PC_MISSING:
            for (uint8_t i = 0; i < 5; i++)
            {
                int16_t x = (int16_t)(28 + i*54);
                GPU_DrawBevelSquare(RGB(40,40,70), x, 70, 48, 40);
                if (i == z->gapIdx)
                    GPU_DrawTextCentered(C_YELLOW, fh, (int16_t)(x+24), 77, "?");
                else
                {
                    sprintf(text, "%u", (unsigned)z->seqVal[i]);
                    GPU_DrawTextCentered(C_WHITE, fb, (int16_t)(x+24), 81, text);
                }
            }
            break;
        case PC_BIG:
            GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, 80, z->big ? "TAP THE BIGGER" : "TAP THE SMALLER");
            GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, 102, "NUMBER");
            break;
        default: /* PC_LETTER */
            if (z->letterMode == 2)
            {
                const char * w = _words[0];
                /* find the word whose first letter matches (for the picture) */
                for (uint8_t i = 0; i < 20; i++)
                    if ((uint8_t)(_words[i][0]-'A') == z->letter) { w = _words[i]; break; }
                text[0] = '_'; text[1] = w[1]; text[2] = w[2]; text[3] = 0;
                GPU_DrawTextCentered(C_WHITE, fh, GPU_X/2, 70, text);
                GPU_DrawTextCentered(C_GRAY_LIGHT, &fontDescription[0], GPU_X/2, 104, "which letter starts the word?");
            }
            else
            {
                GPU_DrawPanel(RGB(30,20,60), C_YELLOW, (int16_t)(GPU_X/2-30), 58, 60, 50);
                text[0] = (char)('A' + z->letter); text[1] = 0;
                GPU_DrawTextCentered(C_WHITE, fh, GPU_X/2, 70, text);
                GPU_DrawTextCentered(C_GRAY_LIGHT, &fontDescription[0], GPU_X/2, 112,
                                     z->letterMode == 1 ? "find the small letter" : "find the same letter");
            }
            break;
    }
}

static void DrawPuzzleCards(void)
{
    const FontDescription * fh = &fontDescription[3];
    Puzzle * z = &memory->pz;
    char text[4];
    for (uint8_t i = 0; i < z->nChoices; i++)
    {
        int16_t x = (z->nChoices == 2) ? (int16_t)(24 + i*152) :
                    (z->nChoices == 4) ? (int16_t)(8 + i*77) : (int16_t)(12 + i*102);
        int16_t w = (z->nChoices == 2) ? 120 : (z->nChoices == 4) ? 68 : 88;
        GPU_Color bg = RGB(45,55,110);
        if (z->phase == 1 && i == z->correct) bg = RGB(30,140,60);
        else if (z->phase == 2 && i == z->wrongIdx && (z->phaseTicks & 2)) bg = RGB(160,50,40);
        GPU_DrawBevelSquare(bg, x, 150, w, 56);
        if (i == z->cursor && z->phase == 0)
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(x-2), 148, w+4, 60);
        int16_t cx = (int16_t)(x + w/2);
        if (z->choiceKind == 1)
        {
            DrawShape(z->cshape[i], _colorVal[z->ccolor[i]], cx, 178, 18);
        }
        else if (z->choiceKind == 2)
        {
            text[0] = (char)((z->letterMode == 1 ? 'a' : 'A') + z->val[i]); text[1] = 0;
            GPU_DrawTextCentered(C_WHITE, fh, cx, 165, text);
        }
        else
        {
            sprintf(text, "%u", (unsigned)z->val[i]);
            GPU_DrawTextCentered(C_WHITE, fh, cx, 165, text);
        }
        if (z->phase == 1 && i == z->correct)
            GPU_DrawBurst(cx, 178, (uint8_t)(25 - z->phaseTicks), 25, C_YELLOW);
    }
}

static void DrawPuzzlePanel(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fb = &fontDescription[2];
    Puzzle * z = &memory->pz;
    char text[24];
    GPU_DrawPanel(RGB(16,16,32), C_YELLOW, 10, 26, 300, 190);
    GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 30, _puzzleName[z->type]);
    DrawPuzzleQuestion();
    if (z->phase == 1)
    {
        static const char * const praise[4] = { "GREAT!", "WELL DONE!", "SUPER!", "YES!" };
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 138, praise[memory->stars % 4]);
    }
    else if (z->phase == 2)
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 138, "TRY AGAIN!");
    DrawPuzzleCards();
    sprintf(text, "STARS %u", (unsigned)memory->stars);
    GPU_DrawText(C_YELLOW, fs, 16, 210, text, (uint16_t)strlen(text));
    sprintf(text, "WAGON %u / %u", (unsigned)(memory->loaded + 1), (unsigned)memory->target);
    GPU_DrawText(C_GRAY_LIGHT, fs, (int16_t)(GPU_X - 16 - strlen(text)*fs->width), 210, text, (uint16_t)strlen(text));
}

/* ------------------------------------------------------------------ drawing */

typedef struct {
    GPU_Color skyTop, skyBot, far, near, ground, ballast, cloud;
} Palette;

static const Palette _pal[3] = {
    { RGB(60,120,200), RGB(160,200,235), RGB(110,140,170), RGB(90,150,95),
      RGB(120,160,90), RGB(125,120,115), C_WHITE },
    { RGB(70,50,110), RGB(240,140,80), RGB(95,75,115), RGB(75,95,75),
      RGB(115,110,75), RGB(115,105,98), RGB(250,195,155) },
    { RGB(4,4,22), RGB(24,28,64), RGB(32,36,62), RGB(28,42,38),
      RGB(42,54,46), RGB(72,70,80), RGB(42,46,82) }
};

/* a little person waiting on the platform: shirt body, head, waving arm */
static void DrawPerson3D(fixed x, fixed z, GPU_Color shirt, int16_t shade, uint8_t wave)
{
    Vec3 p = GPU3D_Vec(x, FX(0.45), z);
    GPU3D_DrawBox(GPU_ShadeColor(shirt, shade), &p, FX(0.22), FX(0.5), FX(0.16), 0, 0, 0, 0);
    Vec3 hp = GPU3D_Vec(x, FX(0.82), z);
    Vec3 hs = GPU3D_Vec(FX(0.24), FX(0.24), FX(0.24));
    GPU3D_DrawMeshTinted(&GPU3D_BallMesh, &hp, &hs, 0, 0, 0, GPU_ShadeColor(RGB(250,210,170), shade), 0);
    if (wave)
    {
        Vec3 ap = GPU3D_Vec(x + FX(0.18), FX(0.72), z);
        GPU3D_DrawBox(GPU_ShadeColor(shirt, shade), &ap, FX(0.08), FX(0.3), FX(0.08), 0, 0, ANG(25), 0);
    }
}

/* a 3d station: platform, building, roof, waiting people, stop zone */
static void DrawStation3D(int16_t shade)
{
    float rel = memory->stationX - memory->dist;
    float mx = RelModel(rel);
    if (mx < -13.0f || mx > 9.0f)
        return;
    fixed cx = FX((double)mx);
    /* the building and people sit LEFT of the stop point so they are visible
       along the platform as the train approaches (the loco halts at the right
       end of the platform, on the green zone) */
    fixed bx = cx - FX(2.0);
    /* platform beside the track (far side) */
    FlatBox(GPU_ShadeColor(RGB(155,150,145), shade), cx - FX(1.5), FX(0.1), FX(2.0), FX(4.0), FX(0.2), FX(1.6));
    /* building */
    Vec3 p = GPU3D_Vec(bx, FX(0.9), FX(2.6));
    GPU3D_DrawBox(GPU_ShadeColor(RGB(185,145,105), shade), &p, FX(1.8), FX(1.6), FX(1.0), 0, 0, 0, 0);
    Vec3 rp = GPU3D_Vec(bx, FX(1.9), FX(2.6));
    Vec3 rs = GPU3D_Vec(FX(2.1), FX(0.7), FX(1.2));
    GPU3D_DrawMeshTinted(&GPU3D_PyramidMesh, &rp, &rs, 0, 0, 0, GPU_ShadeColor(RGB(125,60,50), shade), 0);
    /* door + window */
    p = GPU3D_Vec(bx, FX(0.5), FX(2.05));
    GPU3D_DrawBox(GPU_ShadeColor(RGB(70,50,40), shade), &p, FX(0.5), FX(0.9), FX(0.06), 0, 0, 0, 0);
    p = GPU3D_Vec(bx + FX(0.55), FX(1.0), FX(2.05));
    GPU3D_DrawBox(RGB(140,200,230), &p, FX(0.4), FX(0.4), FX(0.06), 0, 0, 0, 0);
    /* friends waiting on the platform (they board once the puzzle is solved) */
    if (!memory->stationDone)
    {
        uint8_t seed = (uint8_t)(memory->stationX);
        for (uint8_t i = 0; i < memory->people && i < 4; i++)
        {
            fixed px = cx + FX(-3.2 + (double)i*0.6);
            uint8_t wave = (uint8_t)(((memory->animTick/8) + i) & 1);
            DrawPerson3D(px, FX(1.5), _colorVal[(i + seed) % 6], shade, wave);
        }
    }
    /* green stop zone on the main rail (zone half width converted to units) */
    if (!memory->stationDone)
    {
        float half = WO_STATION_ZONE/16.0f;
        FlatBox(RGB(0,110,0), cx, FX(0.09), 0, FX((double)(2*half)), FX(0.05), FX(1.0));
        FlatBox(C_GREEN, cx - FX((double)half), FX(0.12), 0, FX(0.1), FX(0.2), FX(1.0));
        FlatBox(C_GREEN, cx + FX((double)half), FX(0.12), 0, FX(0.1), FX(0.2), FX(1.0));
    }
    /* a puzzle marker above the station */
    int16_t sx, sy;
    if (!memory->stationDone && Project3D(bx, FX(2.6), FX(2.6), &sx, &sy))
    {
        GPU_Color mc = (memory->animTick & 8) ? C_YELLOW : C_WHITE;
        GPU_DrawTextOutlined(mc, C_BLACK, &fontDescription[2], sx, (int16_t)(sy-10), "?");
    }
}

static void DrawWorld3D(const Palette * p, int16_t shade)
{
    int32_t dist = (int32_t)memory->dist;
    /* ground plane */
    {
        Vec3 gp = GPU3D_Vec(FX(-4), 0, FX(2.5));
        Vec3 gs = GPU3D_Vec(FX(26), FX_ONE, FX(20));
        GPU3D_DrawMeshTinted(&GPU3D_QuadMesh, &gp, &gs, 0, 0, 0, p->ground, 0);
    }
    /* mountains */
    for (uint8_t i = 0; i < 5; i++)
    {
        float mx = ScrollX(i*271 + 40, 0.12f);
        Vec3 mp = GPU3D_Vec(FX((double)mx), FX(1.2), FX(9.5 + (double)(i%2)));
        Vec3 ms = GPU3D_Vec(FX(4.5), FX(2.6 + (double)(i%3)*0.7), FX(2.0));
        GPU3D_DrawMeshTinted(&GPU3D_PyramidMesh, &mp, &ms, 0, 0, 0, p->far, 0);
    }
    /* telegraph poles */
    for (uint8_t i = 0; i < 3; i++)
    {
        float tx = ScrollX(i*373 + 11, 0.8f);
        FlatBox(GPU_ShadeColor(RGB(80,60,40), shade), FX((double)tx), FX(1.0), FX(6.0), FX(0.08), FX(2.0), FX(0.08));
        FlatBox(GPU_ShadeColor(RGB(80,60,40), shade), FX((double)tx), FX(1.85), FX(6.0), FX(0.7), FX(0.06), FX(0.06));
    }
    /* the far siding: scenery only, scrolls SLOWER for parallax depth */
    FlatBox(GPU_ShadeColor(p->ballast, 170), FX(-4), FX(0.02), FX(WO_SIDING_Z), FX(24), FX(0.06), FX(1.5));
    FlatBox(RGB(60,60,66), FX(-4), FX(0.07), FX(WO_SIDING_Z-0.3), FX(24), FX(0.06), FX(0.07));
    FlatBox(RGB(60,60,66), FX(-4), FX(0.07), FX(WO_SIDING_Z+0.3), FX(24), FX(0.06), FX(0.07));
    {
        int32_t off = Wrap((int32_t)(dist*WO_SIDING_SCROLL), 14);
        for (int16_t k = 0; k < 24; k++)
        {
            float sx = -14.0f + (float)(k*14 - off)/16.0f;
            if (sx > 6.0f) continue;
            FlatBox(GPU_ShadeColor(RGB(95,75,55), 170), FX((double)sx), FX(0.04), FX(WO_SIDING_Z), FX(0.3), FX(0.05), FX(1.1));
        }
    }
    /* the station */
    DrawStation3D(shade);
    /* trees between the tracks */
    for (uint8_t i = 0; i < 3; i++)
    {
        float tx = ScrollX(i*419 + 7, 0.9f);
        Vec3 tp = GPU3D_Vec(FX((double)tx), FX(0.4), FX(2.2));
        Vec3 ts = GPU3D_Vec(FX(0.14), FX(0.8), FX(0.14));
        GPU3D_DrawMeshTinted(&_cylMesh, &tp, &ts, 0, 0, 0, GPU_ShadeColor(RGB(80,60,40), shade), 0);
        Vec3 lp = GPU3D_Vec(FX((double)tx), FX(1.1), FX(2.2));
        Vec3 ls = GPU3D_Vec(FX(0.9), FX(0.9), FX(0.9));
        GPU3D_DrawMeshTinted(&GPU3D_BallMesh, &lp, &ls, 0, 0, 0, GPU_ShadeColor(p->near, 140), 0);
    }
    /* main line */
    FlatBox(p->ballast, FX(-4), FX(0.02), 0, FX(24), FX(0.06), FX(1.5));
    FlatBox(RGB(60,60,66), FX(-4), FX(0.07), FX(-0.3), FX(24), FX(0.06), FX(0.07));
    FlatBox(RGB(60,60,66), FX(-4), FX(0.07), FX(0.3), FX(24), FX(0.06), FX(0.07));
    {
        int32_t off = Wrap(dist, 14);
        for (int16_t k = 0; k < 24; k++)
        {
            float sx = -14.0f + (float)(k*14 - off)/16.0f;
            if (sx > 6.0f) continue;
            FlatBox(GPU_ShadeColor(RGB(95,75,55), shade), FX((double)sx), FX(0.04), 0, FX(0.3), FX(0.05), FX(1.1));
        }
    }
    /* coal lumps */
    for (uint8_t i = 0; i < WO_NUM_COAL; i++)
    {
        const Coal * c = &memory->coal[i];
        if (c->taken) continue;
        float rel = c->worldX - memory->dist;
        float mx = RelModel(rel);
        if (mx < -15.0f || mx > 7.0f) continue;
        fixed cx = FX((double)mx);
        Vec3 cp = GPU3D_Vec(cx, FX(0.2), 0);
        Vec3 cs = GPU3D_Vec(FX(0.4), FX(0.4), FX(0.4));
        GPU3D_DrawMeshTinted(&GPU3D_BallMesh, &cp, &cs, 0, 0, 0, RGB(25,25,30), 0);
        int16_t sx, sy;
        if (Project3D(cx, FX(0.25), 0, &sx, &sy))
        {
            GPU_DrawGlow(C_ORANGE, sx, sy, 6);
            GPU_DrawPixel(C_YELLOW, (uint16_t)(sx+2), (uint16_t)(sy-3));
        }
    }
}

static void DrawGauge(void)
{
    int16_t cx = GPU_X - 16, cy = 44;
    GPU_DrawFilledCircle(RGB(20,20,30), cx, cy, 10);
    GPU_DrawEmptyCircle(C_GRAY_LIGHT, cx, cy, 10);
    for (uint8_t i = 0; i <= 4; i++)
    {
        int32_t ang = ANG(-210) + (int32_t)i * ANG(60);
        int16_t dx = (int16_t)(FX_TO_INT(FX_MUL(GPU3D_Sin(ang), FX(7.0))));
        int16_t dy = (int16_t)(FX_TO_INT(FX_MUL(GPU3D_Cos(ang), FX(7.0))));
        GPU_Color zc = (i < 2) ? C_RED : (i < 3 ? C_YELLOW : C_GREEN);
        GPU_DrawPixel(zc, (uint16_t)(cx+dx), (uint16_t)(cy-dy));
    }
    int32_t ang = ANG(-210) + (int32_t)(memory->steam * ANG(240) / 100.0f);
    int16_t nx = (int16_t)(FX_TO_INT(FX_MUL(GPU3D_Sin(ang), FX(8.0))));
    int16_t ny = (int16_t)(FX_TO_INT(FX_MUL(GPU3D_Cos(ang), FX(8.0))));
    GPU_DrawLine(C_WHITE, cx, cy, (int16_t)(cx+nx), (int16_t)(cy-ny));
    GPU_DrawPixel(C_ORANGE, (uint16_t)cx, (uint16_t)cy);
}

static void DrawCargoCard(void)
{
    const FontDescription * fs = &fontDescription[0];
    int16_t w = (int16_t)(memory->target*30 + 16);
    int16_t x = (int16_t)(GPU_X/2 - w/2);
    GPU_DrawPanel(RGB(16,16,32), C_GRAY_MEDIUM, x, 4, w, 36);
    GPU_DrawText(C_GRAY_LIGHT, fs, (int16_t)(x+4), 6, "CARGO", 5);
    for (uint8_t i = 0; i < memory->target; i++)
    {
        int16_t bx = (int16_t)(x + 8 + i*30);
        GPU_Color bg = (i < memory->loaded) ? RGB(25,110,45) : RGB(40,40,60);
        GPU_DrawBevelSquare(bg, bx, 16, 26, 20);
        if (i < memory->loaded)
        {
            GPU_DrawFilledSquare(C_ORANGE, (int16_t)(bx+6), 21, 14, 10);
            GPU_DrawLine(RGB(120,80,30), (int16_t)(bx+6), 26, (int16_t)(bx+19), 26);
        }
        else if (i == memory->loaded && (memory->animTick & 8))
            GPU_DrawEmptySquare(C_YELLOW, (int16_t)(bx-1), 15, 28, 22);
    }
}

static void DrawHint(void)
{
    const FontDescription * fm = &fontDescription[1];
    char text[48];
    float remain = memory->stationX - memory->dist;
    if (memory->celebrate)
        strcpy(text, "delivered! hold RIGHT to depart");
    else if (remain > 0)
        sprintf(text, "drive RIGHT to the station (%um)", (unsigned)(remain/4));
    else
        sprintf(text, "station behind: hold LEFT to reverse");
    GPU_DrawTextOutlined(C_WHITE, C_BLACK, fm, GPU_X/2, 220, text);
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[48];
    const Palette * p = &_pal[memory->deliveries % 3];
    uint8_t night = (memory->deliveries % 3) == 2;
    int16_t shade = night ? 130 : 256;
    int32_t dist = (int32_t)memory->dist;
    float av = memory->v < 0 ? -memory->v : memory->v;

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(p->skyTop, p->skyBot, 0, 0, GPU_X, 150);
    if (night)
    {
        GPU_DrawStarfieldIn(91, 40, IF_GetCurrentTime(), GPU_X, 100);
        GPU_DrawFilledCircle(C_GRAY_LIGHT, 40, 28, 11);
        GPU_DrawFilledCircle(RGB(10,12,34), 46, 24, 10);
    }
    else
    {
        GPU_Color sun = (memory->deliveries % 3) == 1 ? C_ORANGE : C_YELLOW;
        GPU_DrawGlow(sun, 40, 30, 15);
        GPU_DrawFilledCircle(sun, 40, 30, 8);
    }
    for (uint8_t i = 0; i < 3; i++)
    {
        int16_t cx = (int16_t)(GPU_X + 90 - Wrap(dist/6 + i*137, GPU_X+90) - 45);
        int16_t cy = (int16_t)(46 + i*20);
        GPU_DrawFilledCircle(p->cloud, cx, cy, 10);
        GPU_DrawFilledCircle(p->cloud, (int16_t)(cx+11), cy, 7);
        GPU_DrawFilledCircle(p->cloud, (int16_t)(cx-11), cy, 7);
    }
    for (uint8_t i = 0; i < 6; i++)
    {
        int16_t hx = (int16_t)(GPU_X + 140 - Wrap(dist/4 + i*105, GPU_X+140) - 70);
        GPU_DrawFilledCircle(p->near, hx, 132, 26);
    }

    Setup3D(night);
    DrawWorld3D(p, shade);

    /* cargo wagons behind the loco (furthest slot first), then sliding ones */
    int32_t a3 = WheelAngle();
    for (int8_t s = (int8_t)(memory->numCargo-1); s >= 0; s--)
    {
        for (uint8_t i = 0; i < memory->numCargo; i++)
        {
            const Cargo * c = &memory->cargo[i];
            if (c->state == 2 && c->slot == s)
                DrawCargoWagon3D(c, FX(-4.1 - (double)s*1.75), a3, shade, 0, 0, FX_ONE);
        }
    }
    for (uint8_t i = 0; i < memory->numCargo; i++)
    {
        const Cargo * c = &memory->cargo[i];
        if (c->state != 1) continue;
        fixed t = FX((double)c->slideT/20.0);
        fixed slotX = FX(-4.1 - (double)c->slot*1.75);
        fixed yOff = FX_MUL(FX(1.5), FX_ONE - t);
        fixed scl = FX(0.8) + FX_MUL(FX(0.2), t);
        DrawCargoWagon3D(c, slotX, a3, shade, yOff, 0, scl);
    }
    DrawLoco3D(night);

    for (uint8_t i = 0; i < WO_NUM_PUFFS; i++)
    {
        const Puff * pf = &memory->puffs[i];
        if (pf->age >= 40) continue;
        int16_t r = (int16_t)(2 + pf->age/7);
        GPU_Color c = GPU_ShadeColor(C_GRAY_LIGHT, (int16_t)(230 - pf->age*5));
        GPU_DrawFilledCircle(c, (int16_t)pf->x, (int16_t)pf->y, r);
    }
    if (memory->whistleTicks)
    {
        int16_t nx = 240, ny = (int16_t)(140 - (24 - memory->whistleTicks)*2);
        GPU_DrawFilledCircle(C_WHITE, nx, ny, 2);
        GPU_DrawLine(C_WHITE, (int16_t)(nx+2), ny, (int16_t)(nx+2), (int16_t)(ny-7));
        GPU_DrawFilledCircle(C_WHITE, (int16_t)(nx+10), (int16_t)(ny-6), 2);
        GPU_DrawLine(C_WHITE, (int16_t)(nx+12), (int16_t)(ny-6), (int16_t)(nx+12), (int16_t)(ny-13));
    }

    /* HUD */
    if (!memory->puzzleActive)
        DrawCargoCard();
    sprintf(text, "SCORE %06u", (unsigned)memory->score);
    GPU_DrawText(C_WHITE, fs, 4, 4, text, (uint16_t)strlen(text));
    sprintf(text, "BEST %06u", (unsigned)bestScore);
    GPU_DrawText(C_GRAY_LIGHT, fs, 4, 14, text, (uint16_t)strlen(text));
    sprintf(text, "%s%u km/h", memory->v < -0.5f ? "R " : "", (unsigned)(av*0.5f + 0.5f));
    GPU_DrawText(memory->v > MaxV()*0.9f ? C_ORANGE : C_WHITE, fs,
                 (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    sprintf(text, "DELIVERED %u", (unsigned)memory->deliveries);
    GPU_DrawText(C_GRAY_LIGHT, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 14, text, (uint16_t)strlen(text));
    DrawGauge();
    /* off-screen station marker */
    if (memory->state == 1 && !memory->puzzleActive && !memory->celebrate && !memory->stationDone)
    {
        float rel = memory->stationX - memory->dist;
        int16_t sx, sy;
        fixed cx = FX((double)RelModel(rel));
        if (Project3D(cx, FX(1.0), 0, &sx, &sy) && (memory->animTick & 8))
        {
            if (sx > GPU_X)
                GPU_DrawFilledTriangle(C_YELLOW, GPU_X-14, 108, GPU_X-14, 124, GPU_X-2, 116);
            else if (sx < 0)
                GPU_DrawFilledTriangle(C_YELLOW, 14, 108, 14, 124, 2, 116);
        }
    }
    GPU_DrawText(GPU_ShadeColor(p->ground, 140), fs, 6, GPU_Y-10, "BRAKE/REV", 9);
    GPU_DrawText(GPU_ShadeColor(p->ground, 140), fs, GPU_X-42, GPU_Y-10, "POWER", 5);

    if (memory->popupTicks && !memory->puzzleActive)
    {
        static const char * const msg[7] = {
            "WAGON LOADED!", "OOPS! REVERSE BACK", "", "ORDER DELIVERED!", "", "+STEAM!", "TOOT TOOT!"
        };
        static const GPU_Color col[7] = { C_C3, C_ORANGE, C_WHITE, C_YELLOW, C_WHITE, C_ORANGE, C_WHITE };
        strcpy(text, msg[memory->popupType]);
        GPU_DrawTextOutlined(col[memory->popupType], C_BLACK, fb, GPU_X/2, 56, text);
    }

    if (memory->puzzleActive)
        DrawPuzzlePanel();
    else if (memory->state == 1)
        DrawHint();

    if (memory->state == 0)
    {
        GPU_DrawPanel(RGB(16,16,32), C_YELLOW, 20, 44, 280, 92);
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 50, "WAGON ORDER");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 72, "drive RIGHT to each station and STOP");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 86, "solve the puzzle to load a wagon");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 100, "fill the train to deliver - 8 puzzles!");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 114, "LEFT brakes/reverses - DOWN whistle - coal = steam");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 126, "tap an answer, or LEFT/RIGHT + UP");
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 142, "press a key or tap to depart");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G50_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };