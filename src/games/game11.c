// Snake

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>

typedef enum 
{
    P15_EMPTY,
    P15_1,
    P15_2,
    P15_3,
    P15_4,
    P15_5,
    P15_6,
    P15_7,
    P15_8,
    P15_9,
    P15_10,
    P15_11,
    P15_12,
    P15_13,
    P15_14,
    P15_15
} P15ValuesEnum;

typedef struct {
    P15ValuesEnum values[4][4];
} Game11Memory;

static Game11Memory * const memory = (Game11Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 30

#define SNAKE_W 32
#define SNAKE_H 24

typedef enum {
	D_LEFT,
	D_RIGHT,
	D_UP,
	D_DOWN
} Direction;

typedef struct {
	uint8_t x;
	uint8_t y;
	uint32_t dissapearTime;
} SnakeObject;


static int16_t field[SNAKE_W][SNAKE_H];
static Direction direction;
static SnakeObject fruit1, fruit2, head;
static uint32_t lastMovedTick;


static int16_t Reduce(int16_t prev)
{
	if (!prev)
		return 0;
	return prev-1;
}

static void _GenerateRandomFruit()
{
	uint32_t t = IF_GetCurrentTime();
	uint32_t r = IF_Random();
	SnakeObject *s = (r&0x01) ? &fruit1 : &fruit2;
	r >>= 1;
	s->x = 1+(r%(SNAKE_W-2));
	r >>= 5;
	s->y = 1+(r%(SNAKE_H-2));
	r >>= 5;
	if (!field[s->x][s->y])
	{
		s->dissapearTime = t+1.3*ONE_SECOND * (r&0x07);
	}
	else
	{
		s->dissapearTime = 0;
	}
}

static void ChangeDirection(Direction newDirection)
{
	if (newDirection == D_LEFT && field[head.x-1][head.y] == head.dissapearTime-1)
	{
		return;
	}
	if (newDirection == D_RIGHT && field[head.x+1][head.y] == head.dissapearTime-1)
	{
		return;
	}
	if (newDirection == D_UP && field[head.x][head.y-1] == head.dissapearTime-1)
	{
		return;
	}
	if (newDirection == D_DOWN && field[head.x][head.y+1] == head.dissapearTime-1)
	{
		return;
	}
	direction = newDirection;
}

static void MoveSnakeOnce()
{
	switch(direction)
	{
	case D_LEFT:
		head.x -= 1;
		break;
	case D_RIGHT:
		head.x += 1;
		break;
	case D_UP:
		head.y -= 1;
		break;
	case D_DOWN:
		head.y += 1;
		break;
	default:
		break;
	}
	if (head.x == 0 || head.x == SNAKE_W-1 || head.y == 0 || head.y == SNAKE_H-1 || field[head.x][head.y])
	{
		mainMemory._gameState = GS_DONE;
		return;
	}
    uint32_t t = IF_GetCurrentTime();
	if (head.x == fruit1.x && head.y == fruit1.y && t<fruit1.dissapearTime)
	{
		head.dissapearTime++;
		fruit1.dissapearTime = 0;
	}
	if (head.x == fruit2.x && head.y == fruit2.y && t<fruit2.dissapearTime)
	{
		head.dissapearTime++;
		fruit2.dissapearTime = 0;
	}
	else
	{
		for (uint8_t i=0; i<SNAKE_W; i++)
			for (uint8_t j=0; j<SNAKE_H; j++)
			{
				field[i][j] = Reduce(field[i][j]);
			}
	}
	field[head.x][head.y] = head.dissapearTime;
}

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    for (uint8_t i=0; i<SNAKE_W; i++)
    	for (uint8_t j=0; j<SNAKE_H; j++)
    	{
    		field[i][j] = 0;
    	}
    fruit1 = (SnakeObject){0, 0, 0};
    fruit2 = (SnakeObject){0, 0, 0};
    field[SNAKE_W/2][SNAKE_H/2] = 3;
    field[SNAKE_W/2+1][SNAKE_H/2] = 2;
    field[SNAKE_W/2+2][SNAKE_H/2] = 1;
    head = (SnakeObject){SNAKE_W/2, SNAKE_H/2, 3};
    mainMemory._lastRenderedTick = CalculateTick();
    direction = D_LEFT;
    lastMovedTick = 0;
    _GenerateRandomFruit();
}


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)


static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	if (keyboard_held & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}

	// TODO: snake keyboard controls
	if (mainMemory._gameState == GS_DONE)
	{
		if (mainMemory.touchPressed)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
		return;
	}
    uint32_t tick = CalculateTick();
    while (mainMemory._lastRenderedTick < tick)
    {
		KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
    	if ((mainMemory._lastRenderedTick & 0x7f) == 0x40) // new one every 3 second
    	{
    		_GenerateRandomFruit();
    	}

        mainMemory._lastRenderedTick++;
        if (mainMemory._lastRenderedTick - lastMovedTick > 3)
        {
        	MoveSnakeOnce();
        	lastMovedTick = mainMemory._lastRenderedTick;
        }
        if (keyboard != KEY_NONE)
        {
        	if (keyboard & KEY_LEFT)
        		ChangeDirection(D_LEFT);
			else if (keyboard & KEY_UP)
        		ChangeDirection(D_UP);
			else if (keyboard & KEY_RIGHT)
				ChangeDirection(D_RIGHT);
			else if (keyboard & KEY_DOWN)
				ChangeDirection(D_DOWN);
			keyboard = KEY_NONE;
        }
    }
}

#define START_X 0
#define END_X GPU_X
#define BLOCK_W ((END_X-START_X)/SNAKE_W)
#define START_Y 0
#define END_Y GPU_Y
#define BLOCK_H ((END_Y-START_Y)/SNAKE_H)

static void G_Draw(void)
{
    uint32_t t = IF_GetCurrentTime();
    GPU_ClearFramebuffers();
    for (uint8_t i=0; i<11; i++)
    	GPU_DrawEmptySquare(C_YELLOW, i, i, GPU_X-2*i, GPU_Y-2*i);

    for (uint8_t i=0; i<SNAKE_W; i++)
		for (uint8_t j=0; j<SNAKE_H; j++)
		{
			if (field[i][j])
				GPU_DrawFilledSquare(C_WHITE, START_X+i*BLOCK_W, START_Y+j*BLOCK_H, BLOCK_W-1, BLOCK_H-1);
		}
    // Draw fruit1 and 2 also
    if (t < fruit1.dissapearTime)
    {
    	GPU_DrawFilledSquare(C_BLUE, START_X+fruit1.x*BLOCK_W, START_Y+fruit1.y*BLOCK_H, BLOCK_W-1, BLOCK_H-1);
    }
    if (t < fruit2.dissapearTime)
	{
		GPU_DrawFilledSquare(C_RED, START_X+fruit2.x*BLOCK_W, START_Y+fruit2.y*BLOCK_H, BLOCK_W-1, BLOCK_H-1);
	}
    if (mainMemory._gameState == GS_DONE)
    {
    	// Draw reset button
		const char * textToDisplay = "RESET";
		int8_t len = strlen(textToDisplay);
		GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
		GPU_DrawText(C_BLACK, &fontDescription[3], RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G11_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
