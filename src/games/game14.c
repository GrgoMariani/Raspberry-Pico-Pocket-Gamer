// Breakout

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <math.h>

typedef enum {
	BS_NOT_INITIALIZED,
	BS_PLAYING,
	BS_GAME_OVER,
	BS_GAME_WON
} BreakoutState;

typedef struct {
	uint8_t lives;
} Block;

typedef enum {
	REFLECT_NONE = 0,
	REFLECT_COS = 1,
	REFLECT_SIN = 2
} ReflectEnum;

typedef struct {
	float player_x;
	float ball_x;
	float ball_y;
	float ball_sin;
	float ball_cos;
	BreakoutState breakoutState;
	Block blocks[6][4];
} Game14Memory;

static Game14Memory * const memory = (Game14Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 20

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static int InitiateBall()
{
	memory->ball_x = 0.0f;
	memory->ball_y = 100.0f;
	memory->ball_sin = 0.0f;
	memory->ball_cos = 1.0f;
	uint32_t seed = IF_Random();
	memory->ball_sin = sin(seed);
	memory->ball_cos = cos(seed);
	if (memory->ball_cos == 1.0f)
		return 1;
	if (memory->ball_sin > 0)
		memory->ball_sin *= -1.0f;
	return 0;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
	memory->breakoutState = BS_NOT_INITIALIZED;
	memory->player_x = 0.0f;
	mainMemory._lastRenderedTick = CalculateTick();
	while (InitiateBall());
	for (uint8_t i=0; i<6; i++)
		for (uint8_t j=0; j<4; j++)
		{
			memory->blocks[i][j].lives = 4;
		}
}

#define EDGE_X (40)
#define EDGE_Y (20)
#define BOX_W (GPU_X - EDGE_X*2)
#define BOX_H (GPU_Y - EDGE_Y*2)

#define BLOCK_V_W (31.0f)
#define BLOCK_V_H (16.0f)
#define BLOCK_V_SEPARATOR_W (1.0f)
#define BLOCK_V_SEPARATOR_H (2.0f)
#define BLOCK_V_EDGE_X (4.0f)
#define BLOCK_V_EDGE_Y (30.0f)
#define PLAYER_V_W (31.0f)

#define BALL_SIZE_V (2.0f)
#define BALL_SIZE (BALL_SIZE_V/200.0f*BOX_H)
#define BALL_SPEED (120.0f/NUM_TICKS)

#define PLAYER_W (PLAYER_V_W/200.0f*BOX_W)
#define PLAYER_H (6)
#define PLAYER_SPEED (90.0f/NUM_TICKS)

#define BLOCK_W (BLOCK_V_W/200.0f*BOX_W)
#define BLOCK_H (BLOCK_V_H/200.0f*BOX_H)


static uint8_t CheckWallCollisions()
{
	if (memory->ball_x < -100.0f)
	{
		memory->ball_x = -100.0f;
		memory->ball_cos *= -1.0f;
	}
	else if (memory->ball_x > 100.0f)
	{
		memory->ball_x = 100.0f;
		memory->ball_cos *= -1.0f;
	}
	if (memory->ball_y < -100.0f)
	{
		memory->ball_y = -100.0f;
		memory->ball_sin *= -1.0f;
	}
	else if (memory->ball_y > 100.0f)
	{
		if (memory->player_x-PLAYER_V_W/2<memory->ball_x && memory->player_x+PLAYER_V_W/2 > memory->ball_x)
		{
			float diff = (memory->ball_x-memory->player_x)/((PLAYER_V_W+0.1)/2);
			memory->ball_cos = diff;
			memory->ball_sin = -sqrt(1-diff*diff);
		}
		else
		{
			memory->breakoutState = BS_GAME_OVER;
			return 1;
		}
	}
	return 0;
}

static uint8_t _intersects(float rect_x, float rect_y)
{
    float circleDistancex = memory->ball_x - rect_x;
    float circleDistancey = memory->ball_y - rect_y;
	circleDistancex = circleDistancex > 0 ? circleDistancex : -circleDistancex;
	circleDistancey = circleDistancey > 0 ? circleDistancey : -circleDistancey;

    if (circleDistancex > (BLOCK_V_W/2 + BALL_SIZE)) { return 0; }
    if (circleDistancey > (BLOCK_V_H/2 + BALL_SIZE)) { return 0; }

    if (circleDistancex <= (BLOCK_V_W/2)) { return REFLECT_SIN; } 
    if (circleDistancey <= (BLOCK_V_H/2)) { return REFLECT_COS; }

    float cornerDistance_sq = pow(circleDistancex - BLOCK_V_W/2, 2) +
                         pow(circleDistancey - BLOCK_V_H/2, 2);

	if (cornerDistance_sq <= pow(BALL_SIZE, 2))
		return REFLECT_COS | REFLECT_SIN;
	return 0;
}


static int CheckBlockCollisions()
{
	uint8_t intersects_block;
	float x, y;
	for (uint8_t i=0; i<6; i++)
		for (uint8_t j=0; j<4; j++)
		{
			if (!memory->blocks[i][j].lives)
				continue;
			x = -100.0f+BLOCK_V_EDGE_X + BLOCK_V_W/2 + i*(BLOCK_V_W + BLOCK_V_SEPARATOR_W);
			y = -100.0f+BLOCK_V_EDGE_Y + BLOCK_V_H/2 + j*(BLOCK_V_H + BLOCK_V_SEPARATOR_H);
			intersects_block = _intersects(x, y);
			if (intersects_block)
			{
				memory->blocks[i][j].lives--;
				if (intersects_block & REFLECT_COS)
					memory->ball_cos *= -1.0f;
				if (intersects_block & REFLECT_SIN)
					memory->ball_sin *= -1.0f;
				return 1;
			}
		}
	return 0;
}

static int CheckGameWon()
{
	for (uint8_t i=0; i<6; i++)
		for (uint8_t j=0; j<4; j++)
		{
			if (memory->blocks[i][j].lives)
			{
				return 0;
			}
		}
	memory->breakoutState = BS_GAME_WON;
	return 1;
}

static void MoveBall()
{
	memory->ball_x += memory->ball_cos*BALL_SPEED;
	memory->ball_y += memory->ball_sin*BALL_SPEED;
}

static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	uint32_t current_tick = CalculateTick();
	while (mainMemory._lastRenderedTick < current_tick)
	{
		if (memory->breakoutState == BS_PLAYING)
		{
			if (keyboard_held & KEY_LEFT)
			{
				memory->player_x -= PLAYER_SPEED;
				if (memory->player_x < -(100.0f-PLAYER_V_W/2))
					memory->player_x = -(100.0f-PLAYER_V_W/2);
			}
			if (keyboard_held & KEY_RIGHT)
			{
				memory->player_x += PLAYER_SPEED;
				if (memory->player_x > (100.0f-PLAYER_V_W/2))
					memory->player_x = (100.0f-PLAYER_V_W/2);
			}
			MoveBall();
			CheckWallCollisions();
			CheckBlockCollisions();
			CheckGameWon();
		}
		else if (keyboard_held & (KEY_LEFT | KEY_RIGHT))
		{
			G_Init();
			memory->breakoutState = BS_PLAYING;
		}
		// Update state
		mainMemory._lastRenderedTick++;
	}
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[2];
    GPU_ClearFramebuffers();
	// Draw Field
	GPU_DrawEmptySquare(C_C2, EDGE_X, EDGE_Y, BOX_W, BOX_H);
	// Draw Player
	GPU_DrawFilledSquare(C_C1, EDGE_X+(memory->player_x+100.0f)/200.0f*BOX_W-PLAYER_W/2, GPU_Y-EDGE_Y-PLAYER_H/2, PLAYER_W, PLAYER_H);
	// Draw Blocks
	
	for (uint8_t i=0; i<6; i++)
		for (uint8_t j=0; j<4; j++)
		{
			Block * active_block = &memory->blocks[i][j];
			if (memory->blocks[i][j].lives)
			{
				GPU_Color color;
				switch (memory->blocks[i][j].lives)
				{
				case 1: color = C_GRAY_DARK;
					break;
				case 2: color = C_GRAY_MEDIUM;
					break;
				case 3: color = C_GRAY_LIGHT;
					break;
				default:
					color = C_WHITE;
					break;
				}
				GPU_DrawFilledSquare(color, EDGE_X + (BLOCK_V_EDGE_X+(BLOCK_V_W+BLOCK_V_SEPARATOR_W)*i)/200.0f*BOX_W, EDGE_Y+(BLOCK_V_EDGE_Y+(BLOCK_V_H+BLOCK_V_SEPARATOR_H)*j)/200.0f*BOX_H, BLOCK_W, BLOCK_H);
			}
		}
	// Draw Ball
	GPU_DrawFilledCircle(C_C3, EDGE_X + (memory->ball_x+100.0f)/200.0f*BOX_W, EDGE_Y + (memory->ball_y+100.0f)/200.0f*BOX_H, BALL_SIZE);
	// Draw Text
	if (memory->breakoutState != BS_PLAYING)
	{
		char text[32];
		int len;
		switch (memory->breakoutState)
		{
			case BS_GAME_OVER:
				len = sprintf(text, "YOU LOSE");
				break;
			case BS_GAME_WON:
				len = sprintf(text, "YOU WIN");
				break;
			default:
				len = sprintf(text, "PRESS ANY KEY TO START");
				break;
		}
		GPU_DrawText(C_GREEN, f_big, GPU_X/2-len*f_big->width/2, GPU_Y/2-f_big->height/2, text, len);
	}
}

static void G_Deinit(void)
{

}

const Callbacks G14_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
