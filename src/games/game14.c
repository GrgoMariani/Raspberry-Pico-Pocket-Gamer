// Breakout

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <math.h>

typedef enum {
	BS_NOT_INITIALIZED,
	BS_PLAYING
} BreakoutState;

typedef struct {
	float player_x;
	float ball_x;
	float ball_y;
	float ball_sin;
	float ball_cos;
	BreakoutState breakoutState;
} Game14Memory;

static Game14Memory * const memory = (Game14Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 35

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static int InitiateBall()
{
	memory->player_x = 0.0f;
	memory->ball_x=0.0f;
	memory->ball_y=0.0f;
	memory->ball_sin=0.0f;
	memory->ball_cos=1.0f;
	uint32_t seed = IF_Random();
	memory->ball_sin = sin(seed);
	memory->ball_cos = cos(seed);
	if (memory->ball_cos == 1.0f)
		return 1;
	return 0;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
	memory->breakoutState = BS_NOT_INITIALIZED;
	mainMemory._lastRenderedTick = CalculateTick();
	InitiateBall();
}

#define EDGE_X (16)
#define EDGE_Y (fontDescription[0].height)
#define BOX_W (GPU_X - EDGE_X*2)
#define BOX_H (GPU_Y - EDGE_Y*2)

#define BALL_SIZE (3)
#define BALL_SPEED (120.0f/NUM_TICKS)
#define PLAYER_W (36)
#define PLAYER_H (6)
#define PLAYER_SPEED (70.0f/NUM_TICKS)


static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	uint32_t current_tick = CalculateTick();
	while (mainMemory._lastRenderedTick < current_tick)
	{
		// Update state
		mainMemory._lastRenderedTick++;
	}
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
    GPU_ClearFramebuffers();
	// Draw Field
	// Draw Player
	// Draw Blocks
	// Draw Ball
	// Draw Score
	// Draw Text
}

static void G_Deinit(void)
{

}

const Callbacks G14_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
