// Pong

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <math.h>

typedef enum {
	PS_NOT_INITIALIZED,
	PS_PLAYING,
	PS_PLAYER_SCORE,
	PS_COMPUTER_SCORE
} PongState;

typedef struct {
	float player_y;
	float computer_y;
	float ball_x;
	float ball_y;
	float ball_sin;
	float ball_cos;
	unsigned int score_player;
	unsigned int score_computer;
	PongState pongState;
} Game13Memory;

static Game13Memory * const memory = (Game13Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 35

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static int InitiateBall()
{
	memory->player_y = 0.0f;
	memory->computer_y = 0.0f;
	memory->ball_x=0.0f;
	memory->ball_y=0.0f;
	memory->ball_sin=0.0f;
	memory->ball_cos=1.0f;
	uint32_t seed = IF_Random();
	memory->ball_sin = sin(seed);
	memory->ball_cos = cos(seed);
	if (memory->ball_sin == 1.0f)
		return 1;
	return 0;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
	memory->score_player = 0;
	memory->score_computer = 0;
	memory->pongState = PS_NOT_INITIALIZED;
	mainMemory._lastRenderedTick = CalculateTick();
	InitiateBall();
}

#define EDGE_X (16)
#define EDGE_Y (fontDescription[0].height)
#define BOX_W (GPU_X - EDGE_X*2)
#define BOX_H (GPU_Y - EDGE_Y*2)

#define BALL_SIZE (3)
#define BALL_SPEED (120.0f/NUM_TICKS)
#define PLAYER_W (6)
#define PLAYER_H (36)
#define PLAYER_SPEED (70.0f/NUM_TICKS)



static void CheckPlayerCollision()
{
	if (memory->ball_y-memory->player_y <= PLAYER_H/2 && memory->ball_y-memory->player_y >= -PLAYER_H/2)
	{
		float diff = (memory->ball_y-memory->player_y)/((PLAYER_H+0.1)/2);
		memory->ball_sin = diff;
		memory->ball_cos = sqrt(1-diff*diff);
	}
	else
	{
		memory->score_computer++;
		memory->pongState = PS_COMPUTER_SCORE;
	}
}

static void CheckAICollision()
{
	if (memory->ball_y-memory->computer_y <= PLAYER_H/2 && memory->ball_y-memory->computer_y >= -PLAYER_H/2)
	{
		float diff = (memory->ball_y-memory->computer_y)/((PLAYER_H+0.1)/2);
		memory->ball_sin = diff;
		memory->ball_cos = -sqrt(1-diff*diff);
	}
	else
	{
		memory->score_player++;
		memory->pongState = PS_PLAYER_SCORE;
	}
}

static void MoveBall()
{
	memory->ball_x += memory->ball_cos*BALL_SPEED;
	memory->ball_y += memory->ball_sin*BALL_SPEED;
	if (memory->ball_x < -(100.0f-BALL_SIZE/2))
	{
		CheckPlayerCollision();
	}
	else if (memory->ball_x > (100.0f-BALL_SIZE/2))
	{
		CheckAICollision();
	}
	if (memory->ball_y < -100.0f)
	{
		memory->ball_y = -100.f;
		memory->ball_sin *= -1;
	}
	else if (memory->ball_y > 100.0f)
	{
		memory->ball_y = 100.f;
		memory->ball_sin *= -1;
	}
}

static void MoveAI()
{
	if (memory->computer_y - 2.8f*PLAYER_SPEED>memory->ball_y)
	{
		memory->computer_y -= PLAYER_SPEED;
		if (memory->computer_y < -(100.0f-PLAYER_H/2))
			memory->computer_y = -(100.0f-PLAYER_H/2);
	}
	else if (memory->computer_y + 2.8f*PLAYER_SPEED<memory->ball_y)
	{
		memory->computer_y += PLAYER_SPEED;
		if (memory->computer_y > (100.0f-PLAYER_H/2))
			memory->computer_y = (100.0f-PLAYER_H/2);
	}
}

static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	uint32_t current_tick = CalculateTick();
	while (mainMemory._lastRenderedTick < current_tick)
	{
		if (memory->pongState == PS_PLAYING)
		{
			if (keyboard_held & KEY_UP)
			{
				memory->player_y -= PLAYER_SPEED;
				if (memory->player_y < -(100.0f-PLAYER_H/2))
					memory->player_y = -(100.0f-PLAYER_H/2);
			}
			if (keyboard_held & KEY_DOWN)
			{
				memory->player_y += PLAYER_SPEED;
				if (memory->player_y > (100.0f-PLAYER_H/2))
					memory->player_y = (100.0f-PLAYER_H/2);
			}
			MoveBall();
			MoveAI();
		}
		else
		{
			if (keyboard_held & (KEY_DOWN | KEY_UP))
			{
				memory->pongState = PS_PLAYING;
				while (InitiateBall());
			}
		}
		mainMemory._lastRenderedTick++;
	}
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
    GPU_ClearFramebuffers();
	// Draw Field
	GPU_DrawEmptySquare(C_BLUE, EDGE_X, EDGE_Y, BOX_W, BOX_H);
	GPU_DrawLine(C_RED, EDGE_X + BOX_W/2, EDGE_Y, EDGE_X + BOX_W/2, GPU_Y - EDGE_Y);
	// Draw Player
	GPU_DrawFilledSquare(C_WHITE, EDGE_X-PLAYER_W/2, EDGE_Y+(memory->player_y+100)/200*BOX_H-PLAYER_H/2, PLAYER_W, PLAYER_H);
	// Draw Computer
	GPU_DrawFilledSquare(C_WHITE, GPU_X-EDGE_X-PLAYER_W/2, EDGE_Y+(memory->computer_y+100)/200*BOX_H-PLAYER_H/2, PLAYER_W, PLAYER_H);
	// Draw Ball
	GPU_DrawFilledCircle(C_YELLOW, ((memory->ball_x+100)/200)*BOX_W+EDGE_X, ((memory->ball_y+100)/200)*BOX_H+EDGE_Y, BALL_SIZE );
	// Draw Score
	char text[32];
	int len;
	len = sprintf(text, "PLAYER: %d", memory->score_player);
	GPU_DrawText(C_C1, &fontDescription[0], 0, 0, text, len);
	len = sprintf(text, "AI: %d", memory->score_computer);
	GPU_DrawText(C_C2, &fontDescription[0], GPU_X-len*fontDescription[0].width, 0, text, len);

	if (memory->pongState == PS_NOT_INITIALIZED)
	{
		len = sprintf(text, "PRESS KEY TO START");
		GPU_DrawText(C_C3, f_big, GPU_X/2-len*f_big->width/2, GPU_Y/2-f_big->height/2, text, len);
	}
	else if (memory->pongState == PS_PLAYER_SCORE)
	{
		len = sprintf(text, "PLAYER SCORES");
		GPU_DrawText(C_C3, f_big, GPU_X/2-len*f_big->width/2, GPU_Y/2-f_big->height/2, text, len);
	}
	else if (memory->pongState == PS_COMPUTER_SCORE)
	{
		len = sprintf(text, "AI SCORES");
		GPU_DrawText(C_C3, f_big, GPU_X/2-len*f_big->width/2, GPU_Y/2-f_big->height/2, text, len);
	}
}

static void G_Deinit(void)
{

}

const Callbacks G13_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
