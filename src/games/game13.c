// Pong

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

typedef struct {
	float player_y;
	float computer_y;
	float ball_x;
	float ball_y;
	float ball_sin;
	float ball_cos;
	int score_player;
	int score_computer;
} Game13Memory;

static Game13Memory * const memory = (Game13Memory*)(&mainMemory.sharedMemory.memblocks[0]);


#define MAX_NUMS 8

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
}


#define BOX_W ((GPU_X/2)/4)
#define BOX_H (GPU_Y/4)

static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	if (keyboard_held & KEY_MENU)
	{
		SetupCallbacks(&MM_Callbacks);
		return;
	}
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
    GPU_ClearFramebuffers();
}

static void G_Deinit(void)
{

}

const Callbacks G13_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
