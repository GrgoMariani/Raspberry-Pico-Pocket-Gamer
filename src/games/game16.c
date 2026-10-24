// MISSION CONTROL - land the module on the pad

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 25

#define MC_GRAVITY      FX(0.0032)    /* units per tick^2 */
#define MC_MAIN_THRUST  FX(0.0090)
#define MC_SIDE_THRUST  FX(0.0050)
#define MC_WIND         FX(0.0004)    /* pushes the module away from the pad */
#define MC_DRAG         FX(0.995)
#define MC_FUEL_MAX     120
#define MC_START_ALT    FX(26.0)
#define MC_LAND_SPEED   FX(0.09)      /* maximum touchdown speed */
#define MC_LAND_DRIFT   FX(0.06)      /* maximum horizontal speed */
#define MC_LIVES        3

typedef enum {
    MS_COUNTDOWN = 0,
    MS_DESCENT,
    MS_LANDED,
    MS_CRASHED,
    MS_GAME_OVER
} MissionState;

typedef struct {
    fixed x, y, z;              /* position, y is the altitude */
    fixed vx, vy, vz;
    int32_t pitch;              /* attitude of the module, for the looks */
    int32_t roll;
    int16_t fuel;
    uint8_t thrustMain;
    uint8_t thrustSide;
    fixed padX, padZ, padSize;
    uint32_t score;
    uint8_t mission;
    uint8_t lives;
    MissionState state;
    uint8_t stateTick;
    int16_t lastAltitude;
    int16_t lastMessage;
} Game16Memory;

static Game16Memory * const memory = (Game16Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 24)

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* ------------------------------------------------------------------ terrain */

static fixed TerrainHeight(fixed x, fixed z)
{
    fixed dx = x - memory->padX;
    fixed dz = z - memory->padZ;
    fixed d = GPU3D_Sqrt(FX_MUL(dx, dx) + FX_MUL(dz, dz));
    /* the landing pad and its surroundings are flat */
    fixed flat = memory->padSize + FX(2.0);
    if (d < flat)
        return 0;
    int32_t ax = (int32_t)(((int64_t)x * 26) / FX_ONE);
    int32_t az = (int32_t)(((int64_t)z * 19) / FX_ONE);
    fixed h = FX_MUL(FX(1.9), GPU3D_Sin(ax)) + FX_MUL(FX(1.3), GPU3D_Cos(az + 240))
            + FX_MUL(FX(0.7), GPU3D_Sin(ax*2 + 100));
    /* fade the hills in so that they do not start right at the pad */
    fixed fade = d - flat;
    if (fade < FX(3.0))
        h = FX_MUL(h, FX_MUL(fade, FX(1.0/3.0)));
    return h;
}

/* ------------------------------------------------------------------- states */

static void StartMission(uint8_t mission)
{
    memory->mission = mission;
    memory->padSize = FX(4.0) - FX(0.4)*(int32_t)(mission-1);
    if (memory->padSize < FX(2.4))
        memory->padSize = FX(2.4);
    memory->padX = FX((int32_t)(Random_Range(40)) - 20);
    memory->padZ = FX((int32_t)(Random_Range(60)) + 40);
    memory->x = memory->padX + FX(4.0) - FX(0.5)*(int32_t)mission;
    memory->z = memory->padZ - FX(6.0);
    memory->y = MC_START_ALT;
    memory->vx = -FX(0.010);
    memory->vy = 0;
    memory->vz = -FX(0.010);
    memory->pitch = 0;
    memory->roll = 0;
    memory->fuel = (int16_t)(MC_FUEL_MAX - 8*(mission-1));
    if (memory->fuel < 55)
        memory->fuel = 55;
    memory->thrustMain = 0;
    memory->thrustSide = 0;
    memory->state = MS_COUNTDOWN;
    memory->stateTick = 0;
    memory->lastAltitude = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memory->score = 0;
    memory->lives = MC_LIVES;
    StartMission(1);
}

static void Crash(void)
{
    memory->state = MS_CRASHED;
    memory->stateTick = 0;
    if (memory->lives)
        memory->lives--;
}

