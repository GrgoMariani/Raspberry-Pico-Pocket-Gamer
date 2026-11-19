// NUMBER TRAIN - drive a steam locomotive, halt it on the station mark and
// load the right number of friends aboard: a counting game for young kids.
// RIGHT powers the loco, LEFT brakes (touch: right half powers, left half
// brakes). The station sign shows a number (1..3, later up to 10); every tap
// or key press loads one friend while the counter counts along. There is no
// losing: missing a stop just brings the next station, so it stays
// encouraging in a classroom / kindergarten setting.

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 30
#define TS_RAIL_Y 196            /* top of the rail on screen */
#define TS_FRONT_X 118           /* screen x of the loco's front coupler */
#define TS_ACCEL 66.0f           /* px/s^2 with the throttle open */
#define TS_BRAKE 150.0f          /* px/s^2 with the brake on */
#define TS_FRICTION 12.0f        /* px/s^2 coasting */
#define TS_STOP_EPS 0.6f         /* below this speed the loco counts as stopped */
#define TS_NUM_PUFFS 12
#define TS_MAX_FRIENDS 10        /* biggest counting goal */

typedef struct {
    float x, y;
    uint8_t age;                 /* 40 = expired */
} Puff;

typedef struct {
    float dist;                  /* world x of the loco's front */
    float v;                     /* px/s */
    float stationX;              /* world x of the stop marker */
    float zoneHalf;              /* half width of the stop zone */
    float oldStationX;           /* previous station, scrolls off behind */
    uint8_t oldValid;
    uint8_t stationDone;         /* current station scored or missed */
    uint8_t celebrate;           /* ticks of the stop celebration */
    uint32_t score;
    uint16_t stops;
    uint8_t level;
    uint8_t state;               /* 0 ready, 1 driving */
    uint8_t boarding;            /* halted at the station, loading friends */
    uint8_t target;              /* how many friends wait at this station */
    uint8_t loaded;              /* friends already on the wagon */
    uint8_t popupTicks;
    uint8_t popupType;           /* 0 perfect, 1 great, 2 good, 3 missed, 4 done */
    uint8_t animTick;
    uint8_t braking;             /* brake held this tick (for sparks) */
    Puff puffs[TS_NUM_PUFFS];
} Game40Memory;

static Game40Memory * const memory = (Game40Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestScore = 0;

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static float MaxV(void)
{
    float m = 240.0f + 18.0f*(float)(memory->level-1);
    return m > 330.0f ? 330.0f : m;
}

static void SpawnStation(void)
{
    memory->stationX = memory->dist + 1300.0f + (float)Random_Range(700);
    /* generous stop zone for small engineers */
    float zh = 44.0f - 2.0f*(float)(memory->level-1);
    memory->zoneHalf = zh < 30.0f ? 30.0f : zh;
    memory->stationDone = 0;
    memory->boarding = 0;
    memory->loaded = 0;
    /* the counting goal grows gently: 1..3, then 1..5, then 1..10 */
    uint32_t maxN = memory->level <= 2 ? 3 : (memory->level <= 4 ? 5 : 10);
    memory->target = (uint8_t)(1 + Random_Range(maxN));
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game40Memory));
    memory->level = 1;
    memory->state = 0;
    for (uint8_t i = 0; i < TS_NUM_PUFFS; i++)
        memory->puffs[i].age = 40;
    SpawnStation();
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void StartRun(void)
{
    memory->state = 1;
}

static void SpawnPuff(void)
{
    for (uint8_t i = 0; i < TS_NUM_PUFFS; i++)
        if (memory->puffs[i].age >= 40)
        {
            memory->puffs[i].x = 103.0f + (float)Random_Range(5) - 2.0f;
            memory->puffs[i].y = 150.0f;
            memory->puffs[i].age = 0;
            return;
        }
}

