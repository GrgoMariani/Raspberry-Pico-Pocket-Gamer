// HANGMAN - guess the word before the figure is complete

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

#define HM_MAX_WRONG 6
#define HM_LETTERS 26
#define HM_COLS 13
#define HM_CELL_W 23
#define HM_CELL_H 22
#define HM_GRID_OX ((GPU_X - HM_COLS*HM_CELL_W)/2)
#define HM_GRID_OY 178
#define HM_ROW_GAP 24

typedef struct {
    char word[16];
    char hint[20];
    uint8_t len;
    uint8_t letterState[HM_LETTERS];   /* 0 unseen, 1 correct, 2 wrong */
    uint8_t wrong;
    uint8_t cursor;                    /* keyboard cursor over the alphabet */
    uint8_t over;                      /* 0 playing, 1 won, 2 lost */
} Game36Memory;

static Game36Memory * const memory = (Game36Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint16_t wins = 0, losses = 0;

typedef struct { const char * word; const char * hint; } WordEntry;
static const WordEntry _words[] = {
    { "ELEPHANT", "ANIMAL" },   { "GIRAFFE", "ANIMAL" },    { "DOLPHIN", "ANIMAL" },
    { "PENGUIN", "ANIMAL" },    { "KANGAROO", "ANIMAL" },   { "BUTTERFLY", "ANIMAL" },
    { "TURTLE", "ANIMAL" },     { "RABBIT", "ANIMAL" },     { "MONKEY", "ANIMAL" },
    { "ZEBRA", "ANIMAL" },      { "OCTOPUS", "ANIMAL" },    { "HEDGEHOG", "ANIMAL" },
    { "BANANA", "FOOD" },       { "PIZZA", "FOOD" },        { "PANCAKE", "FOOD" },
    { "SPAGHETTI", "FOOD" },    { "CHOCOLATE", "FOOD" },    { "SANDWICH", "FOOD" },
    { "STRAWBERRY", "FOOD" },   { "POPCORN", "FOOD" },      { "CARROT", "FOOD" },
    { "ROCKET", "SPACE" },      { "PLANET", "SPACE" },      { "GALAXY", "SPACE" },
    { "COMET", "SPACE" },       { "ASTEROID", "SPACE" },    { "SATURN", "SPACE" },
    { "TELESCOPE", "SPACE" },   { "GRAVITY", "SPACE" },     { "ORBIT", "SPACE" },
    { "BICYCLE", "THING" },     { "UMBRELLA", "THING" },    { "COMPUTER", "THING" },
    { "KEYBOARD", "THING" },    { "BACKPACK", "THING" },    { "FLASHLIGHT", "THING" },
    { "TRAMPOLINE", "THING" },  { "SUBMARINE", "THING" },   { "HELICOPTER", "THING" },
    { "VOLCANO", "NATURE" },    { "RAINBOW", "NATURE" },    { "GLACIER", "NATURE" },
    { "THUNDER", "NATURE" },    { "DESERT", "NATURE" },     { "WATERFALL", "NATURE" },
    { "SOCCER", "SPORT" },      { "TENNIS", "SPORT" },      { "HOCKEY", "SPORT" },
    { "BASEBALL", "SPORT" },    { "SWIMMING", "SPORT" },    { "GYMNASTICS", "SPORT" },
};
#define NUM_WORDS ((int)(sizeof(_words)/sizeof(_words[0])))

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game36Memory));
    const WordEntry * w = &_words[Random_Range(NUM_WORDS)];
    strncpy(memory->word, w->word, sizeof(memory->word)-1);
    strncpy(memory->hint, w->hint, sizeof(memory->hint)-1);
    memory->len = (uint8_t)strlen(memory->word);
    memory->cursor = 0;
    memory->over = 0;
    mainMemory._gameState = GS_RUNNING;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static uint8_t WordComplete(void)
{
    for (uint8_t i = 0; i < memory->len; i++)
    {
        uint8_t c = (uint8_t)(memory->word[i] - 'A');
        if (memory->letterState[c] != 1)
            return 0;
    }
    return 1;
}

