// 2048

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    ORIENTATION_0,
    ORIENTATION_90,
    ORIENTATION_180,
    ORIENTATION_270
} OrientationEnum;

typedef enum {
    ANIM_DONE = 0,
    ANIM_GENERATE = 1,
    ANIM_LOOP = 2,
    ANIM_MOVE = 4,
    ANIM_DELETE = 8
} AnimationState;

typedef enum {
    F2048_Empty = 0,
    F2048_1     = 1,
    F2048_2     = 2,
    F2048_4     = 4,
    F2048_8     = 8,
    F2048_16    = 16,
    F2048_32    = 32,
    F2048_64    = 64,
    F2048_128   = 128,
    F2048_256   = 256,
    F2048_512   = 512,
    F2048_1024  = 1024,
    F2048_2048  = 2048,
    F2048_4096  = 4096,
    F2048_8192  = 8192,
} Field2048;

typedef struct {
    uint8_t x;
    uint8_t y;
    OrientationEnum orientation;
} PositionInfo;

typedef struct {
    PositionInfo created;
    PositionInfo goal;
    int32_t currentDrawTick;
    AnimationState AnimationState;
    Field2048 value;
} Sprite;

typedef struct {
    uint32_t _lastKeyboardTick;
    Field2048 _field[4][4];
    Sprite _sprites[48];
    uint8_t _spritesNum;
    uint32_t _score;
    uint32_t _best;
    uint8_t _won;
    uint8_t _winBanner;
    OrientationEnum _orientation;
} Game9Memory;

