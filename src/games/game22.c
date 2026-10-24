// SPACE INVADERS

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_TICKS 40

#define INV_COLS 10
#define INV_ROWS 5
#define INV_CELL_W 26
#define INV_CELL_H 18
#define INV_SPRITE_W 11
#define INV_SPRITE_H 8

#define SHIELD_COUNT 4
#define SHIELD_W 22
#define SHIELD_H 14
#define PLAYER_Y (GPU_Y-26)
#define PLAYER_W 16
#define START_LIVES 3

typedef struct {
    uint8_t alive[INV_ROWS][INV_COLS];
    int16_t blockX, blockY;
    int8_t marchDir;
    uint8_t marchDelay;
    uint8_t frame;
    uint8_t aliveCount;
    int16_t playerX;
    int16_t bulletX, bulletY;      /* bulletY < 0 = none */
    int16_t bombX[3], bombY[3];
    int16_t ufoX;
    int8_t  ufoDir;
    uint16_t ufoTimer;
    uint8_t shields[SHIELD_COUNT][SHIELD_W*SHIELD_H];
    uint8_t lives;
    uint8_t wave;
    uint32_t score;
    uint8_t state;                 /* 0 ready, 1 playing, 2 dying, 3 over, 4 wave */
    uint8_t stateTick;
    uint8_t explosionX, explosionY, explosionTick;
} Game22Memory;

static Game22Memory * const memory = (Game22Memory*)(&mainMemory.sharedMemory.memblocks[0]);

/* two frames of the classic crab, 11 pixels wide */
static const uint16_t _crab[2][INV_SPRITE_H] = {
    { 0x090, 0x120, 0x3F0, 0x6D8, 0x7FF, 0x7E7, 0x421, 0x0D8 },
    { 0x090, 0x492, 0x5EE, 0x76E, 0x7FF, 0x3FC, 0x210, 0x421 },
};

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void BuildShields(void)
{
    for (uint8_t s = 0; s < SHIELD_COUNT; s++)
        for (uint8_t y = 0; y < SHIELD_H; y++)
            for (uint8_t x = 0; x < SHIELD_W; x++)
            {
                uint8_t on = 1;
                /* cut the corners and the door */
                if (y < 4 && (x < 4-y || x >= SHIELD_W-4+y))
                    on = 0;
                if (y >= SHIELD_H-5 && x >= 8 && x < 14)
                    on = 0;
                memory->shields[s][y*SHIELD_W + x] = on;
            }
}

static void StartWave(uint8_t wave)
{
    memory->wave = wave;
    memory->aliveCount = INV_ROWS*INV_COLS;
    for (uint8_t r = 0; r < INV_ROWS; r++)
        for (uint8_t c = 0; c < INV_COLS; c++)
            memory->alive[r][c] = 1;
    memory->blockX = 30;
    memory->blockY = (int16_t)(36 + (wave-1)*8);
    if (memory->blockY > 90)
        memory->blockY = 90;
    memory->marchDir = 1;
    memory->marchDelay = 0;
    memory->frame = 0;
    memory->bulletY = -1;
    for (uint8_t i = 0; i < 3; i++)
        memory->bombY[i] = -1;
    memory->ufoX = -1;
    memory->ufoTimer = 400;
    memory->explosionTick = 0;
    BuildShields();
    memory->state = 0;
    memory->stateTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game22Memory));
    memory->lives = START_LIVES;
    memory->score = 0;
    memory->playerX = GPU_X/2;
    StartWave(1);
}

static int16_t InvX(uint8_t c) { return (int16_t)(memory->blockX + c*INV_CELL_W); }
static int16_t InvY(uint8_t r) { return (int16_t)(memory->blockY + r*INV_CELL_H); }

static void ExplodeAt(int16_t x, int16_t y)
{
    memory->explosionX = (uint8_t)x;
    memory->explosionY = (uint8_t)y;
    memory->explosionTick = 6;
}

static void DamageShield(uint8_t s, int16_t lx, int16_t ly, uint8_t radius)
{
    for (int16_t y = ly-radius; y <= ly+radius; y++)
        for (int16_t x = lx-radius; x <= lx+radius; x++)
        {
            if (x < 0 || x >= SHIELD_W || y < 0 || y >= SHIELD_H)
                continue;
            if ((x-lx)*(x-lx) + (y-ly)*(y-ly) <= radius*radius + 1)
                memory->shields[s][y*SHIELD_W + x] = 0;
        }
}

/* returns the shield index hit at screen position, or -1 */
static int8_t ShieldHit(int16_t x, int16_t y)
{
    for (uint8_t s = 0; s < SHIELD_COUNT; s++)
    {
        int16_t sx = (int16_t)(28 + s*76);
        int16_t sy = (int16_t)(PLAYER_Y - 30);
        if (x < sx || x >= sx+SHIELD_W || y < sy || y >= sy+SHIELD_H)
            continue;
        if (memory->shields[s][(y-sy)*SHIELD_W + (x-sx)])
            return (int8_t)s;
    }
    return -1;
}

static void Tick(void)
{
    switch (memory->state)
    {
    case 0:
        memory->stateTick++;
        if (memory->stateTick > 20)
            memory->state = 1;
        break;
    case 4:
        memory->stateTick++;
        if (memory->stateTick > 30)
            StartWave((uint8_t)(memory->wave + 1));
        break;
    case 2:
        memory->stateTick++;
        if (memory->stateTick > 20)
        {
            if (memory->lives)
                memory->lives--;
            if (memory->lives == 0)
            {
                memory->state = 3;
                mainMemory._gameState = GS_DONE;
            }
            else
            {
                memory->playerX = GPU_X/2;
                memory->bulletY = -1;
                for (uint8_t i = 0; i < 3; i++)
                    memory->bombY[i] = -1;
                memory->state = 0;
                memory->stateTick = 0;
            }
        }
        break;
    case 3:
        memory->stateTick++;
        break;
    case 1:
    default:
        break;
    }
    if (memory->state != 1)
        return;

    /* ---- player ---- */
    KeyPressedEnum held = Keyboard_GetHeldKeys();
    KeyPressedEnum pressed = Keyboard_GetPressedKeys();
    int8_t move = 0;
    if (held & KEY_LEFT) move = -1;
    if (held & KEY_RIGHT) move = 1;
    uint8_t fire = (pressed & (KEY_UP | KEY_DOWN)) ? 1 : 0;
    if (mainMemory.touchPressed)
    {
        if (mainMemory.touch_X < GPU_X/3)
            move = -1;
        else if (mainMemory.touch_X > 2*GPU_X/3)
            move = 1;
        else
            fire = 1;
    }
    memory->playerX = (int16_t)(memory->playerX + move*4);
    if (memory->playerX < 12) memory->playerX = 12;
    if (memory->playerX > GPU_X-12) memory->playerX = GPU_X-12;
    if (fire && memory->bulletY < 0)
    {
        memory->bulletX = memory->playerX;
        memory->bulletY = PLAYER_Y - 6;
    }

    /* ---- invaders march ---- */
    uint8_t delay = (uint8_t)(4 + memory->aliveCount/4);
    if (memory->marchDelay)
        memory->marchDelay--;
    else
    {
        memory->marchDelay = delay;
        memory->frame ^= 1;
        int16_t step = 3;
        int16_t minX = 4, maxX = (int16_t)(GPU_X - INV_COLS*INV_CELL_W - 4);
        int16_t nextX = (int16_t)(memory->blockX + memory->marchDir*step);
        if (nextX < minX || nextX > maxX)
        {
            memory->marchDir = (int8_t)-memory->marchDir;
            memory->blockY = (int16_t)(memory->blockY + 6);
            if (memory->blockY + 4*INV_CELL_H > PLAYER_Y - 40)
            {
                /* the invaders landed */
                memory->state = 3;
                memory->lives = 0;
                mainMemory._gameState = GS_DONE;
                return;
            }
        }
        else
            memory->blockX = nextX;
        /* drop a bomb from a random living column */
        uint8_t cols[INV_COLS];
        uint8_t n = 0;
        for (uint8_t c = 0; c < INV_COLS; c++)
            for (uint8_t r = INV_ROWS; r-- > 0;)
                if (memory->alive[r][c])
                {
                    cols[n++] = c;
                    break;
                }
        if (n && Random_Range(3) == 0)
        {
            uint8_t c = cols[Random_Range(n)];
            for (uint8_t r = INV_ROWS; r-- > 0;)
                if (memory->alive[r][c])
                {
                    for (uint8_t b = 0; b < 3; b++)
                        if (memory->bombY[b] < 0)
                        {
                            memory->bombX[b] = (int16_t)(InvX(c) + INV_SPRITE_W/2);
                            memory->bombY[b] = (int16_t)(InvY(r) + INV_SPRITE_H);
                            break;
                        }
                    break;
                }
        }
    }

    /* ---- bullets ---- */
    if (memory->bulletY >= 0)
    {
        memory->bulletY -= 7;
        int8_t sh = ShieldHit(memory->bulletX, memory->bulletY);
        if (sh >= 0)
        {
            DamageShield((uint8_t)sh, (int16_t)(memory->bulletX - (28 + sh*76)),
                         (int16_t)(memory->bulletY - (PLAYER_Y-30)), 2);
            memory->bulletY = -1;
        }
        else
        {
            /* invaders */
            for (uint8_t r = 0; r < INV_ROWS && memory->bulletY >= 0; r++)
                for (uint8_t c = 0; c < INV_COLS && memory->bulletY >= 0; c++)
                {
                    if (!memory->alive[r][c])
                        continue;
                    int16_t ix = InvX(c), iy = InvY(r);
                    if (memory->bulletX >= ix && memory->bulletX <= ix+INV_SPRITE_W &&
                        memory->bulletY >= iy && memory->bulletY <= iy+INV_SPRITE_H)
                    {
                        memory->alive[r][c] = 0;
                        memory->aliveCount--;
                        memory->score += (uint32_t)(INV_ROWS - r)*10;
                        ExplodeAt((int16_t)(ix+INV_SPRITE_W/2), (int16_t)(iy+INV_SPRITE_H/2));
                        memory->bulletY = -1;
                        if (memory->aliveCount == 0)
                        {
                            memory->score += 300;
                            memory->state = 4;
                            memory->stateTick = 0;
                        }
                    }
                }
            /* ufo */
            if (memory->bulletY >= 0 && memory->ufoX >= 0 &&
                memory->bulletX >= memory->ufoX && memory->bulletX <= memory->ufoX+16 &&
                memory->bulletY <= 20 && memory->bulletY >= 12)
            {
                memory->score += 100 + 50*Random_Range(4);
                ExplodeAt((int16_t)(memory->ufoX+8), 16);
                memory->ufoX = -1;
                memory->bulletY = -1;
            }
            if (memory->bulletY < 4)
                memory->bulletY = -1;
        }
    }

    /* ---- bombs ---- */
    for (uint8_t b = 0; b < 3; b++)
    {
        if (memory->bombY[b] < 0)
            continue;
        memory->bombY[b] += 4;
        int8_t sh = ShieldHit(memory->bombX[b], memory->bombY[b]);
        if (sh >= 0)
        {
            DamageShield((uint8_t)sh, (int16_t)(memory->bombX[b] - (28 + sh*76)),
                         (int16_t)(memory->bombY[b] - (PLAYER_Y-30)), 2);
            memory->bombY[b] = -1;
            continue;
        }
        /* cancels the player bullet */
        if (memory->bulletY >= 0 &&
            abs(memory->bombX[b]-memory->bulletX) < 3 && abs(memory->bombY[b]-memory->bulletY) < 6)
        {
            memory->bombY[b] = -1;
            memory->bulletY = -1;
            continue;
        }
        if (abs(memory->bombX[b]-memory->playerX) < PLAYER_W/2 && memory->bombY[b] > PLAYER_Y-4)
        {
            memory->bombY[b] = -1;
            ExplodeAt(memory->playerX, PLAYER_Y);
            memory->state = 2;
            memory->stateTick = 0;
            continue;
        }
        if (memory->bombY[b] > GPU_Y)
            memory->bombY[b] = -1;
    }

    /* ---- ufo ---- */
    if (memory->ufoX < 0)
    {
        if (memory->ufoTimer)
            memory->ufoTimer--;
        else
        {
            memory->ufoDir = Random_Range(2) ? 1 : -1;
            memory->ufoX = memory->ufoDir > 0 ? -20 : GPU_X+4;
        }
    }
    else
    {
        memory->ufoX = (int16_t)(memory->ufoX + memory->ufoDir*2);
        if (memory->ufoX < -24 || memory->ufoX > GPU_X+8)
        {
            memory->ufoX = -1;
            memory->ufoTimer = 500;
        }
    }

    if (memory->explosionTick)
        memory->explosionTick--;
}

