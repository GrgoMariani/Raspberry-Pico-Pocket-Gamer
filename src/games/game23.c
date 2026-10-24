// TURBO DRIVE - a 3d racing game: overtake the traffic, stay on the road

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 40

#define RD_SEG FX(4.0)              /* length of one road segment */
#define RD_HALF FX(5.0)             /* half width of the road */
#define RD_DRAW 42                  /* segments drawn ahead */
#define RD_LAP 600                  /* segments per lap */
#define RD_CAM_BACK FX(8.0)
#define RD_CAM_HEIGHT FX(3.0)
#define RD_CAR_Z FX(2.0)            /* the player car sits this far ahead of the eye */

#define RD_MAX_SPEED FX(1.5)
#define RD_ACCEL FX(0.012)
#define RD_BRAKE FX(0.03)
#define RD_DRAG FX(0.995)
#define RD_STEER FX(0.045)

#define RD_CARS 8
#define START_LIVES 3

typedef struct {
    fixed z;                 /* absolute position on the track */
    fixed lane;              /* -0.7 .. 0.7 of the road half width */
    fixed speed;
    GPU_Color color;
} Traffic;

typedef struct {
    fixed _distance;
    fixed _speed;
    fixed _playerX;          /* -1 .. 1 of the road half width */
    Traffic _cars[RD_CARS];
    uint32_t _bonus;
    uint32_t _score;
    uint8_t _lap;
    uint8_t _lives;
    uint8_t _offroad;
    uint8_t _state;          /* 0 ready, 1 driving, 2 crashed, 3 over */
    uint8_t _stateTick;
} Game23Memory;

static Game23Memory * const memory = (Game23Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 30)

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* ------------------------------------------------------------------- track */

/* the track is a repeating list of sections: how many segments they last and
   how strong the curve is (positive = right) */
static const int16_t _sectionLen[8]   = { 50, 40, 60, 35, 55, 45, 65, 40 };
static const int16_t _sectionCurve[8] = { 0, 3, 0, -4, 2, 0, -3, 4 };

static int16_t CurveAt(int32_t segment)
{
    int32_t i = ((segment % RD_LAP) + RD_LAP) % RD_LAP;
    int32_t pos = 0;
    for (uint8_t s = 0; s < 8; s++)
    {
        pos += _sectionLen[s];
        if (i < pos)
            return _sectionCurve[s];
    }
    return 0;
}

static fixed HillAt(int32_t segment)
{
    int32_t a = (int32_t)(((int64_t)segment * 40) % ANG_FULL);
    int32_t b = (int32_t)(((int64_t)segment * 17 + 300) % ANG_FULL);
    return FX_MUL(FX(1.6), GPU3D_Sin(a)) + FX_MUL(FX(0.8), GPU3D_Sin(b));
}

/* ------------------------------------------------------------------- logic */

static void SpawnTrafficCar(Traffic * car, fixed z)
{
    car->z = z;
    car->lane = (fixed)Random_Range((uint32_t)FX(1.4)) - FX(0.7);
    car->speed = RD_MAX_SPEED/3 + (fixed)Random_Range((uint32_t)(RD_MAX_SPEED/4));
    static const GPU_Color colors[5] = { C_RED, C_YELLOW, C_CYAN, C_MAGENTA, C_WHITE };
    car->color = colors[Random_Range(5)];
}

static void StartRace(void)
{
    memory->_distance = 0;
    memory->_speed = 0;
    memory->_playerX = 0;
    memory->_offroad = 0;
    for (uint8_t i = 0; i < RD_CARS; i++)
        SpawnTrafficCar(&memory->_cars[i], FX(40) + FX(30)*(int32_t)i);
    memory->_state = 0;
    memory->_stateTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game23Memory));
    memory->_lives = START_LIVES;
    memory->_score = 0;
    memory->_bonus = 0;
    memory->_lap = 1;
    StartRace();
}

