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
                    memory->_score++;
                    return;
                }
            }
    }
}

static void G_Init(void)
{
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
    memory->_score = 0;
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


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (0)


static void G_Update(void)
{
    KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	if (keyboard_held & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}

    const uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
	    KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
        if (keyboard != KEY_NONE && mainMemory._lastRenderedTick-memory->_lastKeyboardTick > 5)
        {
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
            _GenerateRandomField();
            memory->_lastKeyboardTick = mainMemory._lastRenderedTick;
        }
        CalculateNewSpriteStates();
        RemoveDoneSprites();
        mainMemory._lastRenderedTick++;
    }
    if (mainMemory.touchPressed)
	{
		if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
			G_Init();
	}
}

GPU_Color ValueToColor(Field2048 f)
{
    switch(f)
    {
        case F2048_1: return C_WHITE;
            break;
        case F2048_2: return C_GRAY_LIGHT;
            break;
        case F2048_4: return C_GRAY_MEDIUM;
            break;
        case F2048_8: return C_GRAY_DARK;
            break;
        case F2048_16: return C_GREEN;
            break;
        case F2048_32: return C_YELLOW;
            break;
        case F2048_64: return C_RED;
            break;
        case F2048_128: return C_BLUE;
            break;
        case F2048_256: return C_C1;
            break;
        case F2048_512: return C_C2;
            break;
        case F2048_1024: return C_C3;
            break;
        case F2048_2048: return C_C3;
            break;
        case F2048_4096: return C_C3;
            break;
        case F2048_8192: return C_C3;
            break;
        default:
            break;
    }
    return C_WHITE;
}

#define BLOCK_SIZE ( (GPU_X/2 < GPU_Y) ? (GPU_X/8) : (GPU_Y/4) )
#define START_X ( (GPU_X/2 - 4*BLOCK_SIZE)/2 )
#define START_Y ( (GPU_Y - 4*BLOCK_SIZE)/2 )

static void DrawSingleSprite(uint8_t i)
{
    Sprite * s = &memory->_sprites[i];
    if (s->AnimationState == ANIM_DONE)
        return;
    int value = (int)s->value;
    GPU_Color color = ValueToColor(s->value);
    GPU_ClearTexture();
    char text[5];
    int len = sprintf(text, "%d", value);
    const FontDescription * f = &fontDescription[2];
    for (uint8_t i=0; i<4; i++)
        Texture_DrawEmptySquare(color, i, i, TEXTURE_SIZE-2*i, TEXTURE_SIZE-2*i);
    if (value != 0)
        Texture_DrawText(color, f, TEXTURE_SIZE/2-(len)*f->width/2, TEXTURE_SIZE/2-f->height/2, text, len);
    uint32_t tick = s->currentDrawTick;
    switch (s->AnimationState)
    {
    case ANIM_DELETE:
        GPU_DrawRotatedTexture(START_X + s->goal.x*BLOCK_SIZE+BLOCK_SIZE/2, START_Y + s->goal.y*BLOCK_SIZE+BLOCK_SIZE/2, (BLOCK_SIZE-(BLOCK_SIZE/8)*tick)/(double)TEXTURE_SIZE, tick*0.2);
        break;
    case ANIM_LOOP:
        GPU_DrawRotatedTexture(START_X + s->goal.x*BLOCK_SIZE+BLOCK_SIZE/2, START_Y + s->goal.y*BLOCK_SIZE+BLOCK_SIZE/2, BLOCK_SIZE/(double)TEXTURE_SIZE, 0);
        break;
    case ANIM_GENERATE:
        GPU_DrawRotatedTexture(START_X + s->goal.x*BLOCK_SIZE+BLOCK_SIZE/2, START_Y + s->goal.y*BLOCK_SIZE+BLOCK_SIZE/2, ((BLOCK_SIZE/8)*tick)/(double)TEXTURE_SIZE, 1.4-tick*0.2);
        break;
    case ANIM_MOVE:
        GPU_DrawRotatedTexture(START_X + (s->goal.x*(tick)+s->created.x*(8-tick))*(BLOCK_SIZE/8)+BLOCK_SIZE/2, START_Y + (s->goal.y*(tick)+s->created.y*(8-tick))*(BLOCK_SIZE/8)+BLOCK_SIZE/2, BLOCK_SIZE/(double)TEXTURE_SIZE, 0);
        break;
    default:
        break;
    }    
}

static void G_Draw(void)
{
    const FontDescription * font_s = &fontDescription[0];
    const FontDescription * font_m = &fontDescription[1];
    const FontDescription * font_l = &fontDescription[2];
    const FontDescription * font_xl = &fontDescription[3];
    GPU_ClearFramebuffers();
    GPU_DrawEmptySquare(C_WHITE, START_X, START_Y, BLOCK_SIZE*4-1, BLOCK_SIZE*4-1);
    // Draw first screen
    for (uint8_t i=0; i<memory->_spritesNum; i++)
        DrawSingleSprite(i);
    // Draw second screen
    GPU_DrawFilledSquare(C_GREEN, GPU_X/2, GPU_Y/4, GPU_X/2, GPU_Y/2);
    char text[5];
    int len = sprintf(text, "%d", memory->_score);
    GPU_DrawText(C_BLACK, font_xl, S2_CENTER_X-(len)*font_xl->width/2, S1_CENTER_Y-font_xl->height/2, text, len);
    // Draw reset button
	{
		const char * textToDisplay = "RESET";
		int8_t len = strlen(textToDisplay);
		GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
		GPU_DrawText(C_BLACK, font_xl, RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
	}
}

static void G_Deinit(void)
{

}

const Callbacks G9_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
