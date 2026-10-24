// Q*BERT

#include "../mainLoop.h"
#include "../gpu.h"
#include "../gpu3d.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define QB_ROWS 7
#define QB_NUM_CUBES (QB_ROWS*QB_ROWS)

#define NUM_TICKS 20                  /* logic ticks per second */
#define HOP_TICKS 4                   /* one hop lasts 4 ticks = 200ms */
#define START_LIVES 3

#define RESET_BUTTON_W 80
#define RESET_BUTTON_H 28
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y - 62)

typedef enum {
    QS_READY = 0,
    QS_PLAYING,
    QS_FALLING,
    QS_CAUGHT,
    QS_LEVEL_DONE,
    QS_GAME_OVER
} QbertState;

typedef struct {
    uint8_t _stage[QB_NUM_CUBES];     /* how often a cube was already visited */
    uint8_t _targetStage;             /* visits needed to finish a cube */
    int8_t _playerRow;
    int8_t _playerCol;
    int8_t _fromRow;
    int8_t _fromCol;
    int8_t _hopRow;                   /* where the current hop goes (may be off the pyramid) */
    int8_t _hopCol;
    uint8_t _hopTick;                 /* 0 = standing still, 1..HOP_TICKS = in the air */
    int8_t _enemyRow;
    int8_t _enemyCol;
    uint8_t _enemyActive;
    uint8_t _enemyHopTick;
    uint8_t _enemyHopDelay;
    uint8_t _spawnCountdown;
    uint32_t _score;
    uint8_t _lives;
    uint8_t _level;
    QbertState _state;
    uint8_t _stateTick;
    uint8_t _fallTick;
} Game15Memory;

static Game15Memory * const memory = (Game15Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* ---------------------------------------------------------------- geometry */

#define QB_PIXELS 28                  /* pixels per world unit */
#define QB_SCALE FX(QB_PIXELS)
#define QB_EDGE FX(0.70)              /* cube edge, projects to ~28px */
#define QB_PITCH ANG(30)
#define QB_YAW ANG(45)
#define QB_DIAG FX(1.41421356)
#define QB_ROW_HEIGHT FX(0.67)
#define QB_ROW_DEPTH FX(0.55)
#define QB_CENTER_X 160
#define QB_CENTER_Y 68
/* (QB_ROW_HEIGHT*cos(pitch) + QB_ROW_DEPTH*sin(pitch)) * QB_PIXELS */
#define QB_ROW_STEP 24

/*
 * The pyramid: cube (row,col) has to end up at
 *   screen x = center + (col - row/2) * QB_PIXELS
 *   screen y = center + row * QB_ROW_STEP
 * With a camera that is yawed by 45 degrees the screen x is (x-z)/sqrt(2) and
 * the depth is (x+z)/sqrt(2), so the world position below produces exactly that
 * layout while every cube still shows its top face and two side faces.
 */
static Vec3 CubePosition(int32_t row, int32_t col)
{
    fixed a = FX_MUL(QB_DIAG, FX_INT(col) - FX_INT(row)/2);
    fixed b = -FX_MUL(FX_MUL(QB_DIAG, QB_ROW_DEPTH), FX_INT(row));
    Vec3 p;
    p.x = (a + b)/2;
    p.z = (b - a)/2;
    p.y = -FX_MUL(QB_ROW_HEIGHT, FX_INT(row));
    return p;
}

/* where a cube ends up on screen, used for the 2d decorations */
static int16_t CubeScreenX(int32_t row, int32_t col)
{
    return (int16_t)(QB_CENTER_X + (2*col - row)*(QB_PIXELS/2));
}

static int16_t CubeScreenY(int32_t row)
{
    return (int16_t)(QB_CENTER_Y + row*QB_ROW_STEP);
}

static uint8_t CubeValid(int32_t row, int32_t col)
{
    return (uint8_t)(row >= 0 && row < QB_ROWS && col >= 0 && col <= row);
}

static uint8_t * CubeStage(int32_t row, int32_t col)
{
    return &memory->_stage[row*QB_ROWS + col];
}

/* ------------------------------------------------------------------ colours */

static const GPU_Color _startColors[6] = { C_BLUE, C_PURPLE, RGB(0,90,160), C_C2, RGB(20,60,120), RGB(0,120,90) };
static const GPU_Color _doneColors[6]  = { C_YELLOW, C_ORANGE, C_C3, C_CYAN, C_YELLOW, C_ORANGE };

static GPU_Color StageColor(int32_t row, int32_t col)
{
    uint8_t palette = (uint8_t)((memory->_level-1) % 6);
    uint8_t stage = *CubeStage(row, col);
    if (stage == 0)
        return _startColors[palette];
    if (stage >= memory->_targetStage)
        return _doneColors[palette];
    return GPU_BlendColor(_startColors[palette], _doneColors[palette], 128);
}

static uint8_t AllCubesDone(void)
{
    for (int32_t row = 0; row < QB_ROWS; row++)
        for (int32_t col = 0; col <= row; col++)
            if (*CubeStage(row, col) < memory->_targetStage)
                return 0;
    return 1;
}

/* ------------------------------------------------------------------- logic */

static void StartLevel(uint8_t level)
{
    memory->_level = level;
    memory->_targetStage = (level >= 3) ? 2 : 1;
    memory->_enemyHopDelay = (level < 2) ? 14 : ((level < 4) ? 11 : 9);
    for (uint8_t i = 0; i < QB_NUM_CUBES; i++)
        memory->_stage[i] = 0;
    memory->_playerRow = 0;
    memory->_playerCol = 0;
    memory->_hopTick = 0;
    memory->_enemyActive = 0;
    memory->_spawnCountdown = 60;
    memory->_state = QS_READY;
    memory->_stateTick = 0;
    memory->_fallTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memory->_score = 0;
    memory->_lives = START_LIVES;
    StartLevel(1);
}

static void LandOnCube(int32_t row, int32_t col)
{
    memory->_playerRow = (int8_t)row;
    memory->_playerCol = (int8_t)col;
    uint8_t * stage = CubeStage(row, col);
    if (*stage < memory->_targetStage)
    {
        (*stage)++;
        memory->_score += 25;
    }
}

/* the four way stick is turned by 45 degrees, like on the arcade cabinet */
static uint8_t DirectionFromInput(KeyPressedEnum keys, int8_t * dRow, int8_t * dCol)
{
    if (keys & KEY_UP)     { *dRow = -1; *dCol = -1; return 1; }   /* north west */
    if (keys & KEY_RIGHT)  { *dRow = -1; *dCol =  0; return 1; }   /* north east */
    if (keys & KEY_DOWN)   { *dRow =  1; *dCol =  1; return 1; }   /* south east */
    if (keys & KEY_LEFT)   { *dRow =  1; *dCol =  0; return 1; }   /* south west */
    return 0;
}

/* touching somewhere hops towards that point */
static uint8_t DirectionFromTouch(int8_t * dRow, int8_t * dCol)
{
    int16_t px = CubeScreenX(memory->_playerRow, memory->_playerCol);
    int16_t py = CubeScreenY(memory->_playerRow);
    int32_t dx = (int32_t)mainMemory.touch_X - px;
    int32_t dy = (int32_t)mainMemory.touch_Y - py;
    if (dx*dx + dy*dy < 144)
        return 0;
    if (dy < 0)
    {
        *dRow = -1;
        *dCol = (dx < 0) ? -1 : 0;
    }
    else
    {
        *dRow = 1;
        *dCol = (dx < 0) ? 0 : 1;
    }
    return 1;
}

static void StartHop(int8_t dRow, int8_t dCol)
{
    memory->_fromRow = memory->_playerRow;
    memory->_fromCol = memory->_playerCol;
    memory->_hopRow = (int8_t)(memory->_playerRow + dRow);
    memory->_hopCol = (int8_t)(memory->_playerCol + dCol);
    memory->_hopTick = 1;
}

static void FinishHop(void)
{
    memory->_hopTick = 0;
    if (!CubeValid(memory->_hopRow, memory->_hopCol))
    {
        memory->_state = QS_FALLING;   /* hopped off the pyramid */
        memory->_fallTick = 0;
        return;
    }
    LandOnCube(memory->_hopRow, memory->_hopCol);
    if (memory->_enemyActive &&
        memory->_enemyRow == memory->_playerRow &&
        memory->_enemyCol == memory->_playerCol)
    {
        memory->_state = QS_CAUGHT;
        memory->_stateTick = 0;
        return;
    }
    if (AllCubesDone())
    {
        memory->_score += 100*memory->_level;
        memory->_state = QS_LEVEL_DONE;
        memory->_stateTick = 0;
    }
}

static void EnemyStep(void)
{
    /* Coaly always hops one step closer to the player */
    int8_t dRow, dCol;
    int32_t target = memory->_playerCol;
    if (memory->_playerRow < memory->_enemyRow)
    {
        int32_t left = memory->_enemyCol - 1;
        int32_t right = memory->_enemyCol;
        int32_t dLeft = (left > target) ? left - target : target - left;
        int32_t dRight = (right > target) ? right - target : target - right;
        dRow = -1;
        dCol = (dLeft <= dRight) ? -1 : 0;
    }
    else
    {
        int32_t left = memory->_enemyCol;
        int32_t right = memory->_enemyCol + 1;
        int32_t dLeft = (left > target) ? left - target : target - left;
        int32_t dRight = (right > target) ? right - target : target - right;
        dRow = 1;
        dCol = (dLeft <= dRight) ? 0 : 1;
    }
    int8_t newRow = (int8_t)(memory->_enemyRow + dRow);
    int8_t newCol = (int8_t)(memory->_enemyCol + dCol);
    if (!CubeValid(newRow, newCol))
        return;
    memory->_enemyRow = newRow;
    memory->_enemyCol = newCol;
    if (newRow == memory->_playerRow && newCol == memory->_playerCol &&
        memory->_state == QS_PLAYING)
    {
        memory->_state = QS_CAUGHT;
        memory->_stateTick = 0;
    }
}

static void SpawnEnemy(void)
{
    memory->_enemyRow = 1;
    memory->_enemyCol = (Random_Next() & 1) ? 0 : 1;
    memory->_enemyActive = 1;
    memory->_enemyHopTick = 0;
}

static void LoseLife(void)
{
    memory->_enemyActive = 0;
    memory->_spawnCountdown = 60;
    if (memory->_lives)
        memory->_lives--;
    if (memory->_lives == 0)
    {
        memory->_state = QS_GAME_OVER;
        memory->_stateTick = 0;
        mainMemory._gameState = GS_DONE;
        return;
    }
    memory->_playerRow = 0;
    memory->_playerCol = 0;
    memory->_hopTick = 0;
    memory->_state = QS_READY;
    memory->_stateTick = 0;
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
    switch (memory->_state)
    {
    case QS_READY:
        memory->_stateTick++;
        if (memory->_stateTick > 20)
        {
            memory->_state = QS_PLAYING;
            memory->_stateTick = 0;
        }
        break;
    case QS_PLAYING:
        if (memory->_hopTick)
        {
            memory->_hopTick++;
            if (memory->_hopTick > HOP_TICKS)
                FinishHop();
        }
        else
        {
            int8_t dRow = 0, dCol = 0;
            KeyPressedEnum keys = Keyboard_GetPressedKeys();
            if (DirectionFromInput(keys, &dRow, &dCol))
                StartHop(dRow, dCol);
            else if (mainMemory.touchPressedNew && DirectionFromTouch(&dRow, &dCol))
                StartHop(dRow, dCol);
        }
        if (memory->_enemyActive)
        {
            memory->_enemyHopTick++;
            if (memory->_enemyHopTick >= memory->_enemyHopDelay)
            {
                memory->_enemyHopTick = 0;
                EnemyStep();
            }
        }
        else if (memory->_spawnCountdown)
        {
            memory->_spawnCountdown--;
            if (memory->_spawnCountdown == 0)
                SpawnEnemy();
        }
        break;
    case QS_FALLING:
        memory->_fallTick++;
        if (memory->_fallTick > 12)
            LoseLife();
        break;
    case QS_CAUGHT:
        memory->_stateTick++;
        if (memory->_stateTick > 12)
            LoseLife();
        break;
    case QS_LEVEL_DONE:
        memory->_stateTick++;
        if (memory->_stateTick > 30)
            StartLevel((uint8_t)(memory->_level + 1));
        break;
    case QS_GAME_OVER:
        memory->_stateTick++;
        break;
    default:
        break;
    }
}

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    /* a long pause must not fast forward the whole game at once */
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

static void ProjectToScreen(const Vec3 * world, int16_t * sx, int16_t * sy)
{
    Vec3 camera;
    GPU3D_WorldToCamera(world, &camera);
    if (!GPU3D_Project(&camera, sx, sy))
    {
        *sx = -100;
        *sy = -100;
    }
}

static void DrawQbert(const Vec3 * body, uint8_t squashed)
{
    fixed size = squashed ? FX(0.30) : FX(0.36);
    GPU3D_DrawBox(C_ORANGE, body, size, size, size, 0, 0, 0, 0);
    if (!squashed)
    {
        Vec3 nose = *body;
        nose.y += FX(0.09);
        nose.z -= FX(0.15);
        GPU3D_DrawBox(C_ORANGE, &nose, FX(0.14), FX(0.14), FX(0.14), 0, 0, 0, 0);
        /* eyes */
        int16_t ex, ey;
        Vec3 eyePos = *body;
        eyePos.y += FX(0.10);
        eyePos.z -= FX(0.16);
        ProjectToScreen(&eyePos, &ex, &ey);
        GPU_DrawFilledCircle(C_WHITE, ex-3, ey, 2);
        GPU_DrawFilledCircle(C_WHITE, ex+3, ey, 2);
        GPU_DrawPixel(C_BLACK, (uint16_t)(ex-3), (uint16_t)ey);
        GPU_DrawPixel(C_BLACK, (uint16_t)(ex+3), (uint16_t)ey);
    }
}

static void DrawPlayer(void)
{
    Vec3 p;
    if (memory->_state == QS_FALLING)
    {
        /* keep falling in the direction of the last hop */
        p = CubePosition(memory->_hopRow, memory->_hopCol);
        p.y += QB_EDGE/2 + FX(0.16) - FX_MUL(FX(0.012), FX_INT((int32_t)memory->_fallTick*memory->_fallTick));
        DrawQbert(&p, 0);
        return;
    }
    if (memory->_hopTick)
    {
        fixed t = FX((int32_t)memory->_hopTick - 1)/HOP_TICKS;
        Vec3 from = CubePosition(memory->_fromRow, memory->_fromCol);
        Vec3 to = CubePosition(memory->_hopRow, memory->_hopCol);
        p.x = from.x + FX_MUL(t, to.x - from.x);
        p.y = from.y + FX_MUL(t, to.y - from.y);
        p.z = from.z + FX_MUL(t, to.z - from.z);
        /* parabola 4*t*(1-t) peaks in the middle of the hop */
        fixed arc = FX_MUL(FX(4.0), FX_MUL(t, FX_ONE - t));
        p.y += QB_EDGE/2 + FX(0.16) + FX_MUL(arc, FX(0.28));
        if (memory->_state == QS_CAUGHT)
            p.y -= FX_MUL(FX(0.03), FX_INT(memory->_stateTick));
        DrawQbert(&p, (uint8_t)(memory->_state == QS_CAUGHT));
        return;
    }
    p = CubePosition(memory->_playerRow, memory->_playerCol);
    p.y += QB_EDGE/2 + FX(0.16);
    if (memory->_state == QS_CAUGHT)
    {
        p.y -= FX_MUL(FX(0.02), FX_INT(memory->_stateTick));
        DrawQbert(&p, 1);
        return;
    }
    DrawQbert(&p, 0);
}

static void DrawEnemy(void)
{
    if (!memory->_enemyActive)
        return;
    Vec3 p = CubePosition(memory->_enemyRow, memory->_enemyCol);
    p.y += QB_EDGE/2 + FX(0.16);
    GPU3D_DrawBox(C_PURPLE, &p, FX(0.34), FX(0.34), FX(0.34), 0, 0, 0, 0);
    Vec3 head = p;
    head.y += FX(0.26);
    head.z -= FX(0.04);
    GPU3D_DrawBox(C_PURPLE, &head, FX(0.26), FX(0.26), FX(0.26), 0, 0, 0, 0);
    int16_t ex, ey;
    ProjectToScreen(&head, &ex, &ey);
    GPU_DrawFilledCircle(C_YELLOW, (int16_t)(ex-3), (int16_t)(ey-1), 2);
    GPU_DrawFilledCircle(C_YELLOW, (int16_t)(ex+3), (int16_t)(ey-1), 2);
}

static void DrawHud(void)
{
    const FontDescription * fm = &fontDescription[1];
    GPU_DrawPanel(RGB(16,16,48), C_C1, 0, 0, GPU_X, 30);
    char text[24];
    int len = sprintf(text, "SCORE %5u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fm, 6, 4, text, (uint16_t)len);
    len = sprintf(text, "LEVEL %u", memory->_level);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 4, text, (uint16_t)len);
    GPU_DrawText(C_WHITE, fm, (int16_t)(GPU_X - 62), 4, "LIVES", 5);
    for (uint8_t i = 0; i < memory->_lives && i < 3; i++)
        GPU_DrawFilledCircle(C_ORANGE, (int16_t)(GPU_X - 56 + i*16), 22, 5);
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(8,8,32), C_BLACK, 0, 30, GPU_X, (int16_t)(GPU_Y-30));

    GPU3D_Init();
    GPU3D_SetOrtho(QB_CENTER_X, QB_CENTER_Y, QB_SCALE);
    GPU3D_SetCameraOrientation(QB_PITCH, QB_YAW, 0);
    GPU3D_SetCameraPosition(0, 0, 0);
    GPU3D_SetLightDirection(FX(-0.45), FX(0.80), FX(-0.40));
    GPU3D_SetAmbient(105);

    /* the top row of the pyramid is the furthest away, so it is drawn first */
    for (int32_t row = 0; row < QB_ROWS; row++)
    {
        for (int32_t col = 0; col <= row; col++)
        {
            Vec3 p = CubePosition(row, col);
            GPU3D_DrawBox(StageColor(row, col), &p, QB_EDGE, QB_EDGE, QB_EDGE, 0, 0, 0, 0);
        }
        if (memory->_enemyActive && memory->_enemyRow == row && memory->_state != QS_FALLING)
            DrawEnemy();
        if (row == memory->_playerRow && memory->_state != QS_FALLING)
            DrawPlayer();
    }
    if (memory->_state == QS_FALLING)
        DrawPlayer();

    DrawHud();

    {
        const char * hint = "UP nw   RIGHT ne   DOWN se   LEFT sw   (or touch)";
        GPU_DrawText(C_GRAY_MEDIUM, fm, 4, (int16_t)(GPU_Y - 12), hint, (uint16_t)strlen(hint));
    }

    if (memory->_state == QS_READY)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, 34, "GET READY");
    else if (memory->_state == QS_LEVEL_DONE)
        GPU_DrawTextOutlined(C_GREEN, C_BLACK, fb, GPU_X/2, 34, "LEVEL DONE");
    else if (memory->_state == QS_CAUGHT)
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, 34, "OUCH");
    else if (memory->_state == QS_GAME_OVER)
    {
        GPU_DrawPanel(RGB(16,16,48), C_RED, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-46), 180, 92);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-40), "GAME OVER");
        char text[32];
        sprintf(text, "SCORE %5u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-18), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G15_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
