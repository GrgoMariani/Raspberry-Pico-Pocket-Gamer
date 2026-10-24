#ifndef TICTACTOE_H_
#define TICTACTOE_H_

#include "../gameshared.h"
#include "../mainLoop.h"

/*
 * Shared implementation of the two tic tac toe variants. game01 plays against
 * a simple AI, game02 is two players on one device; everything else (board,
 * input, drawing) is the same code path.
 */

void TTT_Init(uint8_t withAI);
void TTT_Update(uint8_t withAI);
void TTT_Draw(uint8_t withAI);
void TTT_Deinit(void);

#endif//TICTACTOE_H_