static void Tick(void)
{
    switch (memory->_state)
    {
    case 0:
        memory->_stateTick++;
        if (memory->_stateTick > 25)
            memory->_state = 1;
        return;
    case 2:
        memory->_stateTick++;
        if (memory->_stateTick > 25)
        {
            if (memory->_lives == 0)
            {
                memory->_state = 3;
                mainMemory._gameState = GS_DONE;
            }
            else
            {
                memory->_speed = 0;
                memory->_state = 1;
            }
        }
        return;
    case 3:
        memory->_stateTick++;
        return;
    default:
        break;
    }

    KeyPressedEnum held = Keyboard_GetHeldKeys();
    int8_t steer = 0;
    uint8_t gas = 0;
    uint8_t brake = 0;
    if (held & KEY_LEFT)  steer = -1;
    if (held & KEY_RIGHT) steer = 1;
    if (held & KEY_UP)    gas = 1;
    if (held & KEY_DOWN)  brake = 1;
    if (mainMemory.touchPressed)
    {
        if (mainMemory.touch_X < GPU_X/3)
            steer = -1;
        else if (mainMemory.touch_X > 2*GPU_X/3)
            steer = 1;
        else
            gas = 1;
    }

    /* speed */
    if (gas && memory->_speed < RD_MAX_SPEED)
        memory->_speed += RD_ACCEL;
    if (brake)
        memory->_speed -= RD_BRAKE;
    memory->_speed = FX_MUL(memory->_speed, RD_DRAG);
    if (memory->_speed < 0)
        memory->_speed = 0;
    if (memory->_speed > RD_MAX_SPEED)
        memory->_speed = RD_MAX_SPEED;

    /* steering only works while rolling, and the curve pushes outwards */
    int32_t playerSegment = FX_TO_INT(FX_DIV(memory->_distance, RD_SEG));
    if (steer && memory->_speed > FX(0.2))
        memory->_playerX += FX_MUL(FX_INT(steer), RD_STEER);
    fixed push = FX_MUL(FX_MUL(memory->_speed, memory->_speed), FX_INT(CurveAt(playerSegment)))/250;
    memory->_playerX -= push;
    memory->_offroad = 0;
    if (memory->_playerX > FX_ONE)  { memory->_playerX = FX_ONE;  memory->_offroad = 1; }
    if (memory->_playerX < -FX_ONE) { memory->_playerX = -FX_ONE; memory->_offroad = 1; }
    if (memory->_offroad && memory->_speed > RD_MAX_SPEED/3)
        memory->_speed = FX_MUL(memory->_speed, FX(0.97));

    memory->_distance += memory->_speed;
    memory->_score = (uint32_t)(FX_TO_INT(memory->_distance)/4) + memory->_bonus;

    /* lap */
    if (playerSegment + 1 >= (int32_t)memory->_lap*RD_LAP)
    {
        memory->_lap++;
        memory->_bonus += 500;
    }

    /* traffic */
    fixed carZ = memory->_distance + RD_CAR_Z;
    for (uint8_t i = 0; i < RD_CARS; i++)
    {
        Traffic * car = &memory->_cars[i];
        fixed wasBehind = (car->z < carZ) ? 1 : 0;
        car->z += car->speed;
        if (car->z < memory->_distance - FX(12) || car->z > memory->_distance + FX(RD_DRAW*4))
        {
            SpawnTrafficCar(car, memory->_distance + FX(120) + (fixed)Random_Range((uint32_t)FX(60)));
            continue;
        }
        if (wasBehind && car->z >= carZ)
            memory->_bonus += 50;      /* overtake */
        fixed dz = car->z - carZ;
        if (dz > -FX(2.5) && dz < FX(2.5))
        {
            fixed dx = car->lane - memory->_playerX;
            if (dx > -FX(0.35) && dx < FX(0.35))
            {
                if (memory->_lives)
                    memory->_lives--;
                memory->_state = 2;
                memory->_stateTick = 0;
                SpawnTrafficCar(car, memory->_distance + FX(160));
                return;
            }
        }
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

static void DrawCar(const Vec3 * pos, GPU_Color color, uint8_t player)
{
    GPU3D_DrawBox(color, pos, FX(1.7), FX(0.7), FX(3.0), 0, 0, 0, 0);
    Vec3 cabin = *pos;
    cabin.y += FX(0.55);
    cabin.z -= FX(0.3);
    GPU3D_DrawBox(GPU_ShadeColor(color, 180), &cabin, FX(1.3), FX(0.6), FX(1.4), 0, 0, 0, 0);
    if (player)
    {
        Vec3 light = *pos;
        light.y += FX(0.1);
        light.z -= FX(1.55);
        GPU3D_DrawBox(C_RED, &light, FX(1.5), FX(0.2), FX(0.1), 0, 0, 0, 0);
    }
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    int len;

    GPU_ClearFramebuffers();
    /* sky above the horizon, grass below */
    GPU_DrawVerticalGradient(RGB(40,80,180), RGB(150,180,220), 0, 0, GPU_X, 118);
    GPU_DrawVerticalGradient(RGB(20,90,30), RGB(8,40,14), 0, 118, GPU_X, (int16_t)(GPU_Y-118));

    int32_t playerSegment = FX_TO_INT(FX_DIV(memory->_distance, RD_SEG));

    GPU3D_Init();
    GPU3D_SetPerspective(GPU_X/2, 118, FX(200.0), FX(0.6));
    GPU3D_SetCameraOrientation(0, 0, 0);
    GPU3D_SetCameraPosition(FX_MUL(memory->_playerX, FX_MUL(RD_HALF, FX(0.6))),
                            HillAt(playerSegment) + RD_CAM_HEIGHT,
                            memory->_distance - RD_CAM_BACK);
    GPU3D_SetLightDirection(FX(-0.3), FX(0.8), FX(-0.5));
    GPU3D_SetAmbient(130);

    /* lateral offset of every visible segment relative to the player */
    fixed offsets[RD_DRAW+2];
    offsets[0] = 0;
    fixed dx = 0;
    fixed acc = 0;
    for (int32_t k = 1; k <= RD_DRAW+1; k++)
    {
        dx += FX_MUL(FX(0.010), FX_INT(CurveAt(playerSegment + k - 1)));
        acc += dx;
        offsets[k] = acc;
    }

    /* the road, far segments first */
    for (int32_t k = RD_DRAW; k >= 1; k--)
    {
        int32_t seg = playerSegment + k;
        uint8_t stripe = (uint8_t)((seg/3) & 1);
        Vec3 center = GPU3D_Vec(offsets[k], HillAt(seg),
                                FX_MUL(RD_SEG, FX_INT(seg)) + RD_SEG/2);
        Vec3 scale = GPU3D_Vec(RD_HALF*2, FX_ONE, RD_SEG);
        GPU3D_DrawMeshTinted(&GPU3D_QuadMesh, &center, &scale, 0, 0, 0,
                             stripe ? RGB(72,72,78) : RGB(58,58,64), 0);
        Vec3 rumbleScale = GPU3D_Vec(FX(1.0), FX_ONE, RD_SEG);
        GPU_Color rumble = stripe ? C_RED : C_WHITE;
        Vec3 left = center;  left.x -= RD_HALF + FX(0.5);
        Vec3 right = center; right.x += RD_HALF + FX(0.5);
        GPU3D_DrawMeshTinted(&GPU3D_QuadMesh, &left, &rumbleScale, 0, 0, 0, rumble, 0);
        GPU3D_DrawMeshTinted(&GPU3D_QuadMesh, &right, &rumbleScale, 0, 0, 0, rumble, 0);
        if (stripe)
        {
            Vec3 lineScale = GPU3D_Vec(FX(0.35), FX_ONE, RD_SEG);
            GPU3D_DrawMeshTinted(&GPU3D_QuadMesh, &center, &lineScale, 0, 0, 0, C_WHITE, 0);
        }
    }

    /* traffic, far to near so that close cars cover far ones */
    for (int32_t k = RD_DRAW; k >= 1; k--)
    {
        int32_t seg = playerSegment + k;
        for (uint8_t i = 0; i < RD_CARS; i++)
        {
            Traffic * car = &memory->_cars[i];
            int32_t carSeg = FX_TO_INT(FX_DIV(car->z, RD_SEG));
            if (carSeg != seg)
                continue;
            Vec3 pos = GPU3D_Vec(offsets[k] + FX_MUL(car->lane, RD_HALF),
                                 HillAt(seg) + FX(0.45), car->z);
            DrawCar(&pos, car->color, 0);
        }
    }

    /* the player car */
    {
        Vec3 pos = GPU3D_Vec(FX_MUL(memory->_playerX, RD_HALF),
                             HillAt(playerSegment) + FX(0.45),
                             memory->_distance + RD_CAR_Z);
        if (memory->_state == 2 && (memory->_stateTick & 1))
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                Vec3 piece = pos;
                int32_t a = (int32_t)(i*(ANG_FULL/8)) + memory->_stateTick*ANG(13);
                piece.x += FX_MUL(FX_MUL(FX(0.25), FX_INT(memory->_stateTick)), GPU3D_Cos(a));
                piece.y += FX_MUL(FX_MUL(FX(0.2), FX_INT(memory->_stateTick)), GPU3D_Sin(a));
                GPU3D_DrawBox((i & 1) ? C_ORANGE : C_YELLOW, &piece, FX(0.5), FX(0.5), FX(0.5), a, a/2, 0, 0);
            }
        }
        else
        {
            DrawCar(&pos, C_C1, 1);
        }
    }

    /* HUD */
    GPU_DrawPanel(RGB(8,8,24), C_C1, 0, 0, GPU_X, 20);
    len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fm, 4, 3, text, (uint16_t)len);
    len = sprintf(text, "LAP %u", memory->_lap);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 3, text, (uint16_t)len);
    for (uint8_t i = 0; i < memory->_lives && i < 4; i++)
        GPU_DrawFilledCircle(C_C1, (int16_t)(GPU_X - 14 - i*14), 10, 5);
    {
        int16_t w = (int16_t)FX_TO_INT(FX_DIV(FX_MUL(memory->_speed, FX_INT(100)), RD_MAX_SPEED));
        GPU_DrawPanel(C_BLACK, C_GRAY_DARK, 4, (int16_t)(GPU_Y-14), 104, 10);
        if (w > 2)
            GPU_DrawFilledSquare(w > 80 ? C_RED : C_C3, 6, (int16_t)(GPU_Y-12), (int16_t)(w), 6);
        GPU_DrawText(C_GRAY_LIGHT, fm, 112, (int16_t)(GPU_Y-13), "SPEED", 5);
    }
    if (memory->_offroad && memory->_state == 1)
        GPU_DrawTextOutlined(C_ORANGE, C_BLACK, fb, GPU_X/2, 40, "OFF ROAD!");

    if (memory->_state == 0)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 60, "TURBO DRIVE");
    else if (memory->_state == 2)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, 60, "CRASH!");
    else if (memory->_state == 3)
    {
        GPU_DrawPanel(RGB(16,8,8), C_RED, (int16_t)(GPU_X/2-80), (int16_t)(GPU_Y/2-40), 160, 88);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "RACE OVER");
        len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-10), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
    else
    {
        const char * hint = "UP gas DOWN brake L/R steer";
        GPU_DrawText(C_GRAY_DARK, &fontDescription[0], 152, (int16_t)(GPU_Y-12), hint, (uint16_t)strlen(hint));
    }
}

static void G_Deinit(void)
{

}

const Callbacks G23_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
