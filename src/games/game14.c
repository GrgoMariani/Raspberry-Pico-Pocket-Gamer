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

/* one of up to three balls in play (the multiball power-up adds two) */
typedef struct {
	float x, y;
	float sin, cos;
	uint8_t active;
} Ball;

/* falling power-up released by a destroyed brick */
typedef struct {
	float x, y;
	uint8_t type;      /* 0 wide paddle, 1 slow ball, 2 points, 3 multiball, 4 life */
	uint8_t active;
} PowerUp;

typedef struct {
	float player_x;
	Ball balls[3];
	float ballSpeedMul;      /* the balls get faster with every block hit */
	uint32_t score;
	uint8_t lives;           /* player lives: losing all balls costs one */
	uint8_t level;           /* cleared boards advance to a harder level */
	uint16_t levelFlash;     /* ticks of the "LEVEL n" popup */
	BreakoutState breakoutState;
	Block blocks[6][4];
	PowerUp drops[3];
	uint16_t wideTimer;      /* ticks of wide paddle remaining */
	uint16_t slowTimer;      /* ticks of slow ball remaining */
	uint16_t popupTicks;     /* ticks of the power-up name popup */
	uint8_t popupType;       /* which power-up was caught last */
} Game14Memory;

static Game14Memory * const memory = (Game14Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define NUM_TICKS 20

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* serves upwards, but never flatter than ~27 degrees so that the ball cannot
   end up bouncing between the side walls forever */
static void ServeBall(Ball * ball)
{
	ball->x = 0.0f;
	ball->y = 100.0f;
	int32_t up = 45 + (int32_t)Random_Range(51);      /* 0.45 .. 0.95 */
	ball->sin = -(float)up/100.0f;
	ball->cos = sqrtf(1.0f - ball->sin*ball->sin);
	if (Random_Range(2))
		ball->cos = -ball->cos;
	ball->active = 1;
}

static void InitiateBalls()
{
	for (uint8_t b = 0; b < 3; b++)
		memory->balls[b].active = 0;
	ServeBall(&memory->balls[0]);
}

/* the brick wall of a level: early levels are soft, later ones bring bricks
   with up to four lives in a diagonal pattern */
static void BuildBlocks()
{
	uint8_t spread = memory->level < 4 ? memory->level : 4;
	for (uint8_t i = 0; i < 6; i++)
		for (uint8_t j = 0; j < 4; j++)
			memory->blocks[i][j].lives = (uint8_t)(1 + (i + 2*j + memory->level - 1) % spread);
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
	memory->breakoutState = BS_NOT_INITIALIZED;
	memory->player_x = 0.0f;
	memory->ballSpeedMul = 1.0f;
	memory->score = 0;
	memory->lives = 3;
	memory->level = 1;
	memory->levelFlash = 0;
	memory->wideTimer = 0;
	memory->slowTimer = 0;
	memory->popupTicks = 0;
	memory->popupType = 0;
	for (uint8_t d = 0; d < 3; d++)
		memory->drops[d].active = 0;
	mainMemory._lastRenderedTick = CalculateTick();
	Keyboard_GetPressedKeys();
	Touch_ClearTap();
	InitiateBalls();
	BuildBlocks();
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

/* current paddle half-width (wider while the power-up is active) */
static float PaddleHalf(void)
{
	return memory->wideTimer ? PLAYER_V_W*0.75f : PLAYER_V_W/2;
}

/* returns 1 when the ball fell out of the field (it is deactivated then) */
static uint8_t CheckWallCollisions(Ball * ball)
{
	if (ball->x < -100.0f)
	{
		ball->x = -100.0f;
		ball->cos *= -1.0f;
	}
	else if (ball->x > 100.0f)
	{
		ball->x = 100.0f;
		ball->cos *= -1.0f;
	}
	if (ball->y < -100.0f)
	{
		ball->y = -100.0f;
		ball->sin *= -1.0f;
	}
	else if (ball->y > 100.0f)
	{
		float half = PaddleHalf();
		if (memory->player_x-half<ball->x && memory->player_x+half > ball->x)
		{
			float diff = (ball->x-memory->player_x)/(half+0.05f);
			ball->cos = diff;
			ball->sin = -sqrt(1-diff*diff);
			/* back onto the paddle so the contact is only evaluated once */
			ball->y = 100.0f;
		}
		else if (ball->y > 106.0f)
		{
			ball->active = 0;
			return 1;
		}
	}
	return 0;
}

static uint8_t _intersects(const Ball * ball, float rect_x, float rect_y)
{
    float circleDistancex = ball->x - rect_x;
    float circleDistancey = ball->y - rect_y;
	circleDistancex = circleDistancex > 0 ? circleDistancex : -circleDistancex;
	circleDistancey = circleDistancey > 0 ? circleDistancey : -circleDistancey;

    if (circleDistancex > (BLOCK_V_W/2 + BALL_SIZE)) { return 0; }
    if (circleDistancey > (BLOCK_V_H/2 + BALL_SIZE)) { return 0; }

    if (circleDistancex <= (BLOCK_V_W/2)) { return REFLECT_SIN; } 
    if (circleDistancey <= (BLOCK_V_H/2)) { return REFLECT_COS; }

    float cornerDistance_sq = (circleDistancex - BLOCK_V_W/2)*(circleDistancex - BLOCK_V_W/2) +
                              (circleDistancey - BLOCK_V_H/2)*(circleDistancey - BLOCK_V_H/2);

	if (cornerDistance_sq <= BALL_SIZE*BALL_SIZE)
		return REFLECT_COS | REFLECT_SIN;
	return 0;
}

/* weighted draw: the extra life stays rare, multiball is the fun middle */
static uint8_t DrawPowerUpType(void)
{
	uint32_t r = Random_Range(12);
	if (r < 3) return 0;        /* wide   3/12 */
	if (r < 6) return 1;        /* slow   3/12 */
	if (r < 9) return 2;        /* points 3/12 */
	if (r < 11) return 3;       /* multi  2/12 */
	return 4;                   /* life   1/12 */
}

static int CheckBlockCollisions(Ball * ball)
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
			intersects_block = _intersects(ball, x, y);
			if (intersects_block)
			{
				memory->blocks[i][j].lives--;
				memory->score += 10;
				if (!memory->blocks[i][j].lives)
				{
					memory->score += 25;
					/* a destroyed brick may release a power-up */
					if (Random_Range(4) == 0)
					{
						for (uint8_t d = 0; d < 3; d++)
							if (!memory->drops[d].active)
							{
								memory->drops[d].active = 1;
								memory->drops[d].x = x;
								memory->drops[d].y = y;
								memory->drops[d].type = DrawPowerUpType();
								break;
							}
					}
				}
				if (memory->ballSpeedMul < 1.6f)
					memory->ballSpeedMul += 0.02f;
				if (intersects_block & REFLECT_COS)
					ball->cos *= -1.0f;
				if (intersects_block & REFLECT_SIN)
					ball->sin *= -1.0f;
				return 1;
			}
		}
	return 0;
}

/* all bricks gone: advance to the next level instead of ending the game */
static void CheckLevelCleared()
{
	for (uint8_t i=0; i<6; i++)
		for (uint8_t j=0; j<4; j++)
			if (memory->blocks[i][j].lives)
				return;
	memory->score += 250;
	if (memory->level < 99)
		memory->level++;
	memory->levelFlash = 40;
	/* the pace carries over a little into the next level */
	memory->ballSpeedMul = 1.0f + 0.08f*(memory->level-1);
	if (memory->ballSpeedMul > 1.5f)
		memory->ballSpeedMul = 1.5f;
	memory->wideTimer = 0;
	memory->slowTimer = 0;
	for (uint8_t d = 0; d < 3; d++)
		memory->drops[d].active = 0;
	BuildBlocks();
	InitiateBalls();
}

static void MoveBall(Ball * ball)
{
	float mul = memory->ballSpeedMul;
	if (memory->slowTimer)
		mul *= 0.65f;
	ball->x += ball->cos*BALL_SPEED*mul;
	ball->y += ball->sin*BALL_SPEED*mul;
}

/* multiball: clone the first live ball twice, fanned out by +/- 22 degrees */
static void SpawnMultiball(void)
{
	Ball * src = 0;
	for (uint8_t b = 0; b < 3; b++)
		if (memory->balls[b].active) { src = &memory->balls[b]; break; }
	if (!src)
		return;
	const float ca = 0.927f, sa = 0.375f;   /* cos/sin of ~22 degrees */
	uint8_t clone = 0;
	for (uint8_t b = 0; b < 3; b++)
	{
		Ball * dst = &memory->balls[b];
		if (dst->active)
			continue;
		dst->x = src->x;
		dst->y = src->y;
		/* fan the clones out to opposite sides, no matter which ball is the
		   source (the old sign depended on the slot index, so two clones could
		   fly in the same direction) */
		float sign = (clone++ == 0) ? 1.0f : -1.0f;
		dst->cos = src->cos*ca - src->sin*sa*sign;
		dst->sin = src->sin*ca + src->cos*sa*sign;
		dst->active = 1;
	}
}

/* power-ups fall towards the paddle; catching one applies its effect */
static void UpdatePowerUps()
{
	if (memory->wideTimer) memory->wideTimer--;
	if (memory->slowTimer) memory->slowTimer--;
	for (uint8_t d = 0; d < 3; d++)
	{
		PowerUp * p = &memory->drops[d];
		if (!p->active)
			continue;
		p->y += 1.6f;
		/* caught by the paddle? */
		if (p->y > 96.0f && p->y < 104.0f)
		{
			float half = PaddleHalf();
			if (p->x > memory->player_x-half && p->x < memory->player_x+half)
			{
				p->active = 0;
				if (p->type == 0) memory->wideTimer = 200;
				else if (p->type == 1) memory->slowTimer = 160;
				else if (p->type == 2) memory->score += 100;
				else if (p->type == 3) SpawnMultiball();
				else if (p->type == 4 && memory->lives < 5) memory->lives++;
				/* tell the player what they just caught */
				memory->popupType = p->type;
				memory->popupTicks = 20;
				continue;
			}
		}
		if (p->y > 110.0f)
			p->active = 0;
	}
}

static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
	uint32_t current_tick = CalculateTick();
	while (mainMemory._lastRenderedTick < current_tick)
	{
		if (memory->breakoutState == BS_PLAYING)
		{
			if (memory->levelFlash)
				memory->levelFlash--;
			if (memory->popupTicks)
				memory->popupTicks--;
			/* dragging a finger moves the paddle */
			if (mainMemory.touchPressed)
			{
				float x = ((float)mainMemory.touch_X - EDGE_X)/BOX_W*200.0f - 100.0f;
				if (x < -(100.0f-PLAYER_V_W/2)) x = -(100.0f-PLAYER_V_W/2);
				if (x > (100.0f-PLAYER_V_W/2)) x = (100.0f-PLAYER_V_W/2);
				memory->player_x = x;
			}
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
			uint8_t anyAlive = 0;
			for (uint8_t b = 0; b < 3; b++)
			{
				Ball * ball = &memory->balls[b];
				if (!ball->active)
					continue;
				MoveBall(ball);
				if (!CheckWallCollisions(ball))
				{
					CheckBlockCollisions(ball);
					anyAlive = 1;
				}
			}
			if (!anyAlive)
			{
				/* every ball lost costs a life; the game ends at zero */
				if (memory->lives)
					memory->lives--;
				if (memory->lives == 0)
				{
					memory->breakoutState = BS_GAME_OVER;
				}
				else
				{
					InitiateBalls();
					memory->wideTimer = 0;
					memory->slowTimer = 0;
				}
			}
			else
			{
				CheckLevelCleared();
			}
			UpdatePowerUps();
		}
		else
		{
			uint16_t tx, ty;
			uint8_t tap = Touch_TakeTap(&tx, &ty);
			if (Keyboard_GetPressedKeys() != KEY_NONE || tap)
			{
				G_Init();
				memory->breakoutState = BS_PLAYING;
			}
		}
		// Update state
		mainMemory._lastRenderedTick++;
	}
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[2];
	const FontDescription * fm = &fontDescription[1];
    GPU_ClearFramebuffers();
	GPU_DrawVerticalGradient(RGB(8,6,26), C_BLACK, 0, 0, GPU_X, GPU_Y);
	// Draw Field
	GPU_DrawPanel(RGB(14,10,40), C_C2, EDGE_X, EDGE_Y, BOX_W, BOX_H);
	// Draw Player
	{
		float pw = (PaddleHalf()*2)/200.0f*BOX_W;
		GPU_Color pc = memory->wideTimer ? C_C3 : C_C1;
		GPU_DrawBevelSquare(pc, (int16_t)(EDGE_X+(memory->player_x+100.0f)/200.0f*BOX_W-pw/2), (int16_t)(GPU_Y-EDGE_Y-PLAYER_H/2), (int16_t)pw, PLAYER_H);
	}
	// Draw Blocks
	for (uint8_t i=0; i<6; i++)
		for (uint8_t j=0; j<4; j++)
		{
			if (memory->blocks[i][j].lives)
			{
				/* classic rainbow rows; a damaged brick dims, a brick on its
				   last life gets cracks so the remaining hit points read at a
				   glance (they used to be four shades of grey) */
				static const GPU_Color rowColor[4] = { C_RED, C_ORANGE, C_YELLOW, C_C3 };
				uint8_t lives = memory->blocks[i][j].lives;
				int16_t shade = (int16_t)(110 + lives*50);
				if (shade > 310) shade = 310;
				GPU_Color color = GPU_ShadeColor(rowColor[j], shade);
				int16_t bx = (int16_t)(EDGE_X + (BLOCK_V_EDGE_X+(BLOCK_V_W+BLOCK_V_SEPARATOR_W)*i)/200.0f*BOX_W);
				int16_t by = (int16_t)(EDGE_Y+(BLOCK_V_EDGE_Y+(BLOCK_V_H+BLOCK_V_SEPARATOR_H)*j)/200.0f*BOX_H);
				GPU_DrawBevelSquare(color, bx, by, (int16_t)BLOCK_W, (int16_t)BLOCK_H);
				if (lives == 1 && memory->level > 1)
				{
					GPU_Color crack = GPU_ShadeColor(color, 60);
					GPU_DrawLine(crack, (int16_t)(bx+BLOCK_W*0.3f), by+2, (int16_t)(bx+BLOCK_W*0.45f), (int16_t)(by+BLOCK_H-3));
					GPU_DrawLine(crack, (int16_t)(bx+BLOCK_W*0.45f), (int16_t)(by+BLOCK_H/2), (int16_t)(bx+BLOCK_W*0.7f), (int16_t)(by+BLOCK_H-2));
				}
			}
		}
	// Draw Balls
	for (uint8_t b = 0; b < 3; b++)
	{
		Ball * ball = &memory->balls[b];
		if (!ball->active)
			continue;
		int16_t bx = (int16_t)(EDGE_X + (ball->x+100.0f)/200.0f*BOX_W);
		int16_t by = (int16_t)(EDGE_Y + (ball->y+100.0f)/200.0f*BOX_H);
		GPU_DrawGlow(C_C3, bx, by, (int16_t)(BALL_SIZE+4));
		GPU_DrawBall(C_C3, bx, by, (int16_t)BALL_SIZE);
	}
	// Draw falling power-ups
	{
		const FontDescription * fs = &fontDescription[0];
		for (uint8_t d = 0; d < 3; d++)
		{
			PowerUp * p = &memory->drops[d];
			if (!p->active)
				continue;
			int16_t px = (int16_t)(EDGE_X + (p->x+100.0f)/200.0f*BOX_W);
			int16_t py = (int16_t)(EDGE_Y + (p->y+100.0f)/200.0f*BOX_H);
			GPU_Color c = (p->type == 0) ? C_C3 : (p->type == 1) ? C_CYAN :
			              (p->type == 2) ? C_YELLOW : (p->type == 3) ? C_MAGENTA : C_RED;
			const char * label = (p->type == 0) ? "W" : (p->type == 1) ? "S" :
			                     (p->type == 2) ? "+" : (p->type == 3) ? "M" : "L";
			GPU_DrawGlow(c, px, py, 7);
			GPU_DrawBevelSquare(c, (int16_t)(px-6), (int16_t)(py-6), 12, 12);
			GPU_DrawTextCentered(C_BLACK, fs, px, (int16_t)(py-4), label);
		}
	}
	// score, level and lives
	{
		char hud[32];
		int hlen = sprintf(hud, "LEVEL %u", memory->level);
		GPU_DrawText(C_GRAY_LIGHT, fm, 4, 4, hud, (uint16_t)hlen);
		hlen = sprintf(hud, "SCORE %u", (unsigned)memory->score);
		GPU_DrawText(C_YELLOW, fm, (int16_t)(GPU_X - 4 - hlen*fm->width), 4, hud, (uint16_t)hlen);
		for (uint8_t i = 0; i < memory->lives && i < 5; i++)
			GPU_DrawBall(C_C1, (int16_t)(GPU_X/2 - 28 + i*14), 9, 4);
	}
	if (memory->levelFlash && memory->breakoutState == BS_PLAYING)
	{
		char text[16];
		sprintf(text, "LEVEL %u", memory->level);
		GPU_DrawTextOutlined(C_C3, C_BLACK, f_big, GPU_X/2, (int16_t)(GPU_Y/2-30), text);
	}
	if (memory->popupTicks && memory->breakoutState == BS_PLAYING)
	{
		static const char * const popupText[5] = { "WIDE!", "SLOW!", "+100", "MULTIBALL!", "+1 LIFE" };
		static const GPU_Color popupColor[5] = { C_C3, C_CYAN, C_YELLOW, C_MAGENTA, C_RED };
		GPU_DrawTextOutlined(popupColor[memory->popupType], C_BLACK, fm, GPU_X/2,
		                     (int16_t)(GPU_Y-EDGE_Y-26), popupText[memory->popupType]);
	}
	// Draw Text
	if (memory->breakoutState != BS_PLAYING)
	{
		char text[32];
		switch (memory->breakoutState)
		{
			case BS_GAME_OVER:
				sprintf(text, "YOU LOSE");
				break;
			case BS_GAME_WON:
				sprintf(text, "YOU WIN");
				break;
			default:
				sprintf(text, "PRESS ANY KEY");
				break;
		}
		GPU_DrawTextOutlined(C_GREEN, C_BLACK, f_big, GPU_X/2, (int16_t)(GPU_Y/2-f_big->height/2), text);
		if (memory->breakoutState == BS_NOT_INITIALIZED)
			GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+12), "or touch, drag moves the paddle");
		if (memory->breakoutState == BS_GAME_OVER)
		{
			char text2[32];
			sprintf(text2, "SCORE %u  LEVEL %u", (unsigned)memory->score, memory->level);
			GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2+14), text2);
		}
	}
}

static void G_Deinit(void)
{

}

const Callbacks G14_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };