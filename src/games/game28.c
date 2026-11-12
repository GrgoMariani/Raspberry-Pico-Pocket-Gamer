// SOKOBAN - push every crate onto a target square

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define SK_COLS 10
#define SK_ROWS 8
#define SK_CELL 24
#define SK_OX ((GPU_X - SK_COLS*SK_CELL)/2)
#define SK_OY 28
#define SK_LEVELS 5

typedef struct {
    uint8_t wall[SK_ROWS][SK_COLS];
    uint8_t target[SK_ROWS][SK_COLS];
    uint8_t box[SK_ROWS][SK_COLS];
    uint8_t px, py;
    uint8_t level;
    uint16_t moves;
    uint8_t won;           /* level solved, waiting to advance */
    uint8_t allDone;
    uint32_t winTime;      /* when the level was solved (for the banner delay) */
    /* single-step undo */
    uint8_t uValid, uPx, uPy, uPush, uBx, uBy, uBx2, uBy2;
} Game28Memory;

static Game28Memory * const memory = (Game28Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static const char * _levels[SK_LEVELS][SK_ROWS] = {
    {
        "##########",
        "#        #",
        "#  @$.   #",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "##########",
    },
    {
        "##########",
        "#        #",
        "#  @$ .  #",
        "#        #",
        "#   $ .  #",
        "#        #",
        "#        #",
        "##########",
    },
    {
        "##########",
        "#        #",
        "#   .    #",
        "#   $    #",
        "#  @     #",
        "#        #",
        "#        #",
        "##########",
    },
    {
        "##########",
        "#        #",
        "#  .  .  #",
        "#  $  $  #",
        "#   @    #",
        "#        #",
        "#        #",
        "##########",
    },
    {
        "##########",
        "#        #",
        "#  $ .   #",
        "#  #     #",
        "#  @ $.  #",
        "#        #",
        "#        #",
        "##########",
    },
};

#define UNDO_BTN_X 4
#define UNDO_BTN_W 56
#define RESET_BTN_X (GPU_X-62)
#define RESET_BTN_W 58
#define BTN_Y 4
#define BTN_H 16

static void LoadLevel(uint8_t level)
{
    memset(memory->wall, 0, sizeof(memory->wall));
    memset(memory->target, 0, sizeof(memory->target));
    memset(memory->box, 0, sizeof(memory->box));
    memory->level = level;
    memory->moves = 0;
    memory->won = 0;
    memory->uValid = 0;
    for (uint8_t y = 0; y < SK_ROWS; y++)
        for (uint8_t x = 0; x < SK_COLS; x++)
        {
            char c = _levels[level][y][x];
            if (c == '#') memory->wall[y][x] = 1;
            if (c == '.' || c == '*' || c == '+') memory->target[y][x] = 1;
            if (c == '$' || c == '*') memory->box[y][x] = 1;
            if (c == '@' || c == '+') { memory->px = x; memory->py = y; }
        }
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game28Memory));
    LoadLevel(0);
    mainMemory._gameState = GS_RUNNING;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static uint8_t Solved(void)
{
    for (uint8_t y = 0; y < SK_ROWS; y++)
        for (uint8_t x = 0; x < SK_COLS; x++)
            if (memory->target[y][x] && !memory->box[y][x])
                return 0;
    return 1;
}

static void TryMove(int dx, int dy)
{
    if (memory->won || memory->allDone)
        return;
    int nx = memory->px + dx, ny = memory->py + dy;
    if (nx < 0 || nx >= SK_COLS || ny < 0 || ny >= SK_ROWS)
        return;
    if (memory->wall[ny][nx])
        return;
    uint8_t push = 0;
    int bx = 0, by = 0;
    if (memory->box[ny][nx])
    {
        bx = nx + dx; by = ny + dy;
        if (bx < 0 || bx >= SK_COLS || by < 0 || by >= SK_ROWS)
            return;
        if (memory->wall[by][bx] || memory->box[by][bx])
            return;
        push = 1;
    }
    /* the move is legal: only now take the undo snapshot. A blocked push used
       to leave a phantom undo that decremented the move counter on its own. */
    memory->uValid = 1;
    memory->uPx = memory->px;
    memory->uPy = memory->py;
    memory->uPush = push;
    if (push)
    {
        memory->uBx = (uint8_t)nx; memory->uBy = (uint8_t)ny;
        memory->uBx2 = (uint8_t)bx; memory->uBy2 = (uint8_t)by;
        memory->box[ny][nx] = 0;
        memory->box[by][bx] = 1;
    }
    memory->px = (uint8_t)nx;
    memory->py = (uint8_t)ny;
    memory->moves++;
    if (Solved())
    {
        memory->won = 1;
        memory->winTime = IF_GetCurrentTime();
    }
}

static void Undo(void)
{
    if (!memory->uValid || memory->won)
        return;
    memory->px = memory->uPx;
    memory->py = memory->uPy;
    if (memory->uPush)
    {
        memory->box[memory->uBy2][memory->uBx2] = 0;
        memory->box[memory->uBy][memory->uBx] = 1;
    }
    if (memory->moves)
        memory->moves--;
    memory->uValid = 0;
}

static void G_Update(void)
{
    if (mainMemory.touchPressedNew)
    {
        uint16_t tx = mainMemory.touch_X, ty = mainMemory.touch_Y;
        if (ty < SK_OY)
        {
            if (tx >= UNDO_BTN_X && tx < UNDO_BTN_X+UNDO_BTN_W)
                Undo();
            else if (tx >= RESET_BTN_X && tx < RESET_BTN_X+RESET_BTN_W)
                LoadLevel(memory->level);
            return;
        }
    }
    if (memory->allDone)
    {
        if (mainMemory.touchPressedNew || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    if (memory->won)
    {
        /* show the SOLVED banner for ~1.5s of real time (the old counter ran
           on main loop iterations and flashed by in a few milliseconds) */
        if (IF_GetCurrentTime() - memory->winTime > 1500000)
        {
            if (memory->level + 1 >= SK_LEVELS)
                memory->allDone = 1;
            else
                LoadLevel((uint8_t)(memory->level + 1));
        }
        return;
    }
    /* keyboard: the four directions move the player */
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    if (keys & KEY_LEFT)  TryMove(-1, 0);
    if (keys & KEY_RIGHT) TryMove(1, 0);
    if (keys & KEY_UP)    TryMove(0, -1);
    if (keys & KEY_DOWN)  TryMove(0, 1);
    /* touch: tap a cell to step towards it */
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty) && ty >= SK_OY)
    {
        int cx = ((int)tx - SK_OX)/SK_CELL;
        int cy = ((int)ty - SK_OY)/SK_CELL;
        int dx = cx - memory->px, dy = cy - memory->py;
        if (dx || dy)
        {
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > dy)
                TryMove((cx > memory->px) ? 1 : -1, 0);
            else
                TryMove(0, (cy > memory->py) ? 1 : -1);
        }
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[24];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(14,12,28), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* header */
    GPU_DrawButton(C_GRAY_DARK, C_WHITE, fs, "UNDO", UNDO_BTN_X, BTN_Y, UNDO_BTN_W, BTN_H);
    sprintf(text, "LEVEL %u/%u", (unsigned)(memory->level+1), (unsigned)SK_LEVELS);
    GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, 6, text);
    GPU_DrawButton(C_GRAY_DARK, C_WHITE, fs, "RESET", RESET_BTN_X, BTN_Y, RESET_BTN_W, BTN_H);

    for (uint8_t y = 0; y < SK_ROWS; y++)
        for (uint8_t x = 0; x < SK_COLS; x++)
        {
            int16_t px = (int16_t)(SK_OX + x*SK_CELL);
            int16_t py = (int16_t)(SK_OY + y*SK_CELL);
            if (memory->wall[y][x])
            {
                GPU_DrawBevelSquare(RGB(70,66,90), px, py, SK_CELL-1, SK_CELL-1);
                continue;
            }
            GPU_DrawFilledSquare(RGB(24,22,38), px, py, SK_CELL-1, SK_CELL-1);
            if (memory->target[y][x])
            {
                GPU_DrawGlow(C_C3, (int16_t)(px+SK_CELL/2), (int16_t)(py+SK_CELL/2), 7);
                GPU_DrawFilledCircle(C_C3, (int16_t)(px+SK_CELL/2), (int16_t)(py+SK_CELL/2), 3);
            }
            if (memory->box[y][x])
            {
                GPU_Color bc = memory->target[y][x] ? C_GREEN : C_ORANGE;
                GPU_DrawBevelSquare(bc, (int16_t)(px+3), (int16_t)(py+3), SK_CELL-7, SK_CELL-7);
                GPU_DrawLine(GPU_ShadeColor(bc, 140), (int16_t)(px+3), (int16_t)(py+3), (int16_t)(px+SK_CELL-4), (int16_t)(py+SK_CELL-4));
                GPU_DrawLine(GPU_ShadeColor(bc, 140), (int16_t)(px+SK_CELL-4), (int16_t)(py+3), (int16_t)(px+3), (int16_t)(py+SK_CELL-4));
            }
        }
    /* player */
    {
        int16_t px = (int16_t)(SK_OX + memory->px*SK_CELL + SK_CELL/2);
        int16_t py = (int16_t)(SK_OY + memory->py*SK_CELL + SK_CELL/2);
        GPU_DrawGlow(C_C1, px, py, 10);
        GPU_DrawFilledCircle(C_C1, px, py, 8);
        GPU_DrawFilledCircle(C_WHITE, (int16_t)(px-3), (int16_t)(py-2), 2);
        GPU_DrawFilledCircle(C_WHITE, (int16_t)(px+3), (int16_t)(py-2), 2);
        GPU_DrawPixel(C_BLACK, (uint16_t)(px-3), (uint16_t)(py-2));
        GPU_DrawPixel(C_BLACK, (uint16_t)(px+3), (uint16_t)(py-2));
    }

    sprintf(text, "MOVES %u", (unsigned)memory->moves);
    GPU_DrawTextCentered(C_GRAY_LIGHT, fm, GPU_X/2, (int16_t)(GPU_Y-14), text);

    if (memory->won)
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-10), "SOLVED!");
    if (memory->allDone)
    {
        GPU_DrawPanel(RGB(16,24,16), C_C3, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-34), 180, 68);
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-28), "ALL LEVELS DONE!");
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-2), "you are a sokoban master");
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G28_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };