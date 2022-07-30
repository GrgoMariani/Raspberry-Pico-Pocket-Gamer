// TIC TAC TOE 1P

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"
#include <stdint.h>
#include <string.h>

typedef enum {
    TTT_EMPTY     = 0,
    TTT_PLAYER_1  = 1,
    TTT_PLAYER_2  = 2
} TicTacToeBoardState;

typedef enum {
    TTTP_PLAYER_1 = 1,
    TTTP_PLAYER_2 = 2
} TicTacToePlayer;

typedef enum {
    TTTGS_PLAYING     = 0,
    TTTGS_PLAYER1_WIN = 1,
    TTTGS_PLAYER2_WIN = 2,
    TTTGS_DRAW        = 3
} TicTacToeGameState;

typedef struct {
    TicTacToeBoardState _boardState[3][3];
    uint8_t _numOfFinishedMoves;
    TicTacToePlayer _nextPlayer;
    TicTacToePlayer _aiPlayer;
    uint8_t _victoryLine;
    TicTacToeGameState _finalState;
} Game1Memory;

typedef struct Position {
	uint8_t x;
	uint8_t y;
	TicTacToeGameState state_result;
} Position;

#define NUM_TICKS 1

static Game1Memory * const memory = (Game1Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static inline uint32_t CalculateTick(long long timeNow)
{
	return timeNow/(ONE_SECOND/NUM_TICKS);
}

static TicTacToeGameState CheckIsThereAWinner()
{
	if (memory->_numOfFinishedMoves < 4)
		return TTTGS_PLAYING;
	for (uint8_t i=0; i<3; i++)
	{
		if (memory->_boardState[i][0] != TTT_EMPTY && memory->_boardState[i][0] == memory->_boardState[i][1] && memory->_boardState[i][0] == memory->_boardState[i][2])
		{
			if (memory->_boardState[i][0] == TTT_PLAYER_1 )
				return TTTGS_PLAYER1_WIN;
			return TTTGS_PLAYER2_WIN;
		}
	}
	for (uint8_t i=0; i<3; i++)
	{
		if (memory->_boardState[0][i] != TTT_EMPTY && memory->_boardState[0][i] == memory->_boardState[1][i] && memory->_boardState[0][i] == memory->_boardState[2][i])
		{
			if (memory->_boardState[0][i] == TTT_PLAYER_1 )
				return TTTGS_PLAYER1_WIN;
			return TTTGS_PLAYER2_WIN;
		}
	}
	if (memory->_boardState[0][0] != TTT_EMPTY && memory->_boardState[0][0] == memory->_boardState[1][1] && memory->_boardState[0][0] == memory->_boardState[2][2])
	{
		if (memory->_boardState[0][0] == TTT_PLAYER_1 )
			return TTTGS_PLAYER1_WIN;
		return TTTGS_PLAYER2_WIN;
	}
	if (memory->_boardState[2][0] != TTT_EMPTY && memory->_boardState[2][0] == memory->_boardState[1][1] && memory->_boardState[2][0] == memory->_boardState[0][2])
	{
		if (memory->_boardState[2][0] == TTT_PLAYER_1 )
			return TTTGS_PLAYER1_WIN;
		return TTTGS_PLAYER2_WIN;
	}
	if (memory->_numOfFinishedMoves == 9)
		return TTTGS_DRAW;
	return TTTGS_PLAYING;
}

static void CheckVictoryLine()
{
	
	for (uint8_t i=0; i<3; i++)
	{
		if (memory->_boardState[i][0] != TTT_EMPTY && memory->_boardState[i][0] == memory->_boardState[i][1] && memory->_boardState[i][0] == memory->_boardState[i][2])
		{
			memory->_victoryLine |= 1<<i;
		}
	}
	for (uint8_t i=0; i<3; i++)
	{
		if (memory->_boardState[0][i] != TTT_EMPTY && memory->_boardState[0][i] == memory->_boardState[1][i] && memory->_boardState[0][i] == memory->_boardState[2][i])
		{
			memory->_victoryLine |= 1<<(i+3);
		}
	}
	if (memory->_boardState[0][0] != TTT_EMPTY && memory->_boardState[0][0] == memory->_boardState[1][1] && memory->_boardState[0][0] == memory->_boardState[2][2])
	{
		memory->_victoryLine |= 1<<6;
	}
	if (memory->_boardState[2][0] != TTT_EMPTY && memory->_boardState[2][0] == memory->_boardState[1][1] && memory->_boardState[2][0] == memory->_boardState[0][2])
	{
		memory->_victoryLine |= 1<<7;
	}
}


static Position ReturnBestPositionForNextMove(TicTacToePlayer nextPlayer)
{
	Position result = {0, 0, TTTGS_PLAYING};
    for (uint8_t i = 0; i < 3; i++)
    {
    	for (uint8_t j = 0; j < 3; j++)
    	{
    		if ( memory->_boardState[i][j] != TTT_EMPTY)
    		{
    			continue;
    		}
    		memory->_boardState[i][j] = nextPlayer;
    		memory->_numOfFinishedMoves++;
    		result.x = i;
    		result.y = j;
    		result.state_result = CheckIsThereAWinner();
    		memory->_boardState[i][j] = TTT_EMPTY;
    		memory->_numOfFinishedMoves--;
			if ( result.state_result == TTTGS_PLAYER1_WIN && nextPlayer == TTTP_PLAYER_1)
				break;
			if ( result.state_result == TTTGS_PLAYER2_WIN && nextPlayer == TTTP_PLAYER_2)
				break;
    	}
    	if ( result.state_result == TTTGS_PLAYER1_WIN && nextPlayer == TTTP_PLAYER_1)
			break;
		if ( result.state_result == TTTGS_PLAYER2_WIN && nextPlayer == TTTP_PLAYER_2)
			break;
    }
    return result;
}

static void PlayerPlaysPosition(uint8_t x, uint8_t y, TicTacToePlayer player)
{
	if (memory->_boardState[x][y] != TTT_EMPTY)
	{
		return;
	}
    memory->_boardState[x][y] = (player == TTTP_PLAYER_1) ? TTT_PLAYER_1 : TTT_PLAYER_2;
	memory->_numOfFinishedMoves++;
	memory->_nextPlayer = (memory->_nextPlayer == TTTP_PLAYER_1) ? TTTP_PLAYER_2 : TTTP_PLAYER_1;
}

static void AIChooseBestMove()
{
	TicTacToePlayer ai = memory->_aiPlayer;
	TicTacToePlayer p1 = memory->_aiPlayer == TTTP_PLAYER_1 ? TTTP_PLAYER_2 : TTTP_PLAYER_1;
	TicTacToeGameState desiredState = (ai == TTTP_PLAYER_1) ? TTTGS_PLAYER1_WIN : TTTGS_PLAYER2_WIN;
	TicTacToeGameState losingState = (ai == TTTP_PLAYER_1) ? TTTGS_PLAYER2_WIN : TTTGS_PLAYER1_WIN;
	Position res = ReturnBestPositionForNextMove(ai);
	if (res.state_result == desiredState)
	{
		PlayerPlaysPosition(res.x, res.y, ai);
		return;
	}
	res = ReturnBestPositionForNextMove(p1);
	if (res.state_result == losingState)
	{
		PlayerPlaysPosition(res.x, res.y, ai);
		return;
	}
	// choosing a random leftover state
	uint32_t rand = IF_Random();
	uint8_t num_of_free_spaces = 0;
	for (uint8_t i=0; i<3; i++)
		for (uint8_t j=0; j<3; j++)
		{
			if (memory->_boardState[i][j] == TTT_EMPTY)
				num_of_free_spaces++;
		}
	uint8_t choose_rand = rand % num_of_free_spaces;
	num_of_free_spaces = 0;
	for (uint8_t i=0; i<3; i++)
		for (uint8_t j=0; j<3; j++)
		{
			if (memory->_boardState[i][j] == TTT_EMPTY)
			{
				if (num_of_free_spaces == choose_rand)
				{
					PlayerPlaysPosition(i, j, ai);
				}
				num_of_free_spaces++;
			}
		}
}

static void G_Init(void)
{
	mainMemory._gameState = GS_RUNNING;
	memory->_numOfFinishedMoves = 0;
	for (uint8_t i=0; i<3; i++)
		for (uint8_t j=0; j<3; j++)
		{
			memory->_boardState[i][j] = TTT_EMPTY;
		}
	memory->_nextPlayer = TTTP_PLAYER_1;
	uint32_t rand = IF_Random();
	memory->_aiPlayer = (rand & 0x80) ? TTTP_PLAYER_1 : TTTP_PLAYER_2;
	mainMemory._lastRenderedTick = CalculateTick(IF_GetCurrentTime());
	memory->_victoryLine = 0;
	memory->_finalState = TTTGS_PLAYING;
}


#define START_X GPU_BORDER_X
#define END_X (GPU_X/2-GPU_BORDER_X)
#define START_Y GPU_BORDER_Y
#define END_Y (GPU_Y-GPU_BORDER_Y)
#define BOX_DIMENSION ((END_X-START_X)/3)

#define RESET_BUTTON_W (5*16)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (S2_CENTER_X - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (S2_CENTER_Y + 30)

static void G_Update(void)
{
	uint32_t tickNow = CalculateTick(IF_GetCurrentTime());
	KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
	if (keyboard & KEY_MENU)
	{
	    SetupCallbacks(&MM_Callbacks);
	    return;
	}
	
	while (mainMemory._lastRenderedTick < tickNow && mainMemory._gameState == GS_RUNNING)
	{
		if (memory->_nextPlayer == memory->_aiPlayer)
		{
			AIChooseBestMove();
		}
		else
		{
			if (mainMemory.touchPressed)
			{
				uint32_t x=4, y=4;
				if (mainMemory.touch_X >= START_X && mainMemory.touch_X <= END_X && mainMemory.touch_Y >= START_Y && mainMemory.touch_Y <= END_Y)
				{
					x = mainMemory.touch_X-START_X;
					y = mainMemory.touch_Y-START_Y;
					x /= BOX_DIMENSION;
					y /= BOX_DIMENSION;
					PlayerPlaysPosition(x, y, memory->_nextPlayer);
				}
			}
		}
		TicTacToeGameState gs = CheckIsThereAWinner();
		if (gs != TTTGS_PLAYING)
		{
			memory->_finalState = gs;
			mainMemory._gameState = GS_DONE;
			CheckVictoryLine();
		}
		mainMemory._lastRenderedTick++;
	}
	if (mainMemory._gameState != GS_RUNNING)
	{
		if (mainMemory.touchPressed)
		{
			if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W && mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
				G_Init();
		}
	}
}


#define C_BORDERS C_WHITE
#define C_X C_GREEN
#define C_O C_RED
#define C_TEXT C_WHITE
#define C_VLINE C_YELLOW
#define C_VICTORY C_BLUE

static void G_Draw(void)
{
	const FontDescription * f = &fontDescription[1];
	const FontDescription * f_med = &fontDescription[2];
	const FontDescription * f_big = &fontDescription[3];
	TicTacToeBoardState aiState = (memory->_aiPlayer == TTTP_PLAYER_1) ? TTT_PLAYER_1 : TTT_PLAYER_2;
	TicTacToeBoardState player1State = (memory->_aiPlayer == TTTP_PLAYER_1) ? TTT_PLAYER_2 : TTT_PLAYER_1;
	GPU_ClearFramebuffers();

	GPU_DrawLine(C_BORDERS, GPU_BORDER_X,                  GPU_BORDER_Y + (GPU_REST)/3,   GPU_BORDER_X+GPU_REST-1,       GPU_BORDER_Y+GPU_REST/3);
	GPU_DrawLine(C_BORDERS, GPU_BORDER_X,                  GPU_BORDER_Y + 2*(GPU_REST)/3, GPU_BORDER_X+GPU_REST-1,       GPU_BORDER_Y+2*(GPU_REST)/3);
	GPU_DrawLine(C_BORDERS, GPU_BORDER_X + (GPU_REST/3),   GPU_BORDER_Y,                  GPU_BORDER_X + (GPU_REST)/3,   GPU_BORDER_Y+GPU_REST-1);
	GPU_DrawLine(C_BORDERS, GPU_BORDER_X + (2*GPU_REST/3), GPU_BORDER_Y,                  GPU_BORDER_X + (2*GPU_REST)/3, GPU_BORDER_Y+GPU_REST-1);
	for (uint8_t i=0; i<3; i++)
		for (uint8_t j=0; j<3; j++)
		{
			if (memory->_boardState[i][j] == player1State)
				GPU_DrawLetter(C_X, f_big, GPU_BORDER_X+i*GPU_REST/3 + GPU_REST/6-f_big->width/2, GPU_BORDER_Y+j*GPU_REST/3 + GPU_REST/6-f_big->height/2, 'X');
			else if (memory->_boardState[i][j] == aiState)
				GPU_DrawLetter(C_O, f_big, GPU_BORDER_X+i*GPU_REST/3 + GPU_REST/6-f_big->width/2, GPU_BORDER_Y+j*GPU_REST/3 + GPU_REST/6-f_big->height/2, '0');
		}
	if (mainMemory._gameState == GS_RUNNING)
	{
		if (memory->_nextPlayer == memory->_aiPlayer)
		{
            const char * txtCalculating = "COMPUTING";
			uint8_t len = strlen(txtCalculating);
            GPU_DrawText(C_RED, f_med, GPU_X/2+GPU_X/4-f_med->width*(len)/2, GPU_Y/2-f_med->height/2, txtCalculating, len);
		}
		else
		{
			const char * textToDisplay = "Your move\0";
			uint8_t len = strlen(textToDisplay);
            GPU_DrawText(C_TEXT, f_med, GPU_X/2+GPU_X/4-f_med->width*(len)/2, GPU_Y/2-f_med->height/2, textToDisplay, len);
		}
	}
	else
	{
		TicTacToeGameState aiWins = (memory->_aiPlayer == TTTP_PLAYER_1) ? TTTGS_PLAYER1_WIN : TTTGS_PLAYER2_WIN;
		TicTacToeGameState playerWins = (memory->_aiPlayer == TTTP_PLAYER_1) ? TTTGS_PLAYER2_WIN : TTTGS_PLAYER1_WIN;
		uint8_t line = memory->_victoryLine;
		for (uint8_t i=0; i<3; i++)
			if (line & (1<<i))
				GPU_DrawLine(C_VLINE, GPU_BORDER_X + GPU_REST/3*i+GPU_REST/6, GPU_BORDER_Y, GPU_BORDER_X + GPU_REST/3*i+GPU_REST/6, GPU_BORDER_Y + GPU_REST -1);
		for (uint8_t i=0; i<3; i++)
			if (line & (1<<(3+i)))
				GPU_DrawLine(C_VLINE, GPU_BORDER_X, GPU_BORDER_Y + GPU_REST/3*i+GPU_REST/6, GPU_BORDER_X+GPU_REST-1, GPU_BORDER_Y + GPU_REST/3*i+GPU_REST/6);
		if (line & (1<<6))
			GPU_DrawLine(C_VLINE, GPU_BORDER_X, GPU_BORDER_Y, GPU_BORDER_X+GPU_REST-1, GPU_BORDER_Y+GPU_REST-1);
		if (line & (1<<7))
			GPU_DrawLine(C_VLINE, GPU_BORDER_X+GPU_REST-1, GPU_BORDER_Y, GPU_BORDER_X, GPU_BORDER_Y+GPU_REST-1);
		if (memory->_finalState == TTTGS_DRAW)
		{
			const char * textToDisplay = "Draw\0";
			int8_t len = strlen(textToDisplay);
			GPU_DrawText(C_VICTORY, f_med, S2_CENTER_X-len*f_med->width/2, S2_CENTER_Y-f_med->height/2, textToDisplay, len);
		}
		else if (memory->_finalState == aiWins)
		{
			const char * textToDisplay = "Computer Wins\0";
			int8_t len = strlen(textToDisplay);
			GPU_DrawText(C_VICTORY, f_med, S2_CENTER_X-len*f_med->width/2, S2_CENTER_Y-f_med->height/2, textToDisplay, len);
		}
		else if (memory->_finalState == playerWins)
		{
			const char * textToDisplay = "Player Wins\0";
			int8_t len = strlen(textToDisplay);
			GPU_DrawText(C_VICTORY, f_med, S2_CENTER_X-len*f_med->width/2, S2_CENTER_Y-f_med->height/2, textToDisplay, len);
		}
		// Draw reset button
		{
			const char * textToDisplay = "RESET";
			int8_t len = strlen(textToDisplay);
			GPU_DrawFilledSquare(C_YELLOW, RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H);
			GPU_DrawText(C_BLACK, f_big, RESET_BUTTON_X, RESET_BUTTON_Y, textToDisplay, len);
		}
	}

	GPU_Render();
}

static void G_Deinit(void)
{

}

const Callbacks G1_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit};