static void Guess(uint8_t letter)
{
    if (memory->over || letter >= HM_LETTERS)
        return;
    if (memory->letterState[letter])
        return;                       /* already tried */
    uint8_t found = 0;
    for (uint8_t i = 0; i < memory->len; i++)
        if (memory->word[i] == (char)('A' + letter))
            found = 1;
    if (found)
    {
        memory->letterState[letter] = 1;
        if (WordComplete())
        {
            memory->over = 1;
            wins++;
            mainMemory._gameState = GS_DONE;
        }
    }
    else
    {
        memory->letterState[letter] = 2;
        memory->wrong++;
        if (memory->wrong >= HM_MAX_WRONG)
        {
            memory->over = 2;
            losses++;
            mainMemory._gameState = GS_DONE;
        }
    }
}

static void G_Update(void)
{
    if (memory->over)
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    /* keyboard: arrows walk the alphabet grid, UP+DOWN together guesses */
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    uint8_t cx = (uint8_t)(memory->cursor % HM_COLS), cy = (uint8_t)(memory->cursor / HM_COLS);
    if ((keys & KEY_UP) && (keys & KEY_DOWN))
    {
        Guess(memory->cursor);
    }
    else
    {
        if (keys & KEY_LEFT)  cx = (uint8_t)((cx + HM_COLS - 1) % HM_COLS);
        if (keys & KEY_RIGHT) cx = (uint8_t)((cx + 1) % HM_COLS);
        if (keys & KEY_UP)    cy = (uint8_t)((cy + 1) % 2);
        if (keys & KEY_DOWN)  cy = (uint8_t)((cy + 1) % 2);
        uint8_t idx = (uint8_t)(cy*HM_COLS + cx);
        if (idx < HM_LETTERS)
            memory->cursor = idx;
    }
    /* touch: tap a letter */
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty) && ty >= HM_GRID_OY)
    {
        int col = ((int)tx - HM_GRID_OX)/HM_CELL_W;
        int row = ((int)ty - HM_GRID_OY)/HM_ROW_GAP;
        if (col >= 0 && col < HM_COLS && row >= 0 && row < 2)
        {
            int idx = row*HM_COLS + col;
            if (idx < HM_LETTERS)
            {
                memory->cursor = (uint8_t)idx;
                Guess((uint8_t)idx);
            }
        }
    }
}

static void DrawGallows(void)
{
    GPU_Color c = C_GRAY_LIGHT;
    /* the gallows */
    GPU_DrawLine(c, 20, 160, 100, 160);
    GPU_DrawLine(c, 40, 160, 40, 45);
    GPU_DrawLine(c, 40, 45, 90, 45);
    GPU_DrawLine(c, 90, 45, 90, 58);
    /* the figure, one part per wrong guess */
    if (memory->wrong >= 1) GPU_DrawEmptyCircle(c, 90, 68, 10);
    if (memory->wrong >= 2) GPU_DrawLine(c, 90, 78, 90, 112);
    if (memory->wrong >= 3) GPU_DrawLine(c, 90, 86, 74, 104);
    if (memory->wrong >= 4) GPU_DrawLine(c, 90, 86, 106, 104);
    if (memory->wrong >= 5) GPU_DrawLine(c, 90, 112, 76, 136);
    if (memory->wrong >= 6) GPU_DrawLine(c, 90, 112, 104, 136);
    /* a face once it is over */
    if (memory->wrong >= 1)
    {
        if (memory->over == 2)
        {
            GPU_DrawPixel(C_RED, 86, 66); GPU_DrawPixel(C_RED, 94, 66);
            GPU_DrawLine(C_RED, 86, 73, 94, 73);
        }
        else
        {
            GPU_DrawPixel(C_BLACK, 86, 66); GPU_DrawPixel(C_BLACK, 94, 66);
        }
    }
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    char text[32];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(16,12,30), C_BLACK, 0, 0, GPU_X, GPU_Y);

    /* the word: revealed letters and blanks */
    {
        int16_t total = (int16_t)(memory->len * (fb->width + 3));
        int16_t x = (int16_t)(GPU_X/2 - total/2);
        for (uint8_t i = 0; i < memory->len; i++)
        {
            uint8_t c = (uint8_t)(memory->word[i] - 'A');
            int16_t cx = (int16_t)(x + i*(fb->width + 3));
            if (memory->letterState[c] == 1 || memory->over == 2)
            {
                GPU_Color col = (memory->letterState[c] == 1) ? C_C3 : C_RED;
                GPU_DrawLetter(col, fb, cx, 6, memory->word[i]);
            }
            else
            {
                GPU_DrawLetter(C_GRAY_MEDIUM, fb, cx, 6, '_');
            }
        }
    }
    /* hint */
    sprintf(text, "HINT: %s", memory->hint);
    GPU_DrawTextCentered(C_YELLOW, fs, GPU_X/2, 30, text);

    DrawGallows();

    /* guesses left, on the right of the gallows */
    GPU_DrawText(C_GRAY_LIGHT, fm, 140, 56, "GUESSES LEFT", 12);
    sprintf(text, "%d", (int)(HM_MAX_WRONG - memory->wrong));
    GPU_DrawTextOutlined(memory->wrong >= 4 ? C_RED : C_C3, C_BLACK, fb, 160, 72, text);
    for (uint8_t i = 0; i < HM_MAX_WRONG; i++)
    {
        GPU_Color pc = (i < memory->wrong) ? C_RED : C_GRAY_DARK;
        GPU_DrawFilledCircle(pc, (int16_t)(144 + i*16), 108, 5);
    }
    sprintf(text, "WINS %u  LOSSES %u", (unsigned)wins, (unsigned)losses);
    GPU_DrawText(C_GRAY_MEDIUM, fs, 140, 128, text, (uint16_t)strlen(text));
    GPU_DrawText(C_GRAY_DARK, fs, 140, 142, "arrows + UP+DOWN", 16);
    GPU_DrawText(C_GRAY_DARK, fs, 140, 152, "or tap a letter", 15);

    /* alphabet grid */
    for (uint8_t i = 0; i < HM_LETTERS; i++)
    {
        int16_t px = (int16_t)(HM_GRID_OX + (i % HM_COLS)*HM_CELL_W);
        int16_t py = (int16_t)(HM_GRID_OY + (i / HM_COLS)*HM_ROW_GAP);
        uint8_t st = memory->letterState[i];
        GPU_Color bg = st == 1 ? RGB(30,90,44) : st == 2 ? RGB(90,30,30) : RGB(52,56,104);
        GPU_DrawBevelSquare(bg, px+1, py+1, HM_CELL_W-2, HM_CELL_H-2);
        char letter[2] = { (char)('A' + i), 0 };
        GPU_Color fg = st == 0 ? C_WHITE : (st == 1 ? C_C3 : C_GRAY_MEDIUM);
        GPU_DrawTextCentered(fg, fm, (int16_t)(px+HM_CELL_W/2), (int16_t)(py+HM_CELL_H/2 - fm->height/2), letter);
        if (i == memory->cursor && !memory->over)
            GPU_DrawEmptySquare(C_YELLOW, px, py, HM_CELL_W-1, HM_CELL_H-1);
    }

    if (memory->over)
    {
        GPU_Color col = memory->over == 1 ? C_C3 : C_RED;
        GPU_DrawPanel(RGB(16,16,32), col, (int16_t)(GPU_X/2-90), 60, 180, 68);
        GPU_DrawTextOutlined(col, C_BLACK, fb, GPU_X/2, 66, memory->over == 1 ? "YOU GOT IT!" : "GAME OVER");
        sprintf(text, "the word was %.15s", memory->word);
        GPU_DrawTextCentered(C_WHITE, fs, GPU_X/2, 92, text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, 108, "tap or press a key");
    }
}

static void G_Deinit(void)
{
}

const Callbacks G36_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };