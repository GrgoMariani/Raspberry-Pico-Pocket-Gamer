// Snake

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>

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
/* up to two pending turns: a quick "up then left" used to lose the first turn
   because only the last key of a tick was applied (and a reversal is rejected),
   which sent the snake straight into the wall it was trying to avoid */
static Direction dirQueue[2];
static uint8_t dirQueueN = 0;
static SnakeObject fruit1, fruit2, head;
static SnakeObject fruitGold;      /* rare bonus fruit, worth +3 */
static uint32_t goldNextSpawn;
static uint32_t lastMovedTick;
/* the record lives outside of the shared memory so that it survives a visit
   to another game */
static uint16_t bestLength = 0;


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
	/* never on the snake, the other fruit or the golden fruit: two fruits on
	   one cell used to overlap visually and both were eaten in a single step */
	const SnakeObject * other = (s == &fruit1) ? &fruit2 : &fruit1;
	uint8_t occupied = (uint8_t)(field[s->x][s->y] != 0 ||
		(other->dissapearTime && s->x == other->x && s->y == other->y) ||
		(fruitGold.dissapearTime && s->x == fruitGold.x && s->y == fruitGold.y));
	if (!occupied)
	{
		/* 1.3s .. 10.4s; without the +1 every eighth fruit vanished instantly */
		s->dissapearTime = t + (uint32_t)(1.3*ONE_SECOND) * (1 + (r&0x07));
	}
	else
	{
		s->dissapearTime = 0;
	}
}

/* the golden fruit appears on its own schedule and is worth three segments */
static void _SpawnGoldFruit()
{
	uint32_t t = IF_GetCurrentTime();
	for (uint8_t attempt = 0; attempt < 20; attempt++)
	{
		uint8_t x = (uint8_t)(1 + Random_Range(SNAKE_W-2));
		uint8_t y = (uint8_t)(1 + Random_Range(SNAKE_H-2));
		if (!field[x][y] && !(x==head.x && y==head.y) &&
			!(fruit1.dissapearTime && x==fruit1.x && y==fruit1.y) &&
			!(fruit2.dissapearTime && x==fruit2.x && y==fruit2.y))
		{
			fruitGold.x = x;
			fruitGold.y = y;
			fruitGold.dissapearTime = t + 6*ONE_SECOND;
			return;
		}
	}
	fruitGold.dissapearTime = 0;
}

static void ChangeDirection(Direction newDirection)
{
	/* the cell the head came from holds head.dissapearTime-1: turning into it
	   would mean reversing into the own body */
	int16_t neck = (int16_t)(head.dissapearTime - 1);
	if (newDirection == D_LEFT && head.x > 0 && field[head.x-1][head.y] == neck)
		return;
	if (newDirection == D_RIGHT && head.x+1 < SNAKE_W && field[head.x+1][head.y] == neck)
		return;
	if (newDirection == D_UP && head.y > 0 && field[head.x][head.y-1] == neck)
		return;
	if (newDirection == D_DOWN && head.y+1 < SNAKE_H && field[head.x][head.y+1] == neck)
		return;
	direction = newDirection;
}

/* buffer a turn; it is applied one per snake step, in order */
static void QueueDirection(Direction d)
{
	Direction last = dirQueueN ? dirQueue[dirQueueN-1] : direction;
	if (d == last)
		return;
	if (dirQueueN < 2)
		dirQueue[dirQueueN++] = d;
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
	uint8_t grew = 0;
	if (head.x == fruit1.x && head.y == fruit1.y && t<fruit1.dissapearTime)
	{
		head.dissapearTime++;
		fruit1.dissapearTime = 0;
		grew = 1;
	}
	if (head.x == fruit2.x && head.y == fruit2.y && t<fruit2.dissapearTime)
	{
		head.dissapearTime++;
		fruit2.dissapearTime = 0;
		grew = 1;
	}
	if (head.x == fruitGold.x && head.y == fruitGold.y && t<fruitGold.dissapearTime)
	{
		head.dissapearTime += 3;
		fruitGold.dissapearTime = 0;
		grew = 1;
	}
	if (!grew)
	{
		for (uint8_t i=0; i<SNAKE_W; i++)
			for (uint8_t j=0; j<SNAKE_H; j++)
			{
				field[i][j] = Reduce(field[i][j]);
			}
	}
	field[head.x][head.y] = head.dissapearTime;
	/* remember the record as soon as it grows, so a MENU reset does not lose it */
	if (grew && head.dissapearTime > bestLength)
		bestLength = (uint16_t)head.dissapearTime;
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
    fruitGold = (SnakeObject){0, 0, 0};
    goldNextSpawn = IF_GetCurrentTime() + 10*ONE_SECOND;
    field[SNAKE_W/2][SNAKE_H/2] = 3;
    field[SNAKE_W/2+1][SNAKE_H/2] = 2;
    field[SNAKE_W/2+2][SNAKE_H/2] = 1;
    head = (SnakeObject){SNAKE_W/2, SNAKE_H/2, 3};
    mainMemory._lastRenderedTick = CalculateTick();
    direction = D_LEFT;
    dirQueueN = 0;
    lastMovedTick = 0;
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
    _GenerateRandomFruit();
}


#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)


#define START_X 0
#define END_X GPU_X
#define BLOCK_W ((END_X-START_X)/SNAKE_W)
#define START_Y 0
#define END_Y GPU_Y
#define BLOCK_H ((END_Y-START_Y)/SNAKE_H)

/* tap into the direction the snake should turn */
static void TouchSteer(uint16_t touchX, uint16_t touchY)
{
	int32_t hx = START_X + head.x*BLOCK_W + BLOCK_W/2;
	int32_t hy = START_Y + head.y*BLOCK_H + BLOCK_H/2;
	int32_t dx = (int32_t)touchX - hx;
	int32_t dy = (int32_t)touchY - hy;
	if (dx*dx + dy*dy < 64)
		return;
	if ((dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx))
		QueueDirection((dy < 0) ? D_UP : D_DOWN);
	else
		QueueDirection((dx < 0) ? D_LEFT : D_RIGHT);
}

/* the snake speeds up as it grows: 7.5 cells/s at the start, up to 15 */
static uint32_t MoveInterval(void)
{
	uint32_t len = head.dissapearTime;
	uint32_t faster = (len > 3) ? (len-3)/8 : 0;
	if (faster > 2)
		faster = 2;
	return 4 - faster;
}

static void G_Update(void)
{
	if (mainMemory._gameState == GS_DONE)
	{
		if (mainMemory.touchPressedNew)
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
    	{
    		uint32_t now = IF_GetCurrentTime();
    		if (fruitGold.dissapearTime == 0 && now >= goldNextSpawn)
    		{
    			_SpawnGoldFruit();
    			goldNextSpawn = now + 20*ONE_SECOND;
    		}
    	}

        mainMemory._lastRenderedTick++;
        if (mainMemory._lastRenderedTick - lastMovedTick >= MoveInterval())
        {
        	if (dirQueueN)
        	{
        		ChangeDirection(dirQueue[0]);
        		dirQueue[0] = dirQueue[1];
        		dirQueueN--;
        	}
        	MoveSnakeOnce();
        	lastMovedTick = mainMemory._lastRenderedTick;
        	if (mainMemory._gameState == GS_DONE)
        	{
        		if (head.dissapearTime > bestLength)
        			bestLength = (uint16_t)head.dissapearTime;
        		return;
        	}
        }
        if (keyboard != KEY_NONE)
        {
        	if (keyboard & KEY_LEFT)
        		QueueDirection(D_LEFT);
			else if (keyboard & KEY_UP)
        		QueueDirection(D_UP);
			else if (keyboard & KEY_RIGHT)
				QueueDirection(D_RIGHT);
			else if (keyboard & KEY_DOWN)
				QueueDirection(D_DOWN);
			keyboard = KEY_NONE;
        }
        /* the tap latch delivers every press, even between two ticks */
        uint16_t tx, ty;
        if (Touch_TakeTap(&tx, &ty))
        	TouchSteer(tx, ty);
    }
}

static void DrawFruit(const SnakeObject * fruit, GPU_Color color, uint32_t t)
{
	if (t >= fruit->dissapearTime)
		return;
	int16_t cx = (int16_t)(START_X + fruit->x*BLOCK_W + BLOCK_W/2);
	int16_t cy = (int16_t)(START_Y + fruit->y*BLOCK_H + BLOCK_H/2);
	/* blink shortly before it disappears */
	if (fruit->dissapearTime - t < ONE_SECOND/2 && (t/(ONE_SECOND/8)) & 1)
		color = GPU_ShadeColor(color, 140);
	GPU_DrawGlow(color, cx, cy, (int16_t)(BLOCK_W/2+2));
	GPU_DrawFilledCircle(color, cx, cy, (int16_t)(BLOCK_W/2-1));
	GPU_DrawPixel(RGB(255,255,255), (uint16_t)(cx-2), (uint16_t)(cy-2));
}

static void G_Draw(void)
{
    const FontDescription * fm = &fontDescription[1];
    const FontDescription * fb = &fontDescription[2];
    uint32_t t = IF_GetCurrentTime();
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(6,20,10), C_BLACK, 0, 0, GPU_X, GPU_Y);
    GPU_DrawPanel(RGB(10,30,16), C_GREEN, 6, 6, GPU_X-12, GPU_Y-12);

    uint16_t length = 0;
    for (uint8_t i=0; i<SNAKE_W; i++)
		for (uint8_t j=0; j<SNAKE_H; j++)
		{
			if (field[i][j])
			{
				length++;
				GPU_DrawBevelSquare(C_GREEN, (int16_t)(START_X+i*BLOCK_W+1), (int16_t)(START_Y+j*BLOCK_H+1),
				                    (int16_t)(BLOCK_W-2), (int16_t)(BLOCK_H-2));
			}
		}
    /* the head gets eyes */
    {
        int16_t hx = (int16_t)(START_X + head.x*BLOCK_W + 1);
        int16_t hy = (int16_t)(START_Y + head.y*BLOCK_H + 1);
        GPU_DrawBevelSquare(C_C3, hx, hy, (int16_t)(BLOCK_W-2), (int16_t)(BLOCK_H-2));
        GPU_DrawPixel(C_BLACK, (uint16_t)(hx+2), (uint16_t)(hy+2));
        GPU_DrawPixel(C_BLACK, (uint16_t)(hx+BLOCK_W-4), (uint16_t)(hy+2));
    }
    DrawFruit(&fruit1, C_BLUE, t);
    DrawFruit(&fruit2, C_RED, t);
    DrawFruit(&fruitGold, C_YELLOW, t);

    /* score */
    char text[24];
    sprintf(text, "LENGTH %u", length);
    GPU_DrawText(C_WHITE, fm, 12, 10, text, (uint16_t)strlen(text));
    if (bestLength)
    {
        sprintf(text, "BEST %u", (unsigned)bestLength);
        GPU_DrawText(C_GRAY_MEDIUM, fm, (int16_t)(GPU_X - 12 - strlen(text)*fm->width), 10, text, (uint16_t)strlen(text));
    }

    if (mainMemory._gameState == GS_DONE)
    {
    	GPU_DrawPanel(RGB(16,18,48), C_RED, (int16_t)(GPU_X/2-80), (int16_t)(GPU_Y/2-44), 160, 88);
    	GPU_DrawTextOutlined(C_RED, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-38), "GAME OVER");
		sprintf(text, "LENGTH %u", length);
		GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-12), text);
		GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
		               RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G11_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