static void ResetButtonTouched(void)
{
    if (!mainMemory.touchPressedNew)
        return;
    if (mainMemory.touch_X >= GPU_X/2-40 && mainMemory.touch_X <= GPU_X/2+40 &&
        mainMemory.touch_Y >= GPU_Y/2+20 && mainMemory.touch_Y <= GPU_Y/2+48)
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

static void DrawSprite(const uint16_t * rows, int16_t x, int16_t y, GPU_Color color)
{
    for (uint8_t r = 0; r < INV_SPRITE_H; r++)
        for (uint8_t c = 0; c < INV_SPRITE_W; c++)
            if (rows[r] & (1u << (INV_SPRITE_W-1-c)))
                GPU_DrawPixel(color, (uint16_t)(x+c), (uint16_t)(y+r));
}

static const GPU_Color _rowColor[INV_ROWS] = { C_RED, C_YELLOW, C_C3, C_CYAN, C_C1 };

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    int len;
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(2,2,12), C_BLACK, 0, 0, GPU_X, GPU_Y);
    for (uint8_t i = 0; i < 40; i++)
    {
        uint32_t seed = (uint32_t)i*2654435761u;
        GPU_DrawPixel(C_GRAY_DARK, (uint16_t)((seed >> 6) % GPU_X), (uint16_t)((seed >> 16) % GPU_Y));
    }

    /* invaders */
    for (uint8_t r = 0; r < INV_ROWS; r++)
        for (uint8_t c = 0; c < INV_COLS; c++)
        {
            if (!memory->alive[r][c])
                continue;
            DrawSprite(_crab[memory->frame], InvX(c), InvY(r), _rowColor[r]);
        }

    /* ufo */
    if (memory->ufoX >= 0)
    {
        GPU_DrawFilledCircle(C_RED, (int16_t)(memory->ufoX+8), 16, 4);
        GPU_DrawFilledSquare(C_RED, memory->ufoX, 16, 16, 4);
        GPU_DrawPixel(C_YELLOW, (uint16_t)(memory->ufoX+4), 21);
        GPU_DrawPixel(C_YELLOW, (uint16_t)(memory->ufoX+12), 21);
    }

    /* shields */
    for (uint8_t s = 0; s < SHIELD_COUNT; s++)
    {
        int16_t sx = (int16_t)(28 + s*76);
        int16_t sy = (int16_t)(PLAYER_Y - 30);
        for (uint8_t y = 0; y < SHIELD_H; y++)
            for (uint8_t x = 0; x < SHIELD_W; x++)
                if (memory->shields[s][y*SHIELD_W + x])
                    GPU_DrawPixel(C_GREEN, (uint16_t)(sx+x), (uint16_t)(sy+y));
    }

    /* player */
    if (memory->state != 2 || (memory->stateTick & 1))
    {
        GPU_DrawFilledSquare(C_C3, (int16_t)(memory->playerX-PLAYER_W/2), (int16_t)(PLAYER_Y-2), PLAYER_W, 6);
        GPU_DrawFilledSquare(C_C3, (int16_t)(memory->playerX-3), (int16_t)(PLAYER_Y-6), 6, 4);
        GPU_DrawFilledSquare(C_C3, (int16_t)(memory->playerX-1), (int16_t)(PLAYER_Y-9), 2, 3);
    }

    /* shots */
    if (memory->bulletY >= 0)
        GPU_DrawLine(C_WHITE, memory->bulletX, memory->bulletY, memory->bulletX, (int16_t)(memory->bulletY+4));
    for (uint8_t b = 0; b < 3; b++)
        if (memory->bombY[b] >= 0)
        {
            GPU_DrawLine(C_ORANGE, memory->bombX[b], memory->bombY[b],
                         (int16_t)(memory->bombX[b] + ((memory->bombY[b]/4) & 1 ? 1 : -1)),
                         (int16_t)(memory->bombY[b]+4));
        }
    if (memory->explosionTick)
    {
        int16_t ex = memory->explosionX, ey = memory->explosionY;
        for (int8_t i = -3; i <= 3; i++)
        {
            GPU_DrawPixel(C_YELLOW, (uint16_t)(ex+i), (uint16_t)(ey));
            GPU_DrawPixel(C_YELLOW, (uint16_t)(ex), (uint16_t)(ey+i));
            if ((i & 1) == 0)
            {
                GPU_DrawPixel(C_ORANGE, (uint16_t)(ex+i), (uint16_t)(ey+i/2));
                GPU_DrawPixel(C_ORANGE, (uint16_t)(ex+i), (uint16_t)(ey-i/2));
            }
        }
    }

    /* HUD */
    len = sprintf(text, "SCORE %6u", (unsigned)memory->score);
    GPU_DrawText(C_WHITE, fm, 4, 4, text, (uint16_t)len);
    len = sprintf(text, "WAVE %u", memory->wave);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 4, text, (uint16_t)len);
    for (uint8_t i = 0; i < memory->lives && i < 4; i++)
    {
        int16_t lx = (int16_t)(GPU_X - 20 - i*20);
        GPU_DrawFilledSquare(C_C3, (int16_t)(lx-7), (int16_t)(GPU_Y-8), 14, 4);
        GPU_DrawFilledSquare(C_C3, (int16_t)(lx-1), (int16_t)(GPU_Y-11), 2, 3);
    }
    GPU_DrawLine(C_GREEN, 0, GPU_Y-2, GPU_X-1, GPU_Y-2);

    if (memory->state == 0)
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, GPU_Y/2, "SPACE INVADERS");
    else if (memory->state == 4)
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, GPU_Y/2, "WAVE CLEAR");
    else if (memory->state == 3)
    {
        GPU_DrawPanel(RGB(16,8,8), C_RED, (int16_t)(GPU_X/2-80), (int16_t)(GPU_Y/2-40), 160, 88);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "GAME OVER");
        len = sprintf(text, "SCORE %6u", (unsigned)memory->score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-10), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", (int16_t)(GPU_X/2-40), (int16_t)(GPU_Y/2+20), 80, 28);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G22_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