static void Land(void)
{
    /* the closer to the middle of the pad and the softer, the more points */
    fixed dx = memory->x - memory->padX;
    fixed dz = memory->z - memory->padZ;
    fixed off = GPU3D_Sqrt(FX_MUL(dx, dx) + FX_MUL(dz, dz));
    uint32_t accuracy = (off < memory->padSize/2) ? 300 : 150;
    uint32_t softness = (uint32_t)(200 - (memory->vy < 0 ? -memory->vy : memory->vy)/FX(0.01));
    if (softness > 200) softness = 0;
    memory->score += 100*memory->mission + accuracy + softness + (uint32_t)memory->fuel*2;
    memory->state = MS_LANDED;
    memory->stateTick = 0;
    memory->vy = 0;
    memory->vx = 0;
    memory->vz = 0;
}

static void PhysicsStep(void)
{
    KeyPressedEnum held = Keyboard_GetHeldKeys();
    uint8_t main = (held & KEY_UP) ? 1 : 0;
    int8_t side = 0;
    int8_t fore = 0;
    if (held & KEY_LEFT)  side = -1;
    if (held & KEY_RIGHT) side = 1;
    if (held & KEY_DOWN)  fore = 1;

    /* touch: upper third = main engine, left/right third = lateral */
    if (mainMemory.touchPressed)
    {
        if (mainMemory.touch_Y < GPU_Y/3)
            main = 1;
        else if (mainMemory.touch_X < GPU_X/3)
            side = -1;
        else if (mainMemory.touch_X > 2*GPU_X/3)
            side = 1;
    }

    memory->thrustMain = 0;
    memory->thrustSide = 0;
    if (main && memory->fuel > 0)
    {
        memory->vy += MC_MAIN_THRUST;
        memory->fuel -= 2;
        memory->thrustMain = 1;
    }
    if (side && memory->fuel > 0)
    {
        memory->vx += MC_SIDE_THRUST*side;
        memory->fuel -= 1;
        memory->thrustSide = 1;
    }
    if (fore && memory->fuel > 0)
    {
        memory->vz += MC_SIDE_THRUST;
        memory->fuel -= 1;
        memory->thrustSide = 1;
    }
    if (memory->fuel < 0)
        memory->fuel = 0;

    memory->vy -= MC_GRAVITY;
    memory->vz -= MC_WIND;          /* a steady drift away from the pad */
    memory->vx = FX_MUL(memory->vx, MC_DRAG);
    memory->vz = FX_MUL(memory->vz, MC_DRAG);

    memory->x += memory->vx;
    memory->y += memory->vy;
    memory->z += memory->vz;

    /* attitude follows the thrust, purely for the looks */
    memory->roll += ((side ? side*ANG(6) : 0) - memory->roll)/4;
    memory->pitch += ((main ? -ANG(4) : 0) - memory->pitch)/4;

    fixed ground = TerrainHeight(memory->x, memory->z);
    if (memory->y <= ground)
    {
        memory->y = ground;
        uint8_t onPad = 0;
        fixed dx = memory->x - memory->padX;
        fixed dz = memory->z - memory->padZ;
        if (dx < 0) dx = -dx;
        if (dz < 0) dz = -dz;
        if (dx < memory->padSize/2 && dz < memory->padSize/2)
            onPad = 1;
        fixed speedDown = (memory->vy < 0) ? -memory->vy : memory->vy;
        fixed drift = GPU3D_Sqrt(FX_MUL(memory->vx, memory->vx) + FX_MUL(memory->vz, memory->vz));
        if (onPad && speedDown < MC_LAND_SPEED && drift < MC_LAND_DRIFT)
            Land();
        else
            Crash();
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

static void Tick(void)
{
    switch (memory->state)
    {
    case MS_COUNTDOWN:
        memory->stateTick++;
        if (memory->stateTick > 30)
        {
            memory->state = MS_DESCENT;
            memory->stateTick = 0;
        }
        break;
    case MS_DESCENT:
        PhysicsStep();
        break;
    case MS_LANDED:
        memory->stateTick++;
        if (memory->stateTick > 60)
            StartMission((uint8_t)(memory->mission + 1));
        break;
    case MS_CRASHED:
        memory->stateTick++;
        if (memory->stateTick > 30)
        {
            if (memory->lives == 0)
            {
                memory->state = MS_GAME_OVER;
                memory->stateTick = 0;
                mainMemory._gameState = GS_DONE;
            }
            else
                StartMission(memory->mission);
        }
        break;
    case MS_GAME_OVER:
        memory->stateTick++;
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

#define GRID_STEP FX(5.0)
#define GRID_HALF 6
#define GRID_FAR  60

static void DrawTerrain(void)
{
    /* the grid is aligned to the world so that it scrolls with the module */
    int32_t baseX = FX_TO_INT(memory->x) - (FX_TO_INT(memory->x) % 5);
    int32_t baseZ = FX_TO_INT(memory->z) - (FX_TO_INT(memory->z) % 5);
    for (int32_t i = -GRID_HALF; i <= GRID_HALF; i++)
    {
        fixed wx = FX_INT(baseX + i*5);
        Vec3 a, b;
        a.x = wx;
        a.z = FX_INT(baseZ - 10);
        a.y = TerrainHeight(a.x, a.z);
        for (int32_t s = 0; s < GRID_FAR/5 + 2; s++)
        {
            b.x = wx;
            b.z = a.z + GRID_STEP;
            b.y = TerrainHeight(b.x, b.z);
            int32_t distance = FX_TO_INT(b.z - memory->z);
            GPU_Color color = GPU_ShadeColor(C_GREEN, (int16_t)(230 - distance*2));
            GPU3D_DrawLineWorld(color, &a, &b);
            a = b;
        }
    }
    for (int32_t j = -2; j < GRID_FAR/5; j++)
    {
        fixed wz = FX_INT(baseZ + j*5);
        Vec3 a, b;
        a.z = wz;
        a.x = FX_INT(baseX - GRID_HALF*5);
        a.y = TerrainHeight(a.x, a.z);
        for (int32_t s = 0; s < 2*GRID_HALF; s++)
        {
            b.z = wz;
            b.x = a.x + GRID_STEP;
            b.y = TerrainHeight(b.x, b.z);
            int32_t distance = FX_TO_INT(b.z - memory->z);
            if (distance > -8 && distance < GRID_FAR)
            {
                GPU_Color color = GPU_ShadeColor(C_C1, (int16_t)(230 - distance*2));
                GPU3D_DrawLineWorld(color, &a, &b);
            }
            a = b;
        }
    }
}

static void DrawPad(void)
{
    Vec3 pos = GPU3D_Vec(memory->padX, FX(-0.1), memory->padZ);
    GPU_Color color = (memory->state == MS_LANDED) ? C_GREEN : C_YELLOW;
    GPU3D_DrawBox(color, &pos, memory->padSize, FX(0.25), memory->padSize, 0, 0, 0, 0);
    /* the H in the middle */
    Vec3 bar = GPU3D_Vec(memory->padX, FX(0.06), memory->padZ);
    GPU3D_DrawBox(C_BLACK, &bar, memory->padSize/4, FX(0.1), memory->padSize/2, 0, 0, 0, 0);
    GPU3D_DrawBox(C_BLACK, &bar, memory->padSize/2, FX(0.1), memory->padSize/6, 0, 0, 0, 0);
}

static void DrawLander(void)
{
    Vec3 pos = GPU3D_Vec(memory->x, memory->y + FX(0.7), memory->z);
    if (memory->state == MS_CRASHED)
    {
        /* a few flying pieces */
        for (uint8_t i = 0; i < 6; i++)
        {
            int32_t t = memory->stateTick;
            Vec3 piece = pos;
            piece.x += FX_MUL(FX(0.3)*(int32_t)(i-3), FX_INT(t));
            piece.y += FX_MUL(FX(0.2), FX_INT(t)) - FX_MUL(FX(0.02), FX_INT(t*t));
            piece.z += FX_MUL(FX(0.2)*(int32_t)((i & 1) ? 2 : -2), FX_INT(t));
            if (piece.y > FX(-1.0))
                GPU3D_DrawBox((i & 1) ? C_ORANGE : C_YELLOW, &piece, FX(0.4), FX(0.4), FX(0.4),
                              t*ANG(11), t*ANG(7), 0, 0);
        }
        return;
    }
    /* body */
    GPU3D_DrawBox(C_GRAY_LIGHT, &pos, FX(1.5), FX(1.0), FX(1.5), memory->pitch, 0, memory->roll, 0);
    /* cabin */
    Vec3 cabin = pos;
    cabin.y += FX(0.8);
    GPU3D_DrawBox(C_C1, &cabin, FX(0.9), FX(0.7), FX(0.9), memory->pitch, 0, memory->roll, 0);
    /* legs */
    for (int8_t i = 0; i < 4; i++)
    {
        Vec3 leg = pos;
        leg.x += (i & 1) ? FX(0.9) : FX(-0.9);
        leg.z += (i & 2) ? FX(0.9) : FX(-0.9);
        leg.y -= FX(0.7);
        GPU3D_DrawBox(C_GRAY_MEDIUM, &leg, FX(0.15), FX(0.9), FX(0.15), 0, 0, 0, 0);
        Vec3 foot = leg;
        foot.y -= FX(0.45);
        GPU3D_DrawBox(C_GRAY_MEDIUM, &foot, FX(0.45), FX(0.1), FX(0.45), 0, 0, 0, 0);
    }
    /* engine flame */
    if (memory->thrustMain)
    {
        Vec3 flame = pos;
        flame.y -= FX(0.9) - FX_MUL(FX(0.2), FX_INT((int32_t)(Random_Range(3))));
        GPU3D_DrawBox(C_ORANGE, &flame, FX(0.5), FX(0.9), FX(0.5), 0, 0, 0, 0);
        flame.y -= FX(0.4);
        GPU3D_DrawBox(C_YELLOW, &flame, FX(0.3), FX(0.5), FX(0.3), 0, 0, 0, 0);
    }
    else if (memory->thrustSide)
    {
        Vec3 flame = pos;
        flame.y += FX(0.2);
        GPU3D_DrawBox(C_YELLOW, &flame, FX(0.3), FX(0.3), FX(0.3), 0, 0, 0, 0);
    }
}

static void DrawBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t value, int16_t max, GPU_Color color)
{
    GPU_DrawPanel(C_BLACK, C_GRAY_DARK, x, y, w, h);
    int16_t filled = (int16_t)((int32_t)value*(w-4)/max);
    if (filled > w-4) filled = (int16_t)(w-4);
    if (filled > 0)
        GPU_DrawFilledSquare(color, (int16_t)(x+2), (int16_t)(y+2), filled, (int16_t)(h-4));
}

static void DrawHud(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    char text[32];
    int len;

    GPU_DrawPanel(RGB(8,8,40), C_C1, 0, 0, GPU_X, 26);
    GPU_DrawText(C_YELLOW, fm, 6, 2, "MISSION CONTROL", 15);
    len = sprintf(text, "MISSION %u", memory->mission);
    GPU_DrawText(C_WHITE, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 2, text, (uint16_t)len);
    len = sprintf(text, "SCORE %6u", (unsigned)memory->score);
    GPU_DrawText(C_C3, fm, (int16_t)(GPU_X - len*fm->width - 6), 2, text, (uint16_t)len);

    /* telemetry on the left */
    int16_t altitude = (int16_t)FX_TO_INT(memory->y);
    int16_t vspeed = (int16_t)(memory->vy*100/FX_ONE);
    GPU_DrawPanel(RGB(8,8,40), C_GRAY_DARK, 2, 30, 96, 52);
    len = sprintf(text, "ALT %4d", altitude);
    GPU_DrawText(altitude < 6 ? C_RED : C_GREEN, fs, 6, 33, text, (uint16_t)len);
    len = sprintf(text, "V/S %4d", vspeed);
    GPU_DrawText((-vspeed > 3) ? C_RED : C_GREEN, fs, 6, 44, text, (uint16_t)len);
    GPU_DrawText(C_WHITE, fs, 6, 55, "FUEL", 4);
    DrawBar(40, 55, 54, 9, memory->fuel, MC_FUEL_MAX, (memory->fuel < 25) ? C_RED : C_CYAN);
    for (uint8_t i = 0; i < memory->lives && i < MC_LIVES; i++)
        GPU_DrawFilledCircle(C_GRAY_LIGHT, (int16_t)(10 + i*14), 92, 4);

    /* status on the right */
    const char * status = "NOMINAL";
    GPU_Color statusColor = C_GREEN;
    if (memory->state == MS_COUNTDOWN) { status = "HOLD - T MINUS"; statusColor = C_YELLOW; }
    else if (memory->state == MS_LANDED) { status = "TOUCHDOWN!"; statusColor = C_GREEN; }
    else if (memory->state == MS_CRASHED) { status = "MODULE LOST"; statusColor = C_RED; }
    else if (memory->state == MS_GAME_OVER) { status = "MISSION OVER"; statusColor = C_RED; }
    else if (memory->fuel <= 0) { status = "FUEL OUT"; statusColor = C_RED; }
    else if (-vspeed > 3) { status = "DESCENT FAST"; statusColor = C_ORANGE; }
    else if (altitude < 8 && -vspeed > 2) { status = "PULL UP"; statusColor = C_RED; }
    len = (int)strlen(status);
    GPU_DrawPanel(RGB(8,8,40), statusColor, (int16_t)(GPU_X-118), 30, 116, 20);
    GPU_DrawText(statusColor, fs, (int16_t)(GPU_X-114), 35, status, (uint16_t)len);

    /* distance to the pad */
    fixed dx = memory->x - memory->padX;
    fixed dz = memory->z - memory->padZ;
    int16_t range = (int16_t)FX_TO_INT(GPU3D_Sqrt(FX_MUL(dx,dx) + FX_MUL(dz,dz)));
    len = sprintf(text, "PAD RANGE %4d", range);
    GPU_DrawText(C_C1, fs, (int16_t)(GPU_X-114), 56, text, (uint16_t)len);

    const char * legend = "UP engine  LEFT/RIGHT strafe  DOWN forward";
    GPU_DrawText(C_GRAY_DARK, fs, 4, (int16_t)(GPU_Y-10), legend, (uint16_t)strlen(legend));
}

static void G_Draw(void)
{
    const FontDescription * fb = &fontDescription[2];
    const FontDescription * fh = &fontDescription[3];
    GPU_ClearFramebuffers();
    /* sky and ground */
    GPU_DrawVerticalGradient(RGB(0,0,24), RGB(0,0,0), 0, 0, GPU_X, GPU_Y/2);
    GPU_DrawVerticalGradient(RGB(10,10,14), RGB(0,0,0), 0, GPU_Y/2, GPU_X, GPU_Y/2);

    /* the camera sits behind and above the module and looks down at it */
    GPU3D_Init();
    GPU3D_SetPerspective(GPU_X/2, (int16_t)(GPU_Y/2 + 20), FX(210.0), FX(0.6));
    GPU3D_SetCameraOrientation(ANG(37), 0, 0);
    GPU3D_SetCameraPosition(memory->x, memory->y + FX(9.0), memory->z - FX(12.0));
    GPU3D_SetLightDirection(FX(0.3), FX(0.75), FX(-0.5));
    GPU3D_SetAmbient(90);

    DrawTerrain();
    DrawPad();
    DrawLander();
    DrawHud();

    if (memory->state == MS_COUNTDOWN)
    {
        char text[16];
        int n = 4 - (int)(memory->stateTick*4/30);
        if (n < 1) n = 1;
        sprintf(text, "T-%d", n);
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fh, GPU_X/2, 96, text);
    }
    else if (memory->state == MS_LANDED)
    {
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 96, "TRANQUILLITY");
        if (memory->stateTick > 20)
            GPU_DrawTextOutlined(C_WHITE, C_BLACK, fb, GPU_X/2, 118, "NEXT MISSION");
    }
    else if (memory->state == MS_CRASHED)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, 96, "CRASH");
    else if (memory->state == MS_GAME_OVER)
    {
        GPU_DrawPanel(RGB(8,8,40), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-40), 180, 86);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "MISSION OVER");
        char text[32];
        sprintf(text, "SCORE %6u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fb, GPU_X/2, (int16_t)(GPU_Y/2-8), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G16_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
