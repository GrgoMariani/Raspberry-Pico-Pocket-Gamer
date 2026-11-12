// PINBALL - gravity, bumpers, targets, a kickout saucer and two flippers

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/*
 * A portrait table on the left, a score panel on the right. The ball is fired
 * up the launch lane on the right edge and drops into a busy playfield: three
 * big bumpers, two mini bumpers, a bank of three drop targets, two lane posts,
 * three top rollovers and a centre kickout saucer. Two flippers guard the
 * drain. LEFT/RIGHT (or the left/right half of the table on touch) work the
 * flippers; DOWN/UP or the LAUNCH button charges the plunger and releases the
 * ball. Lighting the rollovers raises the multiplier, clearing the drop
 * targets pays a bonus, and the saucer kicks the ball back up the table.
 *
 * Tuned to be forgiving: long flippers over a narrow drain, a strong flip
 * impulse, gentle gravity and a few seconds of ball save after every launch.
 * Physics runs at 60 Hz, each tick split into sub-steps so a fast ball can
 * never tunnel through a wall or a flipper.
 */

#define NUM_TICKS 60

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* ---- table geometry (screen pixels) ---- */
#define TX      6           /* playfield left wall            */
#define RX      202         /* launch lane right wall         */
#define TY      6           /* top wall                       */
#define BY      234         /* bottom (drain)                 */
#define LX      178         /* launch lane inner wall         */
#define LANE_CX 190         /* ball x while in the launch lane*/

#define BALL_R  4.0f
#define GRAV    0.10f       /* gentle gravity = more reaction time */
#define MAXS    6.5f
#define DAMP    0.999f

/* flippers: long blades over a narrow drain, strong throw */
#define LPX     44.0f
#define RPX     140.0f
#define FPY     216.0f
#define FLEN    44.0f
#define FREST   0.44f       /* radians below horizontal       */
#define FACTIVE (-0.62f)    /* radians above horizontal       */
#define OMEGA   0.34f       /* flipper angular speed (rad/tick)*/
#define FWIDTH  2.5f

#define NBUMP   3
#define NMINI   2
#define NTARG   3
#define NPOST   2
#define NSLING  2
#define NROLL   3
#define NBALLS  3
#define BALL_SAVE_TICKS (4*NUM_TICKS)

typedef enum {
    PB_READY = 0,       /* ball in the lane, charge the plunger   */
    PB_LAUNCH,          /* scripted ride up the lane              */
    PB_PLAY,            /* live physics                           */
    PB_DRAIN,           /* ball lost pause                        */
    PB_OVER
} PBState;

typedef struct {
    float x, y, vx, vy;
} Ball;

typedef struct {
    Ball ball;
    float trail_x[6], trail_y[6];
    uint8_t trailIdx;
    float ltheta, rtheta;       /* flipper angles                 */
    float langv, rangv;         /* flipper angular velocity       */
    float charge;               /* plunger charge 0..1            */
    uint8_t prevPlunger;
    uint16_t launchTick;        /* progress of the lane ride      */
    float launchPower;
    uint16_t ballSave;          /* ticks of free-drain grace      */
    uint32_t score;
    uint8_t ballNum;            /* 1..NBALLS                      */
    uint8_t mult;               /* score multiplier               */
    uint8_t rollLit[NROLL];
    uint8_t rollCool[NROLL];
    uint8_t targLit[NTARG];
    uint8_t targCool[NTARG];
    uint8_t postCool[NPOST];
    uint8_t bumpFlash[NBUMP];
    uint8_t miniFlash[NMINI];
    uint8_t slingFlash[NSLING];
    uint8_t postFlash[NPOST];
    uint8_t saucerFlash;
    uint8_t saucerCool;
    uint8_t stateTick;
    uint8_t msgTick;            /* centre message timer           */
    uint8_t msgKind;            /* 1 lost,2 mult,3 saved,4 targets*/
    int16_t popX, popY;         /* floating "+points" popup       */
    uint16_t popVal;
    uint8_t popTick;
    PBState state;
} Game32Memory;

static Game32Memory * const memory = (Game32Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t highScore = 0;

/* big bumpers */
static const float BX[NBUMP]  = { 62.0f, 122.0f, 92.0f };
static const float BY_[NBUMP] = { 72.0f, 64.0f, 108.0f };
static const float BR[NBUMP]  = { 13.0f, 13.0f, 13.0f };
/* mini bumpers (right side) */
static const float MX[NMINI]  = { 152.0f, 160.0f };
static const float MY[NMINI]  = { 108.0f, 132.0f };
static const float MR[NMINI]  = { 8.0f, 8.0f };
/* drop target bank (left side) */
static const float TGX[NTARG] = { 18.0f, 18.0f, 18.0f };
static const float TGY[NTARG] = { 110.0f, 126.0f, 142.0f };
#define TGR 6.0f
/* lane posts (top corners) */
static const float PSTX[NPOST] = { 30.0f, 154.0f };
static const float PSTY[NPOST] = { 60.0f, 60.0f };
#define PR 3.0f
/* centre kickout saucer */
#define SAUC_X 92.0f
#define SAUC_Y 152.0f
#define SAUC_R 7.0f
/* rollovers */
static const float RX_[NROLL] = { 48.0f, 92.0f, 136.0f };
static const float RY_[NROLL] = { 30.0f, 24.0f, 30.0f };
/* slingshot segments */
static const float SL[NSLING][4] = {
    { 30.0f, 150.0f, 46.0f, 176.0f },
    { 162.0f, 150.0f, 146.0f, 176.0f }
};

/* static wall segments: ax,ay,bx,by,restitution */
typedef struct { float ax, ay, bx, by, rest; } Seg;
static const Seg _walls[] = {
    { TX,  TY,  TX,  176.0f, 0.5f },   /* left wall              */
    { TX,  TY,  LX,  TY,     0.5f },   /* top wall               */
    { LX,  TY,  LX,  176.0f, 0.5f },   /* launch lane inner wall */
    { RX,  TY,  RX,  BY,     0.5f },   /* lane right wall        */
    { TX,  176.0f, LPX, FPY, 0.4f },   /* left funnel            */
    { LX,  176.0f, RPX, FPY, 0.4f }    /* right funnel           */
};
#define NWALL ((int)(sizeof(_walls)/sizeof(_walls[0])))

/* ------------------------------------------------------------------ scoring */

static void AddScore(uint16_t base, int16_t px, int16_t py)
{
    uint32_t gained = (uint32_t)base * memory->mult;
    memory->score += gained;
    if (memory->score > highScore)
        highScore = memory->score;
    memory->popX = px;
    memory->popY = py;
    memory->popVal = (uint16_t)gained;
    memory->popTick = 22;
}

static void ShowMsg(uint8_t kind)
{
    memory->msgKind = kind;
    memory->msgTick = 50;
}

/* ------------------------------------------------------------------ setup */

static void ResetBallToLane(void)
{
    memory->ball.x = LANE_CX;
    memory->ball.y = BY - 12.0f;
    memory->ball.vx = 0;
    memory->ball.vy = 0;
    memory->charge = 0;
    memory->prevPlunger = 0;
    for (uint8_t i = 0; i < 6; i++)
    {
        memory->trail_x[i] = LANE_CX;
        memory->trail_y[i] = BY - 12.0f;
    }
    memory->trailIdx = 0;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game32Memory));
    mainMemory._gameState = GS_RUNNING;
    memory->ltheta = FREST;
    memory->rtheta = FREST;
    memory->ballNum = 1;
    memory->mult = 1;
    memory->state = PB_READY;
    ResetBallToLane();
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

/* ------------------------------------------------------------------ physics */

/* reflect the ball off a line segment; returns 1 on contact */
static uint8_t SegCollide(float ax, float ay, float bx, float by, float rest)
{
    Ball * b = &memory->ball;
    float abx = bx-ax, aby = by-ay;
    float len2 = abx*abx + aby*aby;
    float t = ((b->x-ax)*abx + (b->y-ay)*aby) / len2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float px = ax + t*abx, py = ay + t*aby;
    float dx = b->x-px, dy = b->y-py;
    float d2 = dx*dx + dy*dy;
    if (d2 >= BALL_R*BALL_R)
        return 0;
    float d = sqrtf(d2), nx, ny;
    if (d > 0.0001f) { nx = dx/d; ny = dy/d; }
    else { float il = 1.0f/sqrtf(len2); nx = -aby*il; ny = abx*il; }
    b->x = px + nx*BALL_R;
    b->y = py + ny*BALL_R;
    float vn = b->vx*nx + b->vy*ny;
    if (vn < 0)
    {
        b->vx -= (1+rest)*vn*nx;
        b->vy -= (1+rest)*vn*ny;
    }
    return 1;
}

/* generic round bumper: bounce, kick to a minimum speed, score */
static void RoundBumper(float cx, float cy, float cr, float kick, uint16_t pts, uint8_t *flash)
{
    Ball * b = &memory->ball;
    float dx = b->x-cx, dy = b->y-cy;
    float rr = BALL_R + cr;
    float d2 = dx*dx + dy*dy;
    if (d2 >= rr*rr)
        return;
    float d = sqrtf(d2), nx, ny;
    if (d > 0.0001f) { nx = dx/d; ny = dy/d; }
    else { nx = 0; ny = -1; }
    b->x = cx + nx*rr;
    b->y = cy + ny*rr;
    float vn = b->vx*nx + b->vy*ny;
    if (vn < 0)
    {
        b->vx -= 2*vn*nx;
        b->vy -= 2*vn*ny;
    }
    float sp = sqrtf(b->vx*b->vx + b->vy*b->vy);
    if (sp < kick)
    {
        b->vx += nx*(kick-sp);
        b->vy += ny*(kick-sp);
    }
    b->vx += ((int)Random_Range(3)-1)*0.3f;
    if (flash) *flash = 8;
    AddScore(pts, (int16_t)cx, (int16_t)(cy-cr-4));
}

/* one flipper as a moving segment; angv<0 means it is swinging up */
static void FlipperCollide(float px, float py, float theta, uint8_t mirror, float angv)
{
    Ball * b = &memory->ball;
    float cs = cosf(theta), sn = sinf(theta);
    float tx = mirror ? px - FLEN*cs : px + FLEN*cs;
    float ty = py + FLEN*sn;
    float abx = tx-px, aby = ty-py;
    float len2 = abx*abx + aby*aby;
    float t = ((b->x-px)*abx + (b->y-py)*aby) / len2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float cx = px + t*abx, cy = py + t*aby;
    float dx = b->x-cx, dy = b->y-cy;
    float rr = BALL_R + FWIDTH;
    float d2 = dx*dx + dy*dy;
    if (d2 >= rr*rr)
        return;
    float d = sqrtf(d2), nx, ny;
    if (d > 0.0001f) { nx = dx/d; ny = dy/d; }
    else { nx = 0; ny = -1; }
    b->x = cx + nx*rr;
    b->y = cy + ny*rr;
    /* The blade is a MOVING surface: reflect the ball's velocity relative to
       the surface velocity at the contact point (rotation about the pivot).
       A rising flipper therefore throws the ball, and the impulse self-limits
       once the ball outruns the blade, so it is not double counted across the
       sub-steps. */
    float rx = cx-px, ry = cy-py;
    float svx, svy;
    if (mirror) { svx = angv*ry;  svy = -angv*rx; }
    else        { svx = -angv*ry; svy = angv*rx;  }
    float rvx = b->vx-svx, rvy = b->vy-svy;
    float vn = rvx*nx + rvy*ny;
    if (vn < 0)
    {
        b->vx -= 1.35f*vn*nx;
        b->vy -= 1.35f*vn*ny;
    }
}

static void UpdateFlippers(uint8_t left, uint8_t right)
{
    float lt = left ? FACTIVE : FREST;
    float rt = right ? FACTIVE : FREST;
    float old;

    old = memory->ltheta;
    if (memory->ltheta < lt) { memory->ltheta += OMEGA; if (memory->ltheta > lt) memory->ltheta = lt; }
    else if (memory->ltheta > lt) { memory->ltheta -= OMEGA; if (memory->ltheta < lt) memory->ltheta = lt; }
    memory->langv = memory->ltheta - old;

    old = memory->rtheta;
    if (memory->rtheta < rt) { memory->rtheta += OMEGA; if (memory->rtheta > rt) memory->rtheta = rt; }
    else if (memory->rtheta > rt) { memory->rtheta -= OMEGA; if (memory->rtheta < rt) memory->rtheta = rt; }
    memory->rangv = memory->rtheta - old;
}

static void CheckTargets(void)
{
    Ball * b = &memory->ball;
    for (uint8_t i = 0; i < NTARG; i++)
    {
        if (memory->targCool[i])
            continue;
        float dx = b->x-TGX[i], dy = b->y-TGY[i];
        float rr = BALL_R + TGR;
        if (dx*dx + dy*dy < rr*rr)
        {
            memory->targCool[i] = 20;
            memory->targLit[i] = 1;
            /* bounce the ball back towards the table */
            float d = sqrtf(dx*dx+dy*dy);
            float nx = d>0.0001f ? dx/d : 1.0f, ny = d>0.0001f ? dy/d : 0.0f;
            b->x = TGX[i]+nx*rr;
            b->y = TGY[i]+ny*rr;
            float vn = b->vx*nx+b->vy*ny;
            if (vn < 0) { b->vx -= 1.8f*vn*nx; b->vy -= 1.8f*vn*ny; }
            AddScore(150, (int16_t)(TGX[i]+12), (int16_t)TGY[i]);
            if (memory->targLit[0] && memory->targLit[1] && memory->targLit[2])
            {
                for (uint8_t j = 0; j < NTARG; j++) memory->targLit[j] = 0;
                memory->score += 1000;
                if (memory->score > highScore) highScore = memory->score;
                ShowMsg(4);
            }
        }
    }
}

static void CheckPosts(void)
{
    for (uint8_t i = 0; i < NPOST; i++)
    {
        if (memory->postCool[i])
            continue;
        Ball * b = &memory->ball;
        float dx = b->x-PSTX[i], dy = b->y-PSTY[i];
        float rr = BALL_R + PR;
        if (dx*dx + dy*dy < rr*rr)
        {
            memory->postCool[i] = 15;
            memory->postFlash[i] = 6;
            float d = sqrtf(dx*dx+dy*dy);
            float nx = d>0.0001f ? dx/d : 0.0f, ny = d>0.0001f ? dy/d : -1.0f;
            b->x = PSTX[i]+nx*rr;
            b->y = PSTY[i]+ny*rr;
            float vn = b->vx*nx+b->vy*ny;
            if (vn < 0) { b->vx -= 1.7f*vn*nx; b->vy -= 1.7f*vn*ny; }
            AddScore(25, (int16_t)PSTX[i], (int16_t)(PSTY[i]-6));
        }
    }
}

static void CheckSaucer(void)
{
    if (memory->saucerCool)
        return;
    Ball * b = &memory->ball;
    float dx = b->x-SAUC_X, dy = b->y-SAUC_Y;
    float rr = BALL_R + SAUC_R;
    if (dx*dx + dy*dy < rr*rr)
    {
        memory->saucerCool = 60;
        memory->saucerFlash = 12;
        AddScore(500, (int16_t)SAUC_X, (int16_t)(SAUC_Y-12));
        /* kick the ball back up the table */
        b->x = SAUC_X;
        b->y = SAUC_Y - rr;
        b->vy = -5.5f;
        b->vx = ((int)Random_Range(3)-1)*1.2f;
    }
}

static void CheckRollovers(void)
{
    Ball * b = &memory->ball;
    for (uint8_t i = 0; i < NROLL; i++)
    {
        if (memory->rollCool[i])
            continue;
        float dx = b->x-RX_[i], dy = b->y-RY_[i];
        float rr = BALL_R + 6.0f;
        if (dx*dx + dy*dy < rr*rr)
        {
            memory->rollCool[i] = 30;
            memory->rollLit[i] = 1;
            AddScore(250, (int16_t)RX_[i], (int16_t)(RY_[i]+10));
            if (memory->rollLit[0] && memory->rollLit[1] && memory->rollLit[2])
            {
                for (uint8_t j = 0; j < NROLL; j++)
                    memory->rollLit[j] = 0;
                if (memory->mult < 5)
                    memory->mult++;
                memory->score += 1000;
                if (memory->score > highScore) highScore = memory->score;
                ShowMsg(2);
            }
        }
    }
}

static void ResolveCollisions(void)
{
    for (uint8_t i = 0; i < NBUMP; i++)
        RoundBumper(BX[i], BY_[i], BR[i], 4.5f, 100, &memory->bumpFlash[i]);
    for (uint8_t i = 0; i < NMINI; i++)
        RoundBumper(MX[i], MY[i], MR[i], 3.5f, 75, &memory->miniFlash[i]);
    for (int i = 0; i < NWALL; i++)
        SegCollide(_walls[i].ax, _walls[i].ay, _walls[i].bx, _walls[i].by, _walls[i].rest);
    for (uint8_t i = 0; i < NSLING; i++)
    {
        if (SegCollide(SL[i][0], SL[i][1], SL[i][2], SL[i][3], 1.0f))
        {
            memory->ball.vy -= 2.5f;
            memory->slingFlash[i] = 8;
            AddScore(50, (int16_t)SL[i][0], (int16_t)(SL[i][1]-6));
        }
    }
    FlipperCollide(LPX, FPY, memory->ltheta, 0, memory->langv);
    FlipperCollide(RPX, FPY, memory->rtheta, 1, memory->rangv);
}

static void DecCooldowns(void)
{
    for (uint8_t i = 0; i < NROLL; i++) if (memory->rollCool[i]) memory->rollCool[i]--;
    for (uint8_t i = 0; i < NTARG; i++) if (memory->targCool[i]) memory->targCool[i]--;
    for (uint8_t i = 0; i < NPOST; i++) if (memory->postCool[i]) memory->postCool[i]--;
    if (memory->saucerCool) memory->saucerCool--;
}

static void StartLaunch(void)
{
    memory->launchPower = memory->charge < 0.25f ? 0.25f : memory->charge;
    memory->launchTick = 0;
    memory->ballSave = BALL_SAVE_TICKS;
    memory->state = PB_LAUNCH;
    memory->stateTick = 0;
}

static void EnterPlayfield(void)
{
    float p = memory->launchPower;
    memory->ball.x = 166.0f;
    memory->ball.y = 26.0f;
    memory->ball.vx = -(1.5f + p*2.2f);
    memory->ball.vy = 0.5f + p*1.0f;
    memory->state = PB_PLAY;
    memory->stateTick = 0;
}

static void LoseBall(void)
{
    /* ball save: a drain during the grace period just relaunches the ball */
    if (memory->ballSave > 0)
    {
        ShowMsg(3);
        EnterPlayfield();
        return;
    }
    memory->state = PB_DRAIN;
    memory->stateTick = 0;
    ShowMsg(1);
}

/* clamped angular change towards the rest/active angle for one tick */
static float FlipperDelta(float theta, uint8_t active)
{
    float target = active ? FACTIVE : FREST;
    float d = target - theta;
    if (d > OMEGA) d = OMEGA;
    if (d < -OMEGA) d = -OMEGA;
    return d;
}

static void PhysicsTick(uint8_t left, uint8_t right)
{
    /* Flipper angular change for this tick. It is applied GRADUALLY across the
       sub-steps below: the tip moves ~15 px in a full tick, so checking only
       the final angle let the blade jump straight past the ball and miss it. */
    float ldelta = FlipperDelta(memory->ltheta, left);
    float rdelta = FlipperDelta(memory->rtheta, right);
    memory->langv = ldelta;
    memory->rangv = rdelta;
    DecCooldowns();
    if (memory->ballSave) memory->ballSave--;
    Ball * b = &memory->ball;
    b->vy += GRAV;
    b->vx *= DAMP;
    b->vy *= DAMP;
    float sp = sqrtf(b->vx*b->vx + b->vy*b->vy);
    if (sp > MAXS)
    {
        b->vx *= MAXS/sp;
        b->vy *= MAXS/sp;
        sp = MAXS;
    }
    memory->trail_x[memory->trailIdx] = b->x;
    memory->trail_y[memory->trailIdx] = b->y;
    memory->trailIdx = (uint8_t)((memory->trailIdx+1)%6);
    /* Sub-step so neither the ball nor the flipper tip (the fastest thing on
       the table) moves more than ~1 px per step. */
    float flipSpeed = FLEN * fmaxf(fabsf(ldelta), fabsf(rdelta));
    float maxMotion = fmaxf(sp, flipSpeed);
    int steps = (int)(maxMotion/1.0f) + 1;
    if (steps > 20) steps = 20;
    if (steps < 1) steps = 1;
    float sx = b->vx/steps, sy = b->vy/steps;
    float ls = ldelta/steps, rs = rdelta/steps;
    for (int s = 0; s < steps; s++)
    {
        b->x += sx;
        b->y += sy;
        memory->ltheta += ls;
        memory->rtheta += rs;
        ResolveCollisions();
    }
    /* a flipper throw can exceed the cap; rein it in for the next tick */
    sp = sqrtf(b->vx*b->vx + b->vy*b->vy);
    if (sp > MAXS)
    {
        b->vx *= MAXS/sp;
        b->vy *= MAXS/sp;
    }
    CheckRollovers();
    CheckTargets();
    CheckPosts();
    CheckSaucer();
    if (b->y - BALL_R > BY || b->x < TX-12 || b->x > RX+12)
        LoseBall();
}

/* ------------------------------------------------------------------ input */

#define PANEL_X 210
#define LAUNCH_BTN_X (PANEL_X+4)
#define LAUNCH_BTN_Y 154
#define LAUNCH_BTN_W 100
#define LAUNCH_BTN_H 26

#define RESET_BTN_W 80
#define RESET_BTN_H 28
#define RESET_BTN_X (GPU_X/2 - RESET_BTN_W/2)
#define RESET_BTN_Y (GPU_Y/2 + 30)

static uint8_t LaunchButtonTouched(void)
{
    return (uint8_t)(mainMemory.touchPressed &&
        mainMemory.touch_X >= LAUNCH_BTN_X && mainMemory.touch_X < LAUNCH_BTN_X+LAUNCH_BTN_W &&
        mainMemory.touch_Y >= LAUNCH_BTN_Y && mainMemory.touch_Y < LAUNCH_BTN_Y+LAUNCH_BTN_H);
}

static void Tick(void)
{
    if (memory->popTick) memory->popTick--;
    if (memory->msgTick) memory->msgTick--;
    for (uint8_t i = 0; i < NBUMP; i++) if (memory->bumpFlash[i]) memory->bumpFlash[i]--;
    for (uint8_t i = 0; i < NMINI; i++) if (memory->miniFlash[i]) memory->miniFlash[i]--;
    for (uint8_t i = 0; i < NSLING; i++) if (memory->slingFlash[i]) memory->slingFlash[i]--;
    for (uint8_t i = 0; i < NPOST; i++) if (memory->postFlash[i]) memory->postFlash[i]--;
    if (memory->saucerFlash) memory->saucerFlash--;

    KeyPressedEnum held = Keyboard_GetHeldKeys();
    uint8_t tableMid = (TX+LX)/2;
    uint8_t left = (uint8_t)((held & KEY_LEFT) ||
        (mainMemory.touchPressed && mainMemory.touch_X < tableMid && mainMemory.touch_X < PANEL_X));
    uint8_t right = (uint8_t)((held & KEY_RIGHT) ||
        (mainMemory.touchPressed && mainMemory.touch_X >= tableMid && mainMemory.touch_X < PANEL_X));
    uint8_t plunger = (uint8_t)((held & (KEY_UP|KEY_DOWN)) || LaunchButtonTouched());

    switch (memory->state)
    {
    case PB_READY:
        UpdateFlippers(left, right);
        if (plunger)
        {
            memory->charge += 0.02f;
            if (memory->charge > 1.0f) memory->charge = 1.0f;
        }
        else if (memory->prevPlunger)
        {
            StartLaunch();
        }
        memory->prevPlunger = plunger;
        break;
    case PB_LAUNCH:
        UpdateFlippers(left, right);
        memory->launchTick++;
        memory->ball.x = LANE_CX;
        memory->ball.y = (BY-12.0f) - (float)memory->launchTick * 13.0f;
        if (memory->ball.y <= 24.0f)
            EnterPlayfield();
        break;
    case PB_PLAY:
        PhysicsTick(left, right);
        break;
    case PB_DRAIN:
        UpdateFlippers(left, right);
        memory->stateTick++;
        if (memory->stateTick > 45)
        {
            if (memory->ballNum >= NBALLS)
            {
                memory->state = PB_OVER;
                mainMemory._gameState = GS_DONE;
            }
            else
            {
                memory->ballNum++;
                ResetBallToLane();
                memory->state = PB_READY;
            }
        }
        break;
    case PB_OVER:
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (tap && tx >= RESET_BTN_X && tx <= RESET_BTN_X+RESET_BTN_W &&
            ty >= RESET_BTN_Y && ty <= RESET_BTN_Y+RESET_BTN_H)
            G_Init();
        else if (Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        break;
    }
    default:
        break;
    }
}

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    if (tick > mainMemory._lastRenderedTick + 2*NUM_TICKS)
        mainMemory._lastRenderedTick = tick - 2*NUM_TICKS;
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

/* ------------------------------------------------------------------ drawing */

static void DrawFlipper(float px, float py, float theta, uint8_t mirror, uint8_t active)
{
    float cs = cosf(theta), sn = sinf(theta);
    float tx = mirror ? px - FLEN*cs : px + FLEN*cs;
    float ty = py + FLEN*sn;
    float dx = tx-px, dy = ty-py;
    float il = 1.0f/sqrtf(dx*dx+dy*dy);
    float nx = -dy*il, ny = dx*il;
    float wp = 4.5f, wt = 2.0f;
    int16_t p1x = (int16_t)(px+nx*wp), p1y = (int16_t)(py+ny*wp);
    int16_t p2x = (int16_t)(px-nx*wp), p2y = (int16_t)(py-ny*wp);
    int16_t t1x = (int16_t)(tx+nx*wt), t1y = (int16_t)(ty+ny*wt);
    int16_t t2x = (int16_t)(tx-nx*wt), t2y = (int16_t)(ty-ny*wt);
    GPU_Color c = active ? C_YELLOW : C_ORANGE;
    GPU_DrawFilledTriangle(c, p1x, p1y, p2x, p2y, t1x, t1y);
    GPU_DrawFilledTriangle(c, p2x, p2y, t2x, t2y, t1x, t1y);
    GPU_DrawFilledCircle(GPU_ShadeColor(c, 160), (int16_t)px, (int16_t)py, 4);
}

static void DrawTable(void)
{
    GPU_DrawVerticalGradient(RGB(14,10,40), RGB(6,4,20), TX, TY, RX-TX, BY-TY);
    GPU_DrawEmptySquare(C_C2, TX-2, TY-2, RX-TX+4, BY-TY+4);

    /* launch lane + curved guide */
    GPU_DrawVerticalGradient(RGB(20,16,52), RGB(10,8,28), LX, TY, RX-LX, BY-TY);
    GPU_DrawLine(C_GRAY_DARK, LX, TY, LX, 176);
    GPU_DrawLine(C_GRAY_MEDIUM, LX, 24, RX-4, TY+2);

    /* walls and funnels */
    for (int i = 0; i < NWALL; i++)
    {
        GPU_Color c = (i >= 4) ? C_C1 : C_GRAY_MEDIUM;
        GPU_DrawLine(c, (int16_t)_walls[i].ax, (int16_t)_walls[i].ay,
                     (int16_t)_walls[i].bx, (int16_t)_walls[i].by);
    }

    /* slingshots */
    for (uint8_t i = 0; i < NSLING; i++)
    {
        GPU_Color c = memory->slingFlash[i] ? C_WHITE : C_C3;
        int16_t ax = (int16_t)SL[i][0], ay = (int16_t)SL[i][1];
        int16_t bx = (int16_t)SL[i][2], by = (int16_t)SL[i][3];
        int16_t cx = (int16_t)((SL[i][0]+SL[i][2])/2 + (i==0 ? -8 : 8));
        int16_t cy = (int16_t)((SL[i][1]+SL[i][3])/2);
        GPU_DrawFilledTriangle(c, ax, ay, bx, by, cx, cy);
    }

    /* lane posts */
    for (uint8_t i = 0; i < NPOST; i++)
    {
        int16_t x = (int16_t)PSTX[i], y = (int16_t)PSTY[i];
        GPU_Color c = memory->postFlash[i] ? C_WHITE : C_GRAY_LIGHT;
        GPU_DrawFilledCircle(c, x, y, (int16_t)PR);
        GPU_DrawEmptyCircle(C_GRAY_MEDIUM, x, y, (int16_t)(PR+1));
    }

    /* drop target bank */
    for (uint8_t i = 0; i < NTARG; i++)
    {
        int16_t x = (int16_t)TGX[i], y = (int16_t)TGY[i];
        GPU_Color c = memory->targLit[i] ? C_YELLOW : C_GRAY_MEDIUM;
        if (memory->targLit[i])
            GPU_DrawGlow(c, x, y, 9);
        GPU_DrawFilledSquare(c, x-5, y-5, 10, 10);
        GPU_DrawEmptySquare(C_WHITE, x-5, y-5, 10, 10);
    }

    /* rollovers */
    for (uint8_t i = 0; i < NROLL; i++)
    {
        int16_t x = (int16_t)RX_[i], y = (int16_t)RY_[i];
        GPU_Color c = memory->rollLit[i] ? C_YELLOW : C_GRAY_DARK;
        GPU_DrawGlow(c, x, y, 8);
        GPU_DrawFilledCircle(c, x, y, 5);
        GPU_DrawEmptyCircle(C_GRAY_MEDIUM, x, y, 6);
    }

    /* mini bumpers */
    for (uint8_t i = 0; i < NMINI; i++)
    {
        int16_t x = (int16_t)MX[i], y = (int16_t)MY[i];
        GPU_Color c = memory->miniFlash[i] ? C_WHITE : C_C1;
        GPU_DrawGlow(c, x, y, (int16_t)(MR[i]+4));
        GPU_DrawBall(c, x, y, (int16_t)MR[i]);
    }

    /* big bumpers */
    for (uint8_t i = 0; i < NBUMP; i++)
    {
        int16_t x = (int16_t)BX[i], y = (int16_t)BY_[i];
        GPU_Color c = memory->bumpFlash[i] ? C_WHITE : C_C2;
        GPU_DrawGlow(c, x, y, (int16_t)(BR[i]+5));
        GPU_DrawBall(c, x, y, (int16_t)BR[i]);
        GPU_DrawEmptyCircle(C_YELLOW, x, y, (int16_t)(BR[i]-4));
    }

    /* centre kickout saucer */
    {
        int16_t x = (int16_t)SAUC_X, y = (int16_t)SAUC_Y;
        GPU_Color c = memory->saucerFlash ? C_WHITE : C_ORANGE;
        GPU_DrawGlow(c, x, y, (int16_t)(SAUC_R+5));
        GPU_DrawFilledCircle(C_BLACK, x, y, (int16_t)SAUC_R);
        GPU_DrawEmptyCircle(c, x, y, (int16_t)SAUC_R);
        GPU_DrawEmptyCircle(GPU_ShadeColor(c, 150), x, y, (int16_t)(SAUC_R-3));
    }

    /* flippers */
    DrawFlipper(LPX, FPY, memory->ltheta, 0, (uint8_t)(memory->ltheta < FREST-0.05f));
    DrawFlipper(RPX, FPY, memory->rtheta, 1, (uint8_t)(memory->rtheta < FREST-0.05f));

    /* ball-save light at the drain */
    if (memory->ballSave && memory->state == PB_PLAY)
    {
        GPU_DrawGlow(C_C3, (TX+LX)/2, BY-2, 10);
        GPU_DrawFilledCircle(C_C3, (TX+LX)/2, BY-2, 3);
    }

    /* plunger */
    {
        int16_t ph = (int16_t)(10.0f + memory->charge*16.0f);
        GPU_DrawFilledSquare(C_RED, LANE_CX-5, (int16_t)(BY-ph), 10, ph);
        GPU_DrawFilledSquare(C_GRAY_LIGHT, LANE_CX-7, (int16_t)(BY-3), 14, 3);
    }
}

static void DrawBall(void)
{
    if (memory->state == PB_DRAIN || memory->state == PB_OVER)
        return;
    for (uint8_t i = 0; i < 6; i++)
    {
        uint8_t idx = (uint8_t)((memory->trailIdx+i)%6);
        GPU_DrawFilledCircle(GPU_ShadeColor(C_C1, (int16_t)(40+i*22)),
                             (int16_t)memory->trail_x[idx], (int16_t)memory->trail_y[idx], 1);
    }
    int16_t bx = (int16_t)memory->ball.x, by = (int16_t)memory->ball.y;
    GPU_DrawGlow(C_C1, bx, by, (int16_t)(BALL_R+4));
    GPU_DrawBall(C_WHITE, bx, by, (int16_t)BALL_R);
}

static void DrawPanel(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    int len;

    GPU_DrawVerticalGradient(RGB(16,12,40), C_BLACK, PANEL_X, 0, GPU_X-PANEL_X, GPU_Y);
    GPU_DrawLine(C_GRAY_DARK, PANEL_X-2, 0, PANEL_X-2, GPU_Y);
    GPU_DrawTextCentered(C_C2, fb, PANEL_X+52, 6, "PINBALL");

    GPU_DrawText(C_GRAY_LIGHT, fs, PANEL_X+4, 30, "SCORE", 5);
    len = sprintf(text, "%u", (unsigned)memory->score);
    GPU_DrawText(C_YELLOW, fm, PANEL_X+4, 40, text, (uint16_t)len);
    len = sprintf(text, "HI %u", (unsigned)highScore);
    GPU_DrawText(C_GRAY_MEDIUM, fs, PANEL_X+4, 56, text, (uint16_t)len);

    len = sprintf(text, "BALL %u/%u", memory->ballNum, NBALLS);
    GPU_DrawText(C_WHITE, fs, PANEL_X+4, 72, text, (uint16_t)len);
    len = sprintf(text, "MULT x%u", memory->mult);
    GPU_DrawText(C_C3, fs, PANEL_X+4, 84, text, (uint16_t)len);

    GPU_DrawText(C_GRAY_LIGHT, fs, PANEL_X+4, 98, "LANES", 5);
    for (uint8_t i = 0; i < NROLL; i++)
        GPU_DrawFilledCircle(memory->rollLit[i] ? C_YELLOW : C_GRAY_DARK,
                             (int16_t)(PANEL_X+44+i*16), 102, 4);
    GPU_DrawText(C_GRAY_LIGHT, fs, PANEL_X+4, 112, "TARGETS", 7);
    for (uint8_t i = 0; i < NTARG; i++)
        GPU_DrawFilledSquare(memory->targLit[i] ? C_YELLOW : C_GRAY_DARK,
                             (int16_t)(PANEL_X+52+i*14), 112, 8, 8);

    GPU_DrawText(C_GRAY_LIGHT, fs, PANEL_X+4, 128, "POWER", 5);
    GPU_DrawProgressBar(PANEL_X+4, 138, 100, 10, (uint16_t)(memory->charge*256), C_ORANGE);

    GPU_DrawButton(memory->state == PB_READY ? C_C3 : C_GRAY_DARK, C_BLACK, fm,
                   "LAUNCH", LAUNCH_BTN_X, LAUNCH_BTN_Y, LAUNCH_BTN_W, LAUNCH_BTN_H);

    GPU_DrawText(C_GRAY_DARK, fs, PANEL_X+4, 188, "L/R flippers", 12);
    GPU_DrawText(C_GRAY_DARK, fs, PANEL_X+4, 198, "DOWN charge", 11);
    GPU_DrawText(C_GRAY_DARK, fs, PANEL_X+4, 208, "release fire", 12);
    GPU_DrawText(C_GRAY_DARK, fs, PANEL_X+4, 222, "touch: halves", 13);
}

static void DrawOverlays(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];

    if (memory->popTick)
    {
        sprintf(text, "+%u", (unsigned)memory->popVal);
        int16_t y = (int16_t)(memory->popY - (22-memory->popTick)/2);
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fm, memory->popX, y, text);
    }
    if (memory->msgTick && (memory->state == PB_PLAY || memory->state == PB_DRAIN))
    {
        if (memory->msgKind == 2)
        {
            sprintf(text, "MULTIPLIER x%u", memory->mult);
            GPU_DrawTextOutlined(C_C3, C_BLACK, fb, (TX+LX)/2, 130, text);
        }
        else if (memory->msgKind == 3)
            GPU_DrawTextOutlined(C_C3, C_BLACK, fb, (TX+LX)/2, 130, "BALL SAVED");
        else if (memory->msgKind == 4)
            GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, (TX+LX)/2, 130, "TARGETS +1000");
    }
    if (memory->state == PB_READY)
    {
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fm, (TX+LX)/2, 150, "HOLD DOWN / LAUNCH");
        GPU_DrawTextCentered(C_GRAY_LIGHT, fm, (TX+LX)/2, 166, "release to fire");
    }
    if (memory->state == PB_DRAIN)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, (TX+LX)/2, 120, "BALL LOST");
    if (memory->state == PB_OVER)
    {
        GPU_DrawPanel(RGB(20,10,30), C_RED, (int16_t)(GPU_X/2-92), (int16_t)(GPU_Y/2-44), 184, 92);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-38), "GAME OVER");
        sprintf(text, "SCORE %u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-12), text);
        sprintf(text, "HIGH %u", (unsigned)highScore);
        GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, (int16_t)(GPU_Y/2+4), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BTN_X, RESET_BTN_Y, RESET_BTN_W, RESET_BTN_H);
    }
}

static void G_Draw(void)
{
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(8,6,24), C_BLACK, 0, 0, GPU_X, GPU_Y);
    DrawTable();
    DrawBall();
    DrawPanel();
    DrawOverlays();
}

static void G_Deinit(void)
{
}

const Callbacks G32_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };