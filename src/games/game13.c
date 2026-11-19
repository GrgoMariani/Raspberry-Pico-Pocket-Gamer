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
	PS_COMPUTER_SCORE,
	PS_PLAYER_WINS,
	PS_COMPUTER_WINS
} PongState;

typedef struct {
	float player_y;
	float computer_y;
	float ball_x;
	float ball_y;
	float ball_sin;
	float ball_cos;
	float ballSpeedMul;      /* the rally gets faster with every return */
	float trail_x[6];
	float trail_y[6];
	uint8_t trailIdx;
	uint8_t flashP, flashC;   /* ticks of paddle hit flash */
	uint16_t rally;           /* returns in the current rally */
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

#define WIN_SCORE 11

/* serves the ball in a random direction, but never flatter than ~40 degrees
   so that the rally cannot get stuck bouncing between the side walls */
static void ServeBall()
{
	memory->ball_x=0.0f;
	memory->ball_y=0.0f;
	memory->ballSpeedMul = 1.0f;
	for (uint8_t i = 0; i < 6; i++)
	{
		memory->trail_x[i] = 0.0f;
		memory->trail_y[i] = 0.0f;
	}
	memory->trailIdx = 0;
	memory->rally = 0;
	int32_t angleDeg = (int32_t)Random_Range(81) - 40;      /* -40 .. +40 */
	float rad = (float)angleDeg * 3.14159265f/180.0f;
	memory->ball_sin = sinf(rad);
	memory->ball_cos = cosf(rad);
	if (Random_Range(2))
		memory->ball_cos = -memory->ball_cos;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
	memory->score_player = 0;
	memory->score_computer = 0;
	memory->player_y = 0.0f;
	memory->computer_y = 0.0f;
	memory->flashP = 0;
	memory->flashC = 0;
	memory->pongState = PS_NOT_INITIALIZED;
	mainMemory._lastRenderedTick = CalculateTick();
	Keyboard_GetPressedKeys();
	Touch_ClearTap();
	ServeBall();
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



static void SpeedUpRally()
{
	if (memory->ballSpeedMul < 1.8f)
		memory->ballSpeedMul += 0.06f;
}

static void CheckPlayerCollision()
{
	if (memory->ball_y-memory->player_y <= PLAYER_H/2 && memory->ball_y-memory->player_y >= -PLAYER_H/2)
	{
		float diff = (memory->ball_y-memory->player_y)/((PLAYER_H+0.1)/2);
		memory->ball_sin = diff;
		memory->ball_cos = sqrt(1-diff*diff);
		/* put the ball back in front of the paddle so it cannot be hit
		   twice while it slowly leaves the contact zone */
		memory->ball_x = -(100.0f-BALL_SIZE/2);
		memory->flashP = 4;
		memory->rally++;
		SpeedUpRally();
	}
	else
	{
		memory->score_computer++;
		if (memory->score_computer >= WIN_SCORE)
		{
			memory->pongState = PS_COMPUTER_WINS;
			mainMemory._gameState = GS_DONE;
		}
		else
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
		memory->ball_x = (100.0f-BALL_SIZE/2);
		memory->flashC = 4;
		memory->rally++;
		SpeedUpRally();
	}
	else
	{
		memory->score_player++;
		if (memory->score_player >= WIN_SCORE)
		{
			memory->pongState = PS_PLAYER_WINS;
			mainMemory._gameState = GS_DONE;
		}
		else
			memory->pongState = PS_PLAYER_SCORE;
	}
}

static void MoveBall()
{
	/* remember the recent path for a motion trail */
	memory->trail_x[memory->trailIdx] = memory->ball_x;
	memory->trail_y[memory->trailIdx] = memory->ball_y;
	memory->trailIdx = (uint8_t)((memory->trailIdx + 1) % 6);
	memory->ball_x += memory->ball_cos*BALL_SPEED*memory->ballSpeedMul;
	memory->ball_y += memory->ball_sin*BALL_SPEED*memory->ballSpeedMul;
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
	/* rubber-band difficulty: the AI keeps up when the player is winning and
	   eases off when the player is behind, so matches stay close */
	float aiSpeed = PLAYER_SPEED;
	int diff = (int)memory->score_player - (int)memory->score_computer;
	aiSpeed += (float)diff * PLAYER_SPEED * 0.06f;
	if (aiSpeed < PLAYER_SPEED*0.6f) aiSpeed = PLAYER_SPEED*0.6f;
	if (aiSpeed > PLAYER_SPEED*1.3f) aiSpeed = PLAYER_SPEED*1.3f;
	if (memory->computer_y - 2.8f*aiSpeed>memory->ball_y)
	{
		memory->computer_y -= aiSpeed;
		if (memory->computer_y < -(100.0f-PLAYER_H/2))
			memory->computer_y = -(100.0f-PLAYER_H/2);
	}
	else if (memory->computer_y + 2.8f*aiSpeed<memory->ball_y)
	{
		memory->computer_y += aiSpeed;
		if (memory->computer_y > (100.0f-PLAYER_H/2))
			memory->computer_y = (100.0f-PLAYER_H/2);
	}
}

#define RESET_BUTTON_W (80)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 30)

/* dragging a finger moves the paddle to the finger's height */
static void TouchPaddle(void)
{
	float y = ((float)mainMemory.touch_Y - EDGE_Y)/BOX_H*200.0f - 100.0f;
	if (y < -(100.0f-PLAYER_H/2)) y = -(100.0f-PLAYER_H/2);
	if (y > (100.0f-PLAYER_H/2)) y = (100.0f-PLAYER_H/2);
	memory->player_y = y;
}

static void G_Update(void)
{
	if (mainMemory._gameState == GS_DONE)
	{
		if (mainMemory.touchPressedNew)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W &&
			    mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
		return;
	}
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	uint32_t current_tick = CalculateTick();
	while (mainMemory._lastRenderedTick < current_tick)
	{
		if (memory->pongState == PS_PLAYING)
		{
			if (memory->flashP) memory->flashP--;
			if (memory->flashC) memory->flashC--;
			if (mainMemory.touchPressed)
				TouchPaddle();
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
			uint16_t tx, ty;
			uint8_t tap = Touch_TakeTap(&tx, &ty);
			if ((keyboard_held & (KEY_DOWN | KEY_UP)) || tap)
			{
				memory->pongState = PS_PLAYING;
				ServeBall();
			}
		}
		mainMemory._lastRenderedTick++;
	}
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
	const FontDescription * fm = &fontDescription[1];
    GPU_ClearFramebuffers();
	GPU_DrawVerticalGradient(RGB(6,8,28), C_BLACK, 0, 0, GPU_X, GPU_Y);
	// Draw Field
	GPU_DrawPanel(RGB(10,14,40), C_BLUE, EDGE_X, EDGE_Y, BOX_W, BOX_H);
	for (int16_t y = EDGE_Y+6; y < GPU_Y-EDGE_Y; y += 12)
		GPU_DrawFilledSquare(C_RED, (int16_t)(EDGE_X + BOX_W/2 - 1), y, 2, 6);
	// Draw Player (flashes yellow for a moment when it returns the ball)
	{
		int16_t py = (int16_t)(EDGE_Y+(memory->player_y+100)/200*BOX_H-PLAYER_H/2);
		if (memory->flashP)
			GPU_DrawGlow(C_YELLOW, EDGE_X, (int16_t)(py+PLAYER_H/2), 14);
		GPU_DrawBevelSquare(memory->flashP ? C_YELLOW : C_WHITE, (int16_t)(EDGE_X-PLAYER_W/2), py, PLAYER_W, PLAYER_H);
	}
	// Draw Computer
	{
		int16_t cy = (int16_t)(EDGE_Y+(memory->computer_y+100)/200*BOX_H-PLAYER_H/2);
		if (memory->flashC)
			GPU_DrawGlow(C_YELLOW, (int16_t)(GPU_X-EDGE_X), (int16_t)(cy+PLAYER_H/2), 14);
		GPU_DrawBevelSquare(memory->flashC ? C_YELLOW : C_WHITE, (int16_t)(GPU_X-EDGE_X-PLAYER_W/2), cy, PLAYER_W, PLAYER_H);
	}
	// Draw Ball
	{
		/* motion trail */
		for (uint8_t i = 0; i < 6; i++)
		{
			uint8_t idx = (uint8_t)((memory->trailIdx + i) % 6);
			int16_t tx = (int16_t)(((memory->trail_x[idx]+100)/200)*BOX_W+EDGE_X);
			int16_t ty = (int16_t)(((memory->trail_y[idx]+100)/200)*BOX_H+EDGE_Y);
			GPU_DrawFilledCircle(GPU_ShadeColor(C_YELLOW, (int16_t)(40 + i*20)), tx, ty, 1);
		}
		int16_t bx = (int16_t)(((memory->ball_x+100)/200)*BOX_W+EDGE_X);
		int16_t by = (int16_t)(((memory->ball_y+100)/200)*BOX_H+EDGE_Y);
		GPU_DrawGlow(C_YELLOW, bx, by, (int16_t)(BALL_SIZE+4));
		GPU_DrawBall(C_YELLOW, bx, by, BALL_SIZE);
	}
	// Draw Score
	char text[32];
	int len;
	len = sprintf(text, "PLAYER %u", memory->score_player);
	GPU_DrawText(C_C1, fm, 4, 2, text, (uint16_t)len);
	len = sprintf(text, "AI %u", memory->score_computer);
	GPU_DrawText(C_C2, fm, (int16_t)(GPU_X-len*fm->width-4), 2, text, (uint16_t)len);
	if (memory->pongState == PS_PLAYING && memory->rally >= 3)
	{
		/* the rally heats up: show the streak and how fast the ball is now */
		len = sprintf(text, "RALLY %u  SPEED %u%%", memory->rally,
		              (unsigned)(memory->ballSpeedMul*100.0f));
		GPU_DrawTextCentered(memory->rally >= 8 ? C_ORANGE : C_GRAY_MEDIUM, &fontDescription[0],
		                     GPU_X/2, (int16_t)(GPU_Y-8), text);
	}

	if (memory->pongState == PS_NOT_INITIALIZED)
	{
		GPU_DrawTextOutlined(C_C3, C_BLACK, f_big, GPU_X/2, (int16_t)(GPU_Y/2-f_big->height), "PRESS A KEY");
		GPU_DrawTextCentered(C_GRAY_LIGHT, fm, GPU_X/2, (int16_t)(GPU_Y/2+10), "up/down or drag, first to 11");
	}
	else if (memory->pongState == PS_PLAYER_SCORE)
	{
		GPU_DrawTextOutlined(C_C3, C_BLACK, f_big, GPU_X/2, GPU_Y/2-f_big->height/2, "PLAYER SCORES");
	}
	else if (memory->pongState == PS_COMPUTER_SCORE)
	{
		GPU_DrawTextOutlined(C_C2, C_BLACK, f_big, GPU_X/2, GPU_Y/2-f_big->height/2, "AI SCORES");
	}
	else if (memory->pongState == PS_PLAYER_WINS || memory->pongState == PS_COMPUTER_WINS)
	{
		GPU_DrawPanel(RGB(16,18,48), memory->pongState == PS_PLAYER_WINS ? C_C3 : C_RED,
		              (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-44), 180, 92);
		GPU_DrawTextOutlined(memory->pongState == PS_PLAYER_WINS ? C_C3 : C_RED, C_BLACK, f_big,
		                     GPU_X/2, (int16_t)(GPU_Y/2-38),
		                     memory->pongState == PS_PLAYER_WINS ? "YOU WIN" : "AI WINS");
		char score[24];
		sprintf(score, "%u : %u", memory->score_player, memory->score_computer);
		GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-10), score);
		GPU_DrawButton(C_YELLOW, C_BLACK, f_big, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
		               RESET_BUTTON_W, RESET_BUTTON_H);
	}
}

static void G_Deinit(void)
{

}

const Callbacks G13_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