static Game9Memory * const memory = (Game9Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 20

static OrientationEnum NextRotation(OrientationEnum orientation)
{
    if (orientation == ORIENTATION_0)
        return ORIENTATION_90;
    if (orientation == ORIENTATION_90)
        return ORIENTATION_180;
    if (orientation == ORIENTATION_180)
        return ORIENTATION_270;
    if (orientation == ORIENTATION_270)
        return ORIENTATION_0;
    return orientation;
}

static Field2048 Increase(Field2048 fieldValue)
{
    switch (fieldValue)
    {
    case F2048_1: return F2048_2; break;
    case F2048_2: return F2048_4; break;
    case F2048_4: return F2048_8; break;
    case F2048_8: return F2048_16; break;
    case F2048_16: return F2048_32; break;
    case F2048_32: return F2048_64; break;
    case F2048_64: return F2048_128; break;
    case F2048_128: return F2048_256; break;
    case F2048_256: return F2048_512; break;
    case F2048_512: return F2048_1024; break;
    case F2048_1024: return F2048_2048; break;
    case F2048_2048: return F2048_4096; break;
    case F2048_4096: return F2048_8192; break;
    default:
        break;
    }
    return F2048_Empty;
}


static void _NormalizePoint(uint8_t * x, uint8_t * y, OrientationEnum * currentOrientation)
{
    while (*currentOrientation != ORIENTATION_0)
    {
        uint8_t tx, ty;
        tx = 3-*y;
        ty = *x;
        *x = tx;
        *y = ty;
        *currentOrientation = NextRotation(*currentOrientation);
    }
}

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static uint8_t _CountValues(Field2048 value)
{
    uint8_t result = 0;
    for (uint8_t i=0; i<4; i++) 
        for (uint8_t j=0; j<4; j++)
        {
            if (memory->_field[i][j] == value)
                result++;
        }
    return result;
}

static void _RemoveSingleSprite(uint8_t pos)
{
    if (memory->_spritesNum == 0)
        return;
    for (uint8_t i=pos; i<memory->_spritesNum-1; i++)
        memory->_sprites[i] = memory->_sprites[i+1];
    memory->_spritesNum--;
}

static uint8_t FindActiveSpritePosition(uint8_t x, uint8_t y, OrientationEnum currentOrientation)
{
    _NormalizePoint(&x, &y, &currentOrientation);
    uint8_t result = 32;
    for (uint8_t i=0; i<memory->_spritesNum; i++)
        if (memory->_sprites[i].goal.x == x && 
            memory->_sprites[i].goal.y == y && 
            memory->_sprites[i].AnimationState & (ANIM_GENERATE | ANIM_LOOP | ANIM_MOVE))
            return i;
    return result;
}

static void AddSpriteToList(Sprite sprite)
{
    if (memory->_spritesNum >= 32)
        return;
    _NormalizePoint(&sprite.goal.x, &sprite.goal.y, &sprite.goal.orientation);
    memory->_sprites[memory->_spritesNum] = sprite;
    memory->_spritesNum++;
}

static void SetSpriteToBeRemoved(uint8_t x, uint8_t y, OrientationEnum orientation)
{
    int pos = FindActiveSpritePosition(x, y, orientation);
    if (pos >= memory->_spritesNum)
        return;
    memory->_sprites[pos].currentDrawTick = 0;
    memory->_sprites[pos].AnimationState = ANIM_DELETE;
}

static void SetSpriteToBeMoved(uint8_t x, uint8_t y, OrientationEnum orientation, uint8_t newX, uint8_t newY)
{
    int pos = FindActiveSpritePosition(x, y, orientation);
    if (pos >= memory->_spritesNum)
        return;
    Sprite * s = &memory->_sprites[pos];
    s->currentDrawTick = 0;
    s->AnimationState = ANIM_MOVE;
    s->created.x = x;
    s->created.y = y;
    s->created.orientation = orientation;
    _NormalizePoint(&s->created.x, &s->created.y, &s->created.orientation);
    s->goal.x = newX;
    s->goal.y = newY;
    s->goal.orientation = orientation;
    _NormalizePoint(&s->goal.x, &s->goal.y, &s->goal.orientation);
}

static void CalculateNewSpriteStates()
{
    const uint8_t ANIM_TICKS = 8;
    for (uint8_t i=0; i<memory->_spritesNum; i++)
    {
        Sprite * s = &memory->_sprites[i];
        int32_t tick = s->currentDrawTick;
        s->currentDrawTick++;
        s->currentDrawTick &= (ANIM_TICKS-1);
        switch (s->AnimationState)
        {
        case ANIM_GENERATE:
        case ANIM_MOVE:
            if (tick == (ANIM_TICKS-1))
            {
                s->currentDrawTick = 0;
                s->AnimationState = ANIM_LOOP;
            }
            break;
        case ANIM_DELETE:
            if (tick == (ANIM_TICKS-1))
            {
                s->currentDrawTick = 0;
                s->AnimationState = ANIM_DONE;
            }
            break;
        case ANIM_LOOP:
        default:
            // nothing
            break;
        }
    }
}

static void RemoveDoneSprites()
{
    for (int8_t i=memory->_spritesNum-1; i>=0; i--)
    {
        if (memory->_sprites[i].AnimationState == ANIM_DONE)
        {
            _RemoveSingleSprite(i);
        }
    }
}

static void _GenerateRandomField()
{
    uint8_t r = _CountValues(F2048_Empty);
    if (r)
    {
        uint8_t rand = IF_Random() % r+1;
        for (uint8_t i=0; i<4; i++)
            for (uint8_t j=0; j<4; j++)
            {
                if (memory->_field[i][j] == F2048_Empty)
                {
                    rand--;
                }
                if (!rand)
                {
                    Sprite s = {{i, j, ORIENTATION_0}, {i, j, ORIENTATION_0}, 0, ANIM_GENERATE, F2048_1};
                    AddSpriteToList(s);
                    memory->_field[i][j] = F2048_1;
                    return;
                }
            }
    }
}

static void G_Init(void)
{
    if (memory->_score > memory->_best)
        memory->_best = memory->_score;
    mainMemory._gameState = GS_RUNNING;
    uint32_t tick = CalculateTick();
    mainMemory._lastRenderedTick = tick;
    memory->_lastKeyboardTick = tick;
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
        {
            memory->_field[i][j] = F2048_Empty;
        }
    memory->_orientation = ORIENTATION_0;
    memory->_spritesNum = 0;
    memory->_score = 0;
    _GenerateRandomField();
    _GenerateRandomField();
    memory->_score = 0;   /* the two starting tiles are not scored */
}


static Sprite GetNextPair(uint8_t x, uint8_t y, OrientationEnum orientation)
{
    Sprite result = {{x, y, orientation}, {x, y, orientation}, 0, ANIM_LOOP, F2048_Empty};
    uint8_t savedx = x;
    for (uint8_t i=x; i<4; i++)
    {
        Field2048 here = memory->_field[i][y];
        if (here != F2048_Empty)
        {
            if (result.value == F2048_Empty)
            {
                savedx = i;
                result.value = here;
            }
            else if (result.value != here)
            {
                memory->_field[savedx][y] = F2048_Empty;
                // find the sprite
                if (savedx != x)
                {
                    SetSpriteToBeMoved(savedx, y, orientation, x, y);
                }
                return result;
            }
            else
            {
                SetSpriteToBeRemoved(savedx, y, orientation);
                memory->_field[savedx][y] = F2048_Empty;

                SetSpriteToBeRemoved(i, y, orientation);
                memory->_field[i][y] = F2048_Empty;

                result.AnimationState = ANIM_GENERATE;
                result.value = Increase(here);
                memory->_score += (uint32_t)result.value;
                if (result.value == F2048_2048 && !memory->_won)
                {
                    memory->_won = 1;
                    memory->_winBanner = 80;
                }
                AddSpriteToList(result);
                return result;
            }
        }
    }
    if (memory->_field[savedx][y] != F2048_Empty)
    {
        memory->_field[savedx][y] = F2048_Empty;
    }
    if (result.value != F2048_Empty)
    {
        SetSpriteToBeMoved(savedx, y, orientation, x, y);
    }
    return result;
}

static void Rotate90()
{
    Field2048 t_field[4][4];
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            t_field[3-j][i] = memory->_field[i][j];
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            memory->_field[i][j] = t_field[i][j];
    memory->_orientation = NextRotation(memory->_orientation);
}

static void Rotate180()
{
    Rotate90();
    Rotate90();
}

static void Rotate270()
{
    Rotate180();
    Rotate90();
}

static void GoLeft(OrientationEnum orientation)
{
    for (uint8_t row=0; row<4; row++)
    {
        for (uint8_t col=0; col<4; col++)
        {
            Sprite r = GetNextPair(col, row, orientation);
            memory->_field[col][row] = r.value;
        }
    }
}

static void CopyField(Field2048 * dst)
{
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            dst[i*4+j] = memory->_field[i][j];
}

static uint8_t FieldChanged(const Field2048 * before)
{
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
            if (before[i*4+j] != memory->_field[i][j])
                return 1;
    return 0;
}

/* no empty cell and no neighbour with the same value -> nothing can be moved */
static uint8_t BoardIsDead(void)
{
    for (uint8_t i=0; i<4; i++)
        for (uint8_t j=0; j<4; j++)
        {
            Field2048 here = memory->_field[i][j];
            if (here == F2048_Empty)
                return 0;
            if (i+1 < 4 && memory->_field[i+1][j] == here)
                return 0;
            if (j+1 < 4 && memory->_field[i][j+1] == here)
                return 0;
        }
    return 1;
}


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (152)


static void G_Update(void)
{
    const uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
	    KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
        if (keyboard != KEY_NONE && mainMemory._lastRenderedTick-memory->_lastKeyboardTick > 5 &&
            mainMemory._gameState == GS_RUNNING)
        {
            Field2048 before[16];
            CopyField(before);
            if (keyboard & KEY_LEFT)
            {
                GoLeft(ORIENTATION_0);
            }
            if (keyboard & KEY_DOWN)
            {
                Rotate90();
                GoLeft(ORIENTATION_90);
                Rotate270();
            }
            if (keyboard & KEY_RIGHT)
            {
                Rotate180();
                GoLeft(ORIENTATION_180);
                Rotate180();
            }
            if (keyboard & KEY_UP)
            {
                Rotate270();
                GoLeft(ORIENTATION_270);
                Rotate90();
            }
            if (FieldChanged(before))
            {
                _GenerateRandomField();
                if (BoardIsDead())
                    mainMemory._gameState = GS_DONE;
            }
            memory->_lastKeyboardTick = mainMemory._lastRenderedTick;
        }
        CalculateNewSpriteStates();
        RemoveDoneSprites();
        if (memory->_winBanner)
            memory->_winBanner--;
        mainMemory._lastRenderedTick++;
    }
    if (mainMemory.touchPressedNew)
	{
		if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
			G_Init();
	}
}

/* ------------------------------------------------------------------ drawing */

#define B2_CELL 34
#define B2_GAP 3
#define B2_BOARD (4*B2_CELL + 5*B2_GAP)
#define B2_X ((GPU_X/2 - B2_BOARD)/2)
#define B2_Y ((GPU_Y - B2_BOARD)/2)

typedef struct {
    GPU_Color bg;
    GPU_Color fg;
} TileStyle;

/* the classic 2048 palette, from warm paper white to gold, dark for the tiles
   above 2048 */
static const TileStyle _tileStyle[13] = {
    { RGB(206,202,196), RGB(72,66,58) },    /*    1 (this board starts at 1) */
    { RGB(232,224,212), RGB(72,66,58) },    /*    2 */
    { RGB(226,216,192), RGB(72,66,58) },    /*    4 */
    { RGB(242,172,110), RGB(255,255,255) }, /*    8 */
    { RGB(242,142,96),  RGB(255,255,255) }, /*   16 */
    { RGB(240,112,82),  RGB(255,255,255) }, /*   32 */
    { RGB(238,86,52),   RGB(255,255,255) }, /*   64 */
    { RGB(238,202,108), RGB(255,255,255) }, /*  128 */
    { RGB(236,194,88),  RGB(255,255,255) }, /*  256 */
    { RGB(234,186,68),  RGB(255,255,255) }, /*  512 */
    { RGB(232,178,48),  RGB(255,255,255) }, /* 1024 */
    { RGB(230,170,28),  RGB(255,255,255) }, /* 2048 */
    { RGB(62,58,54),    RGB(240,236,228) }, /* above */
};

static const TileStyle * StyleFor(Field2048 value)
{
    switch (value)
    {
    case F2048_1:    return &_tileStyle[0];
    case F2048_2:    return &_tileStyle[1];
    case F2048_4:    return &_tileStyle[2];
    case F2048_8:    return &_tileStyle[3];
    case F2048_16:   return &_tileStyle[4];
    case F2048_32:   return &_tileStyle[5];
    case F2048_64:   return &_tileStyle[6];
    case F2048_128:  return &_tileStyle[7];
    case F2048_256:  return &_tileStyle[8];
    case F2048_512:  return &_tileStyle[9];
    case F2048_1024: return &_tileStyle[10];
    case F2048_2048: return &_tileStyle[11];
    default:         return &_tileStyle[12];
    }
}

/* pixel origin of a (possibly fractional) board coordinate */
static void CellPixel(float cx, float cy, int16_t * px, int16_t * py)
{
    *px = (int16_t)(B2_X + B2_GAP + cx*(B2_CELL + B2_GAP));
    *py = (int16_t)(B2_Y + B2_GAP + cy*(B2_CELL + B2_GAP));
}

/* shade is the 0..256 brightness of GPU_ShadeColor, 256 = untouched */
static void DrawTile(Field2048 value, int16_t px, int16_t py, int16_t size, int16_t shade, uint8_t withText)
{
    const TileStyle * style = StyleFor(value);
    GPU_DrawBevelSquare(GPU_ShadeColor(style->bg, shade), px, py, size, size);
    if (withText && size >= 18)
    {
        char text[8];
        int len = sprintf(text, "%d", (int)value);
        const FontDescription * f = (len > 2) ? &fontDescription[1] : &fontDescription[2];
        GPU_DrawTextCentered(GPU_ShadeColor(style->fg, shade), f, (int16_t)(px + size/2),
                             (int16_t)(py + size/2 - f->height/2), text);
    }
}

static void DrawSlot(int16_t px, int16_t py)
{
    GPU_DrawFilledSquare(RGB(26,28,44), px, py, B2_CELL, B2_CELL);
    GPU_DrawLine(RGB(16,17,28), px, py, (int16_t)(px+B2_CELL-1), py);
    GPU_DrawLine(RGB(16,17,28), px, py, px, (int16_t)(py+B2_CELL-1));
    GPU_DrawLine(RGB(44,48,72), px, (int16_t)(py+B2_CELL-1), (int16_t)(px+B2_CELL-1), (int16_t)(py+B2_CELL-1));
    GPU_DrawLine(RGB(44,48,72), (int16_t)(px+B2_CELL-1), py, (int16_t)(px+B2_CELL-1), (int16_t)(py+B2_CELL-1));
}

static float AnimT(const Sprite * s)
{
    return (float)s->currentDrawTick / 7.0f;
}

static void DrawSprite(const Sprite * s)
{
    int16_t px, py;
    switch (s->AnimationState)
    {
    case ANIM_DELETE:
    {
        /* the merged-away tile shrinks and darkens under the new one */
        float t = AnimT(s);
        int16_t size = (int16_t)(B2_CELL * (1.0f - 0.65f*t));
        CellPixel((float)s->goal.x, (float)s->goal.y, &px, &py);
        px = (int16_t)(px + (B2_CELL - size)/2);
        py = (int16_t)(py + (B2_CELL - size)/2);
        DrawTile(s->value, px, py, size, (int16_t)(256 - (int16_t)(140*t)), 0);
        break;
    }
    case ANIM_MOVE:
    {
        float t = AnimT(s);
        float cx = (float)s->created.x + ((float)s->goal.x - (float)s->created.x)*t;
        float cy = (float)s->created.y + ((float)s->goal.y - (float)s->created.y)*t;
        CellPixel(cx, cy, &px, &py);
        DrawTile(s->value, px, py, B2_CELL, 256, 1);
        break;
    }
    case ANIM_GENERATE:
    {
        /* pop in with an ease out */
        float t = AnimT(s);
        float e = t*(2.0f - t);
        float scale = 0.25f + 0.75f*e;
        int16_t size = (int16_t)(B2_CELL * scale);
        CellPixel((float)s->goal.x, (float)s->goal.y, &px, &py);
        px = (int16_t)(px + (B2_CELL - size)/2);
        py = (int16_t)(py + (B2_CELL - size)/2);
        DrawTile(s->value, px, py, size, 256, (uint8_t)(scale > 0.6f));
        break;
    }
    case ANIM_LOOP:
    default:
        CellPixel((float)s->goal.x, (float)s->goal.y, &px, &py);
        DrawTile(s->value, px, py, B2_CELL, 256, 1);
        break;
    }
}

static void DrawBoard(void)
{
    GPU_DrawBevelSquare(RGB(40,42,64), B2_X, B2_Y, B2_BOARD, B2_BOARD);
    for (uint8_t y = 0; y < 4; y++)
        for (uint8_t x = 0; x < 4; x++)
        {
            int16_t px, py;
            CellPixel((float)x, (float)y, &px, &py);
            DrawSlot(px, py);
        }
    /* merged tiles first so that sliding tiles pass over them */
    for (uint8_t i = 0; i < memory->_spritesNum; i++)
        if (memory->_sprites[i].AnimationState == ANIM_DELETE)
            DrawSprite(&memory->_sprites[i]);
    for (uint8_t i = 0; i < memory->_spritesNum; i++)
        if (memory->_sprites[i].AnimationState == ANIM_MOVE)
            DrawSprite(&memory->_sprites[i]);
    for (uint8_t i = 0; i < memory->_spritesNum; i++)
        if (memory->_sprites[i].AnimationState != ANIM_DELETE &&
            memory->_sprites[i].AnimationState != ANIM_MOVE)
            DrawSprite(&memory->_sprites[i]);
}

static void G_Draw(void)
{
    const FontDescription * font_s = &fontDescription[0];
    const FontDescription * font_m = &fontDescription[1];
    const FontDescription * font_l = &fontDescription[2];
    const FontDescription * font_xl = &fontDescription[3];
    char text[16];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(12,12,32), C_BLACK, 0, 0, GPU_X, GPU_Y);

    DrawBoard();

    /* right hand console */
    GPU_DrawPanel(RGB(20,22,44), C_GRAY_DARK, (int16_t)(GPU_X/2+6), 8, (int16_t)(GPU_X/2-12), 74);
    GPU_DrawTextCentered(C_GRAY_LIGHT, font_s, S2_CENTER_X, 14, "SCORE");
    sprintf(text, "%u", (unsigned)memory->_score);
    GPU_DrawTextCentered(C_WHITE, font_xl, S2_CENTER_X, 26, text);
    sprintf(text, "BEST %u", (unsigned)(memory->_score > memory->_best ? memory->_score : memory->_best));
    GPU_DrawTextCentered(C_YELLOW, font_s, S2_CENTER_X, 58, text);

    GPU_DrawPanel(RGB(20,22,44), C_GRAY_DARK, (int16_t)(GPU_X/2+6), 90, (int16_t)(GPU_X/2-12), 52);
    GPU_DrawTextCentered(C_GRAY_LIGHT, font_s, S2_CENTER_X, 96, "ARROWS OR SWIPE");
    GPU_DrawTextCentered(C_GRAY_LIGHT, font_s, S2_CENTER_X, 108, "MERGE THE TILES");
    GPU_DrawTextCentered(C_GRAY_MEDIUM, font_s, S2_CENTER_X, 124, "REACH 2048");

    GPU_DrawButton(C_YELLOW, C_BLACK, font_l, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                   RESET_BUTTON_W, RESET_BUTTON_H);

    if (memory->_winBanner)
    {
        GPU_DrawPanel(RGB(60,48,8), C_YELLOW, (int16_t)(GPU_X/4-66), (int16_t)(GPU_Y/2-24), 132, 48);
        GPU_DrawTextOutlined(C_YELLOW, C_BLACK, font_xl, S1_CENTER_X, (int16_t)(GPU_Y/2-18), "2048!");
    }
    else if (mainMemory._gameState == GS_DONE)
    {
        GPU_DrawPanel(RGB(24,10,10), C_RED, (int16_t)(GPU_X/4-70), (int16_t)(GPU_Y/2-32), 140, 64);
        GPU_DrawTextOutlined(C_RED, C_BLACK, font_l, S1_CENTER_X, (int16_t)(GPU_Y/2-26), "NO MOVES");
        sprintf(text, "SCORE %u", (unsigned)memory->_score);
        GPU_DrawTextCentered(C_WHITE, font_m, S1_CENTER_X, (int16_t)(GPU_Y/2-2), text);
    }
    (void)font_l;
}

static void G_Deinit(void)
{

}

const Callbacks G9_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
