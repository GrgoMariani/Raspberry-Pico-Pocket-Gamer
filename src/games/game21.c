// PAC MAN - maze, dots, power pellets and four ghosts

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define NUM_TICKS 25

#define MAZE_W 19
#define MAZE_H 15
#define CELL 14
#define MAZE_OX ((GPU_X - MAZE_W*CELL)/2)
#define MAZE_OY 20

#define PAC_SPEED 0.10f
#define GHOST_SPEED 0.082f
#define FRIGHT_SPEED 0.055f
#define FRIGHT_TICKS 160

#define NUM_GHOSTS 4
#define START_LIVES 3

typedef enum {
    DIR_RIGHT = 0,
    DIR_DOWN = 1,
    DIR_LEFT = 2,
    DIR_UP = 3
} Dir;

static const int8_t _dx[4] = { 1, 0, -1, 0 };
static const int8_t _dy[4] = { 0, 1, 0, -1 };

typedef struct {
    float x, y;          /* in cell coordinates */
    Dir dir;
    Dir want;
    uint8_t frightened;
    uint8_t eaten;       /* on the way back to the start */
    uint8_t released;
    uint16_t releaseIn;
} Ghost;

typedef struct {
    uint8_t _dots[(MAZE_W*MAZE_H+7)/8];
    float _pacX, _pacY;
    Dir _pacDir, _pacWant;
    Ghost _ghosts[NUM_GHOSTS];
    uint16_t _frightLeft;
    uint8_t _lives;
    uint8_t _level;
    uint32_t _score;
    uint16_t _dotsLeft;
    uint8_t _state;      /* 0 ready, 1 playing, 2 dying, 3 over */
    uint8_t _stateTick;
    uint8_t _mouth;
} Game21Memory;

static Game21Memory * const memory = (Game21Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static const char * _maze[MAZE_H] = {
    "###################",
    "#........#........#",
    "#o##.###.#.###.##o#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.###.#.###.####",
    "#..#.....G.....#..#",
    "####.###.#.###.####",
    "#....#...#...#....#",
    "#.##.#.#####.#.##.#",
    "#.................#",
    "#o##.###.#.###.##o#",
    "#........#........#",
    "###################",
};

#define PAC_START_X 9
#define PAC_START_Y 11

static char MazeCell(int32_t x, int32_t y)
{
    if (x < 0 || x >= MAZE_W || y < 0 || y >= MAZE_H)
        return '#';
    return _maze[y][x];
}

static uint8_t IsWall(int32_t x, int32_t y)
{
    return (uint8_t)(MazeCell(x, y) == '#');
}

static uint8_t DotAt(int32_t x, int32_t y)
{
    uint32_t i = (uint32_t)(y*MAZE_W + x);
    return (uint8_t)((memory->_dots[i/8] >> (i%8)) & 1);
}

static void ClearDot(int32_t x, int32_t y)
{
    uint32_t i = (uint32_t)(y*MAZE_W + x);
    memory->_dots[i/8] &= (uint8_t)~(1u << (i%8));
}

static void SetDot(int32_t x, int32_t y)
{
    uint32_t i = (uint32_t)(y*MAZE_W + x);
    memory->_dots[i/8] |= (uint8_t)(1u << (i%8));
}

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void PlaceActors(void)
{
    memory->_pacX = (float)PAC_START_X;
    memory->_pacY = (float)PAC_START_Y;
    memory->_pacDir = DIR_LEFT;
    memory->_pacWant = DIR_LEFT;
    static const uint8_t gx[NUM_GHOSTS] = { 9, 8, 10, 9 };
    static const uint8_t gy[NUM_GHOSTS] = { 7, 7, 7, 6 };
    for (uint8_t i = 0; i < NUM_GHOSTS; i++)
    {
        memory->_ghosts[i].x = (float)gx[i];
        memory->_ghosts[i].y = (float)gy[i];
        memory->_ghosts[i].dir = (i & 1) ? DIR_LEFT : DIR_RIGHT;
        memory->_ghosts[i].want = memory->_ghosts[i].dir;
        memory->_ghosts[i].frightened = 0;
        memory->_ghosts[i].eaten = 0;
        memory->_ghosts[i].released = 1;
        /* the ghosts leave their start cell one after another */
        memory->_ghosts[i].releaseIn = (uint16_t)(i*30);
    }
    memory->_frightLeft = 0;
}

static void BuildDots(void)
{
    memset(memory->_dots, 0, sizeof(memory->_dots));
    memory->_dotsLeft = 0;
    for (int32_t y = 0; y < MAZE_H; y++)
        for (int32_t x = 0; x < MAZE_W; x++)
        {
            char c = MazeCell(x, y);
            if (c == '.' || c == 'o')
            {
                SetDot(x, y);
                memory->_dotsLeft++;
            }
        }
}

static void StartLevel(uint8_t level)
{
    memory->_level = level;
    BuildDots();
    PlaceActors();
    memory->_state = 0;
    memory->_stateTick = 0;
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game21Memory));
    memory->_lives = START_LIVES;
    memory->_score = 0;
    StartLevel(1);
}

/* can the actor standing in cell (cx,cy) leave towards dir? */
static uint8_t CanGo(int32_t cx, int32_t cy, Dir d)
{
    return (uint8_t)!IsWall(cx + _dx[d], cy + _dy[d]);
}

static float SpeedOf(uint8_t ghostIndex)
{
    (void)ghostIndex;
    float base = GHOST_SPEED + 0.006f*(float)(memory->_level-1);
    if (base > PAC_SPEED)
        base = PAC_SPEED;
    return base;
}

/* move one actor that travels along the cell grid */
static void StepActor(float * px, float * py, Dir * dir, Dir want, float speed, uint8_t isGhost)
{
    int32_t cx = (int32_t)(*px + 0.5f);
    int32_t cy = (int32_t)(*py + 0.5f);
    float fx = *px - (float)cx;
    float fy = *py - (float)cy;
    /* at a cell centre: decide the new direction */
    if (fx*fx + fy*fy < speed*speed*0.6f)
    {
        if (CanGo(cx, cy, want))
        {
            *dir = want;
            *px = (float)cx;
            *py = (float)cy;
        }
        else if (!CanGo(cx, cy, *dir))
        {
            *dir = (Dir)((*dir + 2) % 4);
            *px = (float)cx;
            *py = (float)cy;
        }
    }
    *px += (float)_dx[*dir]*speed;
    *py += (float)_dy[*dir]*speed;
    (void)isGhost;
}

static void GhostChoose(Ghost * g, uint8_t index)
{
    int32_t cx = (int32_t)(g->x + 0.5f);
    int32_t cy = (int32_t)(g->y + 0.5f);
    float fx = g->x - (float)cx;
    float fy = g->y - (float)cy;
    if (fx*fx + fy*fy > 0.01f)
        return;                      /* only decide at cell centres */
    int32_t tx, ty;
    if (g->eaten)
    {
        tx = 9; ty = 7;
        if (cx == 9 && cy == 7)
        {
            g->eaten = 0;
            g->frightened = 0;
            g->released = 1;
        }
    }
    else if (g->frightened)
    {
        /* wander */
        Dir options[4];
        uint8_t n = 0;
        for (uint8_t d = 0; d < 4; d++)
            if (CanGo(cx, cy, (Dir)d) && (Dir)((d+2)%4) != g->dir)
                options[n++] = (Dir)d;
        if (n == 0)
        {
            g->want = (Dir)((g->dir + 2) % 4);
            return;
        }
        g->want = options[Random_Range(n)];
        return;
    }
    else
    {
        int32_t px = (int32_t)(memory->_pacX + 0.5f);
        int32_t py = (int32_t)(memory->_pacY + 0.5f);
        switch (index)
        {
        case 0:  tx = px; ty = py; break;                       /* chaser   */
        case 1:  tx = px + _dx[memory->_pacDir]*4;              /* ambusher */
                 ty = py + _dy[memory->_pacDir]*4; break;
        case 2:  tx = px + (px > MAZE_W/2 ? -4 : 4); ty = py; break;
        default: tx = (px/2) + 3; ty = (py/2) + 6; break;        /* lazy one */
        }
    }
    Dir best = g->dir;
    int32_t bestDist = 0x7fffffff;
    uint8_t found = 0;
    for (uint8_t d = 0; d < 4; d++)
    {
        if (!CanGo(cx, cy, (Dir)d))
            continue;
        if ((Dir)((d+2)%4) == g->dir && CanGo(cx, cy, g->dir))
            continue;      /* no reversing unless it is the only way */
        int32_t nx = cx + _dx[d];
        int32_t ny = cy + _dy[d];
        int32_t dist = (nx-tx)*(nx-tx) + (ny-ty)*(ny-ty);
        if (!found || dist < bestDist)
        {
            found = 1;
            bestDist = dist;
            best = (Dir)d;
        }
    }
    if (!found)
        best = (Dir)((g->dir + 2) % 4);
    g->want = best;
}

static void Tick(void)
{
    switch (memory->_state)
    {
    case 0:
        memory->_stateTick++;
        if (memory->_stateTick > 20)
        {
            memory->_state = 1;
            memory->_stateTick = 0;
        }
        break;
    case 1:
    {
        KeyPressedEnum keys = Keyboard_GetPressedKeys();
        if (keys & KEY_RIGHT) memory->_pacWant = DIR_RIGHT;
        if (keys & KEY_LEFT)  memory->_pacWant = DIR_LEFT;
        if (keys & KEY_UP)    memory->_pacWant = DIR_UP;
        if (keys & KEY_DOWN)  memory->_pacWant = DIR_DOWN;
        if (mainMemory.touchPressedNew)
        {
            int16_t px = (int16_t)(MAZE_OX + memory->_pacX*CELL + CELL/2);
            int16_t py = (int16_t)(MAZE_OY + memory->_pacY*CELL + CELL/2);
            int32_t dx = (int32_t)mainMemory.touch_X - px;
            int32_t dy = (int32_t)mainMemory.touch_Y - py;
            if (dx*dx + dy*dy > 36)
            {
                if ((dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx))
                    memory->_pacWant = (dy < 0) ? DIR_UP : DIR_DOWN;
                else
                    memory->_pacWant = (dx < 0) ? DIR_LEFT : DIR_RIGHT;
            }
        }

        StepActor(&memory->_pacX, &memory->_pacY, &memory->_pacDir, memory->_pacWant,
                  PAC_SPEED, 0);
        memory->_mouth = (uint8_t)((memory->_mouth + 1) & 7);

        /* eat */
        int32_t pcx = (int32_t)(memory->_pacX + 0.5f);
        int32_t pcy = (int32_t)(memory->_pacY + 0.5f);
        if (DotAt(pcx, pcy))
        {
            ClearDot(pcx, pcy);
            memory->_dotsLeft--;
            if (MazeCell(pcx, pcy) == 'o')
            {
                memory->_score += 50;
                memory->_frightLeft = FRIGHT_TICKS;
                for (uint8_t i = 0; i < NUM_GHOSTS; i++)
                    if (!memory->_ghosts[i].eaten)
                        memory->_ghosts[i].frightened = 1;
            }
            else
                memory->_score += 10;
            if (memory->_dotsLeft == 0)
            {
                memory->_score += 500;
                StartLevel((uint8_t)(memory->_level + 1));
                return;
            }
        }
        if (memory->_frightLeft)
            memory->_frightLeft--;
        if (memory->_frightLeft == 0)
            for (uint8_t i = 0; i < NUM_GHOSTS; i++)
                memory->_ghosts[i].frightened = 0;

        /* ghosts */
        for (uint8_t i = 0; i < NUM_GHOSTS; i++)
        {
            Ghost * g = &memory->_ghosts[i];
            if (g->releaseIn)
            {
                g->releaseIn--;
                continue;
            }
            float speed;
            if (g->eaten)
                speed = PAC_SPEED*1.4f;
            else if (g->frightened)
                speed = FRIGHT_SPEED;
            else
                speed = SpeedOf(i);
            GhostChoose(g, i);
            StepActor(&g->x, &g->y, &g->dir, g->want, speed, 1);
            /* collision */
            float dx = g->x - memory->_pacX;
            float dy = g->y - memory->_pacY;
            if (dx*dx + dy*dy < 0.36f)
            {
                if (g->frightened && !g->eaten)
                {
                    g->eaten = 1;
                    g->frightened = 0;
                    memory->_score += 200;
                }
                else if (!g->eaten)
                {
                    memory->_state = 2;
                    memory->_stateTick = 0;
                    return;
                }
            }
        }
        break;
    }
    case 2:
        memory->_stateTick++;
        if (memory->_stateTick > 25)
        {
            if (memory->_lives)
                memory->_lives--;
            if (memory->_lives == 0)
            {
                memory->_state = 3;
                mainMemory._gameState = GS_DONE;
            }
            else
            {
                PlaceActors();
                memory->_state = 0;
                memory->_stateTick = 0;
            }
        }
        break;
    case 3:
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

static const GPU_Color _ghostColor[NUM_GHOSTS] = { C_RED, C_CYAN, C_ORANGE, C_MAGENTA };

static void DrawMaze(void)
{
    for (int32_t y = 0; y < MAZE_H; y++)
        for (int32_t x = 0; x < MAZE_W; x++)
        {
            if (!IsWall(x, y))
                continue;
            int16_t px = (int16_t)(MAZE_OX + x*CELL);
            int16_t py = (int16_t)(MAZE_OY + y*CELL);
            /* only draw the sides that face a corridor, that is what makes it
               look like walls instead of blocks */
            GPU_Color wall = RGB(30,50,180);
            if (IsWall(x, y-1) && IsWall(x, y+1) && IsWall(x-1, y) && IsWall(x+1, y))
                continue;      /* inner wall, invisible */
            GPU_DrawFilledSquare(RGB(8,10,40), px, py, CELL, CELL);
            if (!IsWall(x, y-1)) GPU_DrawLine(wall, px, (int16_t)(py+2), (int16_t)(px+CELL-1), (int16_t)(py+2));
            if (!IsWall(x, y+1)) GPU_DrawLine(wall, px, (int16_t)(py+CELL-3), (int16_t)(px+CELL-1), (int16_t)(py+CELL-3));
            if (!IsWall(x-1, y)) GPU_DrawLine(wall, (int16_t)(px+2), py, (int16_t)(px+2), (int16_t)(py+CELL-1));
            if (!IsWall(x+1, y)) GPU_DrawLine(wall, (int16_t)(px+CELL-3), py, (int16_t)(px+CELL-3), (int16_t)(py+CELL-1));
        }
}

static void DrawDots(void)
{
    uint32_t t = IF_GetCurrentTime()/(ONE_SECOND/4);
    for (int32_t y = 0; y < MAZE_H; y++)
        for (int32_t x = 0; x < MAZE_W; x++)
        {
            if (!DotAt(x, y))
                continue;
            int16_t px = (int16_t)(MAZE_OX + x*CELL + CELL/2);
            int16_t py = (int16_t)(MAZE_OY + y*CELL + CELL/2);
            if (MazeCell(x, y) == 'o')
            {
                if (t & 1)
                    GPU_DrawFilledCircle(C_YELLOW, px, py, 5);
                else
                    GPU_DrawFilledCircle(RGB(255,200,80), px, py, 4);
            }
            else
            {
                GPU_DrawFilledSquare(RGB(255,220,170), (int16_t)(px-1), (int16_t)(py-1), 3, 3);
            }
        }
}

static void DrawPac(void)
{
    int16_t px = (int16_t)(MAZE_OX + memory->_pacX*CELL + CELL/2);
    int16_t py = (int16_t)(MAZE_OY + memory->_pacY*CELL + CELL/2);
    if (memory->_state == 2)
    {
        /* shrinking pac */
        int16_t r = (int16_t)(7 - memory->_stateTick/4);
        if (r > 0)
            GPU_DrawFilledCircle(C_YELLOW, px, py, r);
        return;
    }
    GPU_DrawFilledCircle(C_YELLOW, px, py, 7);
    /* the mouth */
    int16_t open = (int16_t)(2 + (memory->_mouth < 4 ? memory->_mouth : 7-memory->_mouth));
    for (int16_t i = 0; i < open; i++)
    {
        int16_t mx = px, my = py;
        switch (memory->_pacDir)
        {
        case DIR_RIGHT: mx = (int16_t)(px + 7 - i/2); my = (int16_t)(py + i - open/2); break;
        case DIR_LEFT:  mx = (int16_t)(px - 7 + i/2); my = (int16_t)(py + i - open/2); break;
        case DIR_UP:    my = (int16_t)(py - 7 + i/2); mx = (int16_t)(px + i - open/2); break;
        default:        my = (int16_t)(py + 7 - i/2); mx = (int16_t)(px + i - open/2); break;
        }
        GPU_DrawLine(C_BLACK, px, py, mx, my);
    }
}

static void DrawGhost(const Ghost * g, uint8_t index)
{
    int16_t px = (int16_t)(MAZE_OX + g->x*CELL + CELL/2);
    int16_t py = (int16_t)(MAZE_OY + g->y*CELL + CELL/2);
    GPU_Color body = _ghostColor[index];
    if (g->eaten)
        body = C_BLACK;             /* only the eyes travel home */
    else if (g->frightened)
        body = (memory->_frightLeft < 40 && (IF_GetCurrentTime()/(ONE_SECOND/8)) & 1) ? C_WHITE : C_BLUE;
    if (!g->eaten)
    {
        GPU_DrawFilledCircle(body, px, (int16_t)(py-1), 7);
        GPU_DrawFilledSquare(body, (int16_t)(px-7), (int16_t)(py-1), 14, 7);
        /* wavy skirt */
        for (int16_t i = 0; i < 4; i++)
            GPU_DrawPixel(C_BLACK, (uint16_t)(px-6+i*4), (uint16_t)(py+6));
    }
    /* eyes look into the direction of travel */
    int16_t ex = (int16_t)(px + _dx[g->dir]*2);
    int16_t ey = (int16_t)(py - 2 + _dy[g->dir]*2);
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(ex-3), ey, 2);
    GPU_DrawFilledCircle(C_WHITE, (int16_t)(ex+3), ey, 2);
    GPU_DrawPixel(C_BLUE, (uint16_t)(ex-3+_dx[g->dir]), (uint16_t)(ey+_dy[g->dir]));
    GPU_DrawPixel(C_BLUE, (uint16_t)(ex+3+_dx[g->dir]), (uint16_t)(ey+_dy[g->dir]));
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    int len;
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(4,4,16), C_BLACK, 0, 0, GPU_X, GPU_Y);

    DrawMaze();
    DrawDots();
    for (uint8_t i = 0; i < NUM_GHOSTS; i++)
        DrawGhost(&memory->_ghosts[i], i);
    DrawPac();

    /* HUD */
    len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
    GPU_DrawText(C_WHITE, fm, 4, 4, text, (uint16_t)len);
    len = sprintf(text, "LEVEL %u", memory->_level);
    GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X/2 - len*fm->width/2), 4, text, (uint16_t)len);
    for (uint8_t i = 0; i < memory->_lives && i < 4; i++)
        GPU_DrawFilledCircle(C_YELLOW, (int16_t)(GPU_X - 14 - i*16), 10, 6);
    len = sprintf(text, "DOTS %u", memory->_dotsLeft);
    GPU_DrawText(C_GRAY_MEDIUM, fm, 4, (int16_t)(GPU_Y-14), text, (uint16_t)len);

    if (memory->_state == 0)
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, fb, GPU_X/2, (int16_t)(MAZE_OY + 7*CELL), "READY!");
    else if (memory->_state == 3)
    {
        GPU_DrawPanel(RGB(16,8,8), C_RED, (int16_t)(GPU_X/2-80), (int16_t)(GPU_Y/2-40), 160, 88);
        GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-34), "GAME OVER");
        len = sprintf(text, "SCORE %6u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-10), text);
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", (int16_t)(GPU_X/2-40), (int16_t)(GPU_Y/2+20), 80, 28);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G21_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