static void ScoreStop(float d)
{
    uint32_t base;
    if (d <= 10.0f)      { base = 300; memory->popupType = 0; }
    else if (d <= 24.0f) { base = 150; memory->popupType = 1; }
    else                 { base = 75;  memory->popupType = 2; }
    memory->score += base + 25u*(memory->level-1);
    if (memory->score > bestScore)
        bestScore = memory->score;
    memory->popupTicks = 40;
    memory->stops++;
    memory->level = (uint8_t)(memory->stops/3 + 1);
    if (memory->level > 9)
        memory->level = 9;
    memory->stationDone = 1;
    memory->boarding = 1;
    memory->v = 0.0f;
    /* a happy little steam burst */
    SpawnPuff();
    SpawnPuff();
}

/* one tap / key press = one friend climbing aboard, counting out loud on
   screen. Extra taps after the goal are simply ignored: no way to fail. */
static void LoadOne(void)
{
    if (!memory->boarding || memory->celebrate || memory->loaded >= memory->target)
        return;
    memory->loaded++;
    SpawnPuff();
    if (memory->loaded == memory->target)
    {
        memory->score += 100u + 25u*(memory->level-1);
        if (memory->score > bestScore)
            bestScore = memory->score;
        memory->popupType = 4;
        memory->popupTicks = 60;
        memory->celebrate = 60;
        SpawnPuff();
    }
}

static void Tick(void)
{
    memory->animTick++;

    /* smoke drifts even on the ready screen (idle chuffing) */
    uint8_t throttle = 0, brake = 0;
    if (memory->state == 1 && !memory->celebrate && !memory->boarding)
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
    uint8_t puffEvery = throttle ? 3 : (memory->v > 1.0f ? 6 : 14);
    if ((memory->animTick % puffEvery) == 0)
        SpawnPuff();
    for (uint8_t i = 0; i < TS_NUM_PUFFS; i++)
    {
        Puff * p = &memory->puffs[i];
        if (p->age >= 40)
            continue;
        p->age++;
        p->x -= memory->v/(float)NUM_TICKS*0.85f + 0.4f;
        p->y -= 0.55f;
    }

    if (memory->state != 1)
        return;
    if (memory->popupTicks)
        memory->popupTicks--;

    if (memory->celebrate)
    {
        memory->celebrate--;
        if (!memory->celebrate)
        {
            memory->oldStationX = memory->stationX;
            memory->oldValid = 1;
            SpawnStation();
        }
        return;
    }
    /* while boarding the train waits patiently at the platform */
    if (memory->boarding)
        return;

    /* longitudinal physics */
    if (throttle)
        memory->v += TS_ACCEL/NUM_TICKS;
    else if (brake)
        memory->v -= TS_BRAKE/NUM_TICKS;
    else
        memory->v -= TS_FRICTION/NUM_TICKS;
    if (memory->v < TS_STOP_EPS && !throttle)
        memory->v = 0.0f;
    if (memory->v > MaxV())
        memory->v = MaxV();
    memory->dist += memory->v/NUM_TICKS;

    if (memory->oldValid && memory->oldStationX < memory->dist - 400.0f)
        memory->oldValid = 0;

    if (!memory->stationDone)
    {
        float d = memory->dist - memory->stationX;
        if (d < 0.0f) d = -d;
        if (memory->v == 0.0f && d <= memory->zoneHalf)
        {
            ScoreStop(d);
        }
        else if (memory->dist > memory->stationX + memory->zoneHalf)
        {
            /* sailed past the platform: no penalty for young engineers,
               just a gentle nudge and the next station */
            memory->stationDone = 1;
            memory->popupType = 3;
            memory->popupTicks = 40;
            memory->oldStationX = memory->stationX;
            memory->oldValid = 1;
            SpawnStation();
        }
    }
}

static void G_Update(void)
{
    /* press edges start the run and load friends; held keys drive. The latch
       is drained every iteration so a stale press cannot load a friend. */
    uint8_t anyPress = (Keyboard_GetPressedKeys() != KEY_NONE) ? 1 : 0;
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
        anyPress = 1;

    if (memory->state == 0)
    {
        if (anyPress)
            StartRun();
    }
    else if (memory->boarding && anyPress)
    {
        LoadOne();
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

typedef struct {
    GPU_Color skyTop, skyBot, far, near, ground, ballast, cloud;
} Palette;

static const Palette _pal[3] = {
    { RGB(60,120,200), RGB(160,200,235), RGB(110,140,170), RGB(90,150,95),
      RGB(120,160,90), RGB(125,120,115), C_WHITE },                        /* day */
    { RGB(70,50,110), RGB(240,140,80), RGB(95,75,115), RGB(75,95,75),
      RGB(115,110,75), RGB(115,105,98), RGB(250,195,155) },                /* sunset */
    { RGB(4,4,22), RGB(24,28,64), RGB(32,36,62), RGB(28,42,38),
      RGB(42,54,46), RGB(72,70,80), RGB(42,46,82) },                       /* night */
};

/* 8 spoke directions (radius 6) and coupling-rod pin offsets (radius 4) */
static const int8_t _dir[8][2] = { {6,0},{4,4},{0,6},{-4,4},{-6,0},{-4,-4},{0,-6},{4,-4} };
static const int8_t _pin[8][2] = { {4,0},{3,3},{0,4},{-3,3},{-4,0},{-3,-3},{0,-4},{3,-3} };

/* every friend gets a stable bright shirt colour so kids can track them */
static const GPU_Color _friendCol[TS_MAX_FRIENDS] = {
    C_RED, C_C1, C_GREEN, C_YELLOW, C_ORANGE,
    C_PURPLE, C_CYAN, C_C2, C_C3, C_WHITE
};

static void DrawFriend(int16_t x, int16_t footY, GPU_Color col, uint8_t wave)
{
    GPU_DrawFilledSquare(col, (int16_t)(x-3), (int16_t)(footY-9), 6, 9);
    GPU_DrawFilledCircle(RGB(250,210,170), x, (int16_t)(footY-12), 3);
    GPU_DrawPixel(C_BLACK, (uint16_t)(x-1), (uint16_t)(footY-13));
    GPU_DrawPixel(C_BLACK, (uint16_t)(x+1), (uint16_t)(footY-13));
    if (wave)
        GPU_DrawLine(col, (int16_t)(x+3), (int16_t)(footY-8), (int16_t)(x+6), (int16_t)(footY-14));
}

static void DrawStation(float worldX, uint8_t current)
{
    int16_t c = (int16_t)(worldX - memory->dist + TS_FRONT_X);
    if (c < -170 || c > GPU_X+170)
        return;
    uint8_t night = (memory->stops % 3) == 2;
    int16_t shade = night ? 130 : 256;
    /* platform (it reaches well past the stop flag so all ten friends fit) */
    GPU_DrawFilledSquare(GPU_ShadeColor(RGB(155,150,145), shade), (int16_t)(c-70), 178, 220, 18);
    GPU_DrawLine(GPU_ShadeColor(C_YELLOW, shade), (int16_t)(c-70), 179, (int16_t)(c+150), 179);
    /* building */
    GPU_DrawFilledSquare(GPU_ShadeColor(RGB(185,145,105), shade), (int16_t)(c-28), 134, 56, 44);
    GPU_DrawFilledTriangle(GPU_ShadeColor(RGB(125,60,50), shade), (int16_t)(c-34), 134, (int16_t)(c+34), 134, c, 116);
    GPU_DrawFilledSquare(GPU_ShadeColor(RGB(70,50,40), shade), (int16_t)(c-6), 164, 12, 14);
    /* the big sign shows HOW MANY friends are waiting: the counting goal */
    GPU_DrawFilledSquare(RGB(30,30,40), (int16_t)(c-25), 136, 50, 28);
    GPU_DrawEmptySquare(GPU_ShadeColor(C_YELLOW, shade), (int16_t)(c-25), 136, 50, 28);
    if (current)
    {
        char num[4];
        sprintf(num, "%u", (unsigned)memory->target);
        GPU_DrawTextCentered(GPU_ShadeColor(C_YELLOW, shade), &fontDescription[3], c, 137, num);
    }
    else
    {
        GPU_DrawTextCentered(C_WHITE, &fontDescription[0], c, 146, "BYE!");
    }
    /* stop marker: a flag on a pole at the exact stopping point */
    GPU_DrawLine(C_GRAY_LIGHT, c, 164, c, TS_RAIL_Y);
    GPU_DrawFilledTriangle(current ? C_YELLOW : C_GRAY_MEDIUM, c, 164, (int16_t)(c+12), 168, c, 172);
    /* the friends still waiting on the platform (count them!): they queue up
       AHEAD of the stop flag so the halted loco never hides them */
    if (current)
    {
        for (uint8_t i = memory->loaded; i < memory->target && i < TS_MAX_FRIENDS; i++)
        {
            int16_t fx = (int16_t)(c + 16 + i*13);
            uint8_t wave = (uint8_t)(((memory->animTick/8) + i) & 1);
            DrawFriend(fx, 178, GPU_ShadeColor(_friendCol[i], shade), wave);
        }
    }
}

/* the stop zone is drawn ON TOP of the track so the sleepers cannot hide it */
static void DrawZone(void)
{
    if (memory->stationDone && !memory->celebrate && !memory->boarding)
        return;
    int16_t c = (int16_t)(memory->stationX - memory->dist + TS_FRONT_X);
    if (c < -170 || c > GPU_X+170)
        return;
    int16_t zs = (int16_t)(c - memory->zoneHalf);
    int16_t ze = (int16_t)(c + memory->zoneHalf);
    GPU_DrawFilledSquare(RGB(0,110,0), zs, 197, (int16_t)(ze-zs), 5);
    GPU_DrawFilledSquare(C_GREEN, (int16_t)(zs-1), 190, 2, 14);
    GPU_DrawFilledSquare(C_GREEN, ze, 190, 2, 14);
}

static void DrawWheel(int16_t cx, int16_t cy, int16_t r, uint8_t idx)
{
    GPU_DrawFilledCircle(RGB(30,30,36), cx, cy, r);
    GPU_DrawEmptyCircle(C_GRAY_MEDIUM, cx, cy, r);
    if (r >= 7)
    {
        GPU_DrawLine(C_GRAY_MEDIUM, (int16_t)(cx-_dir[idx][0]), (int16_t)(cy-_dir[idx][1]),
                     (int16_t)(cx+_dir[idx][0]), (int16_t)(cy+_dir[idx][1]));
        uint8_t j = (uint8_t)((idx+2) & 7);
        GPU_DrawLine(C_GRAY_MEDIUM, (int16_t)(cx-_dir[j][0]), (int16_t)(cy-_dir[j][1]),
                     (int16_t)(cx+_dir[j][0]), (int16_t)(cy+_dir[j][1]));
    }
    else
    {
        GPU_DrawLine(C_GRAY_MEDIUM, (int16_t)(cx-_dir[idx][0]*2/3), (int16_t)(cy-_dir[idx][1]*2/3),
                     (int16_t)(cx+_dir[idx][0]*2/3), (int16_t)(cy+_dir[idx][1]*2/3));
    }
    GPU_DrawPixel(C_GRAY_LIGHT, (uint16_t)cx, (uint16_t)cy);
}

/* the open wagon behind the loco fills up with the loaded friends */
static void DrawWagon(void)
{
    uint8_t idx = (uint8_t)(((int32_t)(memory->dist/4.0f)) & 7);
    DrawWheel(-30, 191, 5, idx);
    DrawWheel(16, 191, 5, idx);
    /* bed + walls */
    GPU_DrawFilledSquare(RGB(70,50,35), -50, 183, 80, 6);
    GPU_DrawFilledSquare(RGB(150,110,60), -50, 172, 80, 11);
    GPU_DrawLine(RGB(190,150,90), -50, 172, 29, 172);
    /* coupler to the loco */
    GPU_DrawLine(C_GRAY_MEDIUM, 30, 186, 38, 186);
    /* the friends already aboard peek over the wall */
    for (uint8_t i = 0; i < memory->loaded && i < TS_MAX_FRIENDS; i++)
    {
        int16_t fx = (int16_t)(-44 + i*8);
        GPU_DrawFilledSquare(_friendCol[i], (int16_t)(fx-3), 170, 6, 3);
        GPU_DrawFilledCircle(RGB(250,210,170), fx, 167, 3);
        GPU_DrawPixel(C_BLACK, (uint16_t)(fx-1), 166);
        GPU_DrawPixel(C_BLACK, (uint16_t)(fx+1), 166);
    }
}

static void DrawLoco(void)
{
    uint8_t idx = (uint8_t)(((int32_t)(memory->dist/4.0f)) & 7);
    uint8_t night = (memory->stops % 3) == 2;
    /* wheels */
    DrawWheel(46, 191, 5, idx);
    DrawWheel(108, 191, 5, idx);
    DrawWheel(66, 188, 8, idx);
    DrawWheel(88, 188, 8, idx);
    /* chassis */
    GPU_DrawFilledSquare(RGB(25,25,32), 38, 183, 74, 6);
    /* coupling rod between the two drive wheels */
    GPU_DrawLine(C_GRAY_LIGHT, (int16_t)(66+_pin[idx][0]), (int16_t)(188+_pin[idx][1]),
                 (int16_t)(88+_pin[idx][0]), (int16_t)(188+_pin[idx][1]));
    GPU_DrawLine(C_GRAY_LIGHT, (int16_t)(66+_pin[idx][0]), (int16_t)(189+_pin[idx][1]),
                 (int16_t)(88+_pin[idx][0]), (int16_t)(189+_pin[idx][1]));
    /* boiler with bands */
    GPU_DrawFilledSquare(RGB(150,45,55), 52, 167, 56, 17);
    GPU_DrawLine(RGB(195,75,85), 52, 167, 107, 167);
    GPU_DrawFilledSquare(RGB(190,70,80), 72, 167, 2, 17);
    GPU_DrawFilledSquare(RGB(190,70,80), 90, 167, 2, 17);
    /* smokebox + chimney */
    GPU_DrawFilledSquare(RGB(40,40,48), 100, 165, 10, 19);
    GPU_DrawFilledSquare(RGB(20,20,26), 101, 153, 6, 13);
    GPU_DrawFilledSquare(RGB(20,20,26), 99, 151, 10, 4);
    /* steam dome */
    GPU_DrawFilledCircle(RGB(200,160,60), 82, 166, 4);
    /* cab */
    GPU_DrawFilledSquare(RGB(120,35,45), 34, 152, 24, 32);
    GPU_DrawFilledSquare(RGB(25,25,32), 30, 149, 32, 5);
    GPU_DrawFilledSquare(night ? RGB(250,220,120) : RGB(140,200,230), 39, 158, 14, 11);
    /* cowcatcher + buffer beam + headlight */
    GPU_DrawFilledTriangle(RGB(60,60,68), 110, 195, 121, 195, 110, 185);
    GPU_DrawFilledSquare(RGB(160,40,40), 110, 180, 4, 5);
    if (night)
        GPU_DrawGlow(C_YELLOW, 113, 172, 7);
    GPU_DrawFilledSquare(C_YELLOW, 110, 170, 4, 5);
    /* brake sparks at the drive wheels */
    if (memory->braking)
    {
        for (uint8_t i = 0; i < 4; i++)
        {
            int16_t sx = (int16_t)(58 + ((memory->animTick*7 + i*13) % 42));
            int16_t sy = (int16_t)(193 + ((memory->animTick + i) % 3));
            GPU_DrawPixel((i & 1) ? C_ORANGE : C_YELLOW, (uint16_t)sx, (uint16_t)sy);
        }
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    const Palette * p = &_pal[memory->stops % 3];
    uint8_t night = (memory->stops % 3) == 2;
    int32_t dist = (int32_t)memory->dist;

    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(p->skyTop, p->skyBot, 0, 0, GPU_X, TS_RAIL_Y);
    if (night)
    {
        GPU_DrawStarfieldIn(91, 40, IF_GetCurrentTime(), GPU_X, 110);
        GPU_DrawFilledCircle(C_GRAY_LIGHT, 272, 32, 11);
        GPU_DrawFilledCircle(RGB(10,12,34), 278, 28, 10);
    }
    else
    {
        GPU_Color sun = (memory->stops % 3) == 1 ? C_ORANGE : C_YELLOW;
        GPU_DrawGlow(sun, 272, 34, 15);
        GPU_DrawFilledCircle(sun, 272, 34, 8);
    }
    /* clouds */
    for (uint8_t i = 0; i < 3; i++)
    {
        int16_t cx = (int16_t)((GPU_X + 90 - (dist/6 + i*137) % (GPU_X+90)) - 45);
        int16_t cy = (int16_t)(30 + i*26);
        GPU_DrawFilledCircle(p->cloud, cx, cy, 10);
        GPU_DrawFilledCircle(p->cloud, (int16_t)(cx+11), cy, 7);
        GPU_DrawFilledCircle(p->cloud, (int16_t)(cx-11), cy, 7);
    }
    /* far mountains */
    for (uint8_t i = 0; i < 5; i++)
    {
        int16_t mx = (int16_t)((GPU_X + 180 - (dist/8 + i*127) % (GPU_X+180)) - 90);
        GPU_DrawFilledTriangle(p->far, mx, TS_RAIL_Y, (int16_t)(mx+100), TS_RAIL_Y,
                               (int16_t)(mx+50), (int16_t)(116 + (i%3)*16));
    }
    /* near hills */
    for (uint8_t i = 0; i < 6; i++)
    {
        int16_t hx = (int16_t)((GPU_X + 140 - (dist/4 + i*105) % (GPU_X+140)) - 70);
        GPU_DrawFilledCircle(p->near, hx, TS_RAIL_Y, 34);
    }
    /* trees */
    for (uint8_t i = 0; i < 5; i++)
    {
        int16_t tx = (int16_t)((GPU_X + 120 - (dist*4/5 + i*97) % (GPU_X+120)) - 60);
        GPU_Color leaf = GPU_ShadeColor(p->near, 140);
        GPU_DrawFilledSquare(RGB(80,60,40), (int16_t)(tx-1), 184, 3, 12);
        GPU_DrawFilledCircle(leaf, tx, 178, 9);
    }
    /* ground + ballast */
    GPU_DrawFilledSquare(p->ground, 0, TS_RAIL_Y, GPU_X, GPU_Y-TS_RAIL_Y);
    GPU_DrawFilledSquare(p->ballast, 0, TS_RAIL_Y, GPU_X, 12);
    /* stations (behind the track) */
    if (memory->oldValid)
        DrawStation(memory->oldStationX, 0);
    DrawStation(memory->stationX, 1);
    /* sleepers + rail */
    {
        int16_t off = (int16_t)(dist % 14);
        GPU_Color tie = GPU_ShadeColor(RGB(95,75,55), night ? 130 : 256);
        for (int16_t x = (int16_t)-off; x < GPU_X; x += 14)
            GPU_DrawFilledSquare(tie, x, 198, 5, 6);
        GPU_DrawFilledSquare(RGB(60,60,66), 0, 195, GPU_X, 2);
        GPU_DrawLine(RGB(150,150,158), 0, 195, GPU_X-1, 195);
    }
    DrawZone();
    DrawWagon();
    /* smoke behind the loco */
    for (uint8_t i = 0; i < TS_NUM_PUFFS; i++)
    {
        const Puff * pf = &memory->puffs[i];
        if (pf->age >= 40)
            continue;
        int16_t r = (int16_t)(2 + pf->age/7);
        GPU_Color c = GPU_ShadeColor(C_GRAY_LIGHT, (int16_t)(230 - pf->age*5));
        GPU_DrawFilledCircle(c, (int16_t)pf->x, (int16_t)pf->y, r);
    }
    DrawLoco();

    /* HUD */
    sprintf(text, "SCORE %06u", (unsigned)memory->score);
    GPU_DrawText(C_WHITE, fm, 4, 4, text, (uint16_t)strlen(text));
    sprintf(text, "BEST %06u", (unsigned)bestScore);
    GPU_DrawText(C_GRAY_LIGHT, fs, 4, 16, text, (uint16_t)strlen(text));
    /* speed */
    sprintf(text, "%u km/h", (unsigned)(memory->v*0.5f + 0.5f));
    GPU_DrawText(memory->v > MaxV()*0.9f ? C_ORANGE : C_WHITE, fm,
                 (int16_t)(GPU_X - 6 - strlen(text)*fm->width), 16, text, (uint16_t)strlen(text));
    /* stations served */
    sprintf(text, "STOPS %u", (unsigned)memory->stops);
    GPU_DrawText(C_GRAY_LIGHT, fs, (int16_t)(GPU_X - 4 - strlen(text)*fs->width), 4, text, (uint16_t)strlen(text));
    /* distance to the marker + progress */
    if (memory->state == 1 && !memory->stationDone)
    {
        int32_t remain = (int32_t)(memory->stationX - memory->dist);
        if (remain < 0) remain = 0;
        sprintf(text, "LV %u  NEXT %um", memory->level, (unsigned)(remain/4));
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, 160, 16, text);
        uint16_t frac = (uint16_t)(256 - remain*256/1600);
        if (frac > 256) frac = 0;
        GPU_DrawProgressBar(120, 4, 80, 9, frac, C_C3);
    }
    /* touch hints */
    GPU_DrawText(GPU_ShadeColor(p->ground, 140), fs, 6, GPU_Y-10, "BRAKE", 5);
    GPU_DrawText(GPU_ShadeColor(p->ground, 140), fs, GPU_X-42, GPU_Y-10, "POWER", 5);

    /* boarding: a big count-along panel, one tap = one friend */
    if (memory->boarding && !memory->celebrate)
    {
        GPU_DrawPanel(RGB(20,24,60), C_YELLOW, 70, 34, 180, 64);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 40, "TAP TO LOAD THE FRIENDS!");
        sprintf(text, "%u / %u", (unsigned)memory->loaded, (unsigned)memory->target);
        GPU_DrawTextCentered(C_YELLOW, &fontDescription[3], GPU_X/2, 58, text);
    }

    /* popups */
    if (memory->popupTicks)
    {
        static const char * const msg[5] = { "PERFECT!", "GREAT!", "GOOD", "OOPS! NEXT STOP", "WELL DONE!" };
        static const GPU_Color col[5] = { C_YELLOW, C_C3, C_CYAN, C_ORANGE, C_YELLOW };
        int16_t py = memory->boarding ? 110 : 84;
        GPU_DrawTextOutlined(col[memory->popupType], C_BLACK, fb, GPU_X/2, py, msg[memory->popupType]);
        if (memory->popupType == 4 && memory->celebrate && memory->celebrate < 40)
            GPU_DrawTextCentered(C_WHITE, fs, GPU_X/2, 134, "hold RIGHT to depart");
    }

    if (memory->state == 0)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 56, "NUMBER TRAIN");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 88, "hold RIGHT = power, LEFT = brake");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 102, "stop on the green zone");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 116, "the sign says how many friends wait");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, 130, "tap once for each friend to load");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fs, GPU_X/2, 146, "touch: left half brakes, right half powers");
        if ((IF_GetCurrentTime()/500000) & 1)
            GPU_DrawTextCentered(C_ORANGE, fm, GPU_X/2, 162, "press a key or tap to depart");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G40_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };