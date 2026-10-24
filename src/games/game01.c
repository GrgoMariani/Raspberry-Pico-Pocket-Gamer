// TIC TAC TOE 1P - against the computer, see tictactoe.c

#include "tictactoe.h"

static void G_Init(void)   { TTT_Init(1); }
static void G_Update(void) { TTT_Update(1); }
static void G_Draw(void)   { TTT_Draw(1); }

const Callbacks G1_Callbacks = { &G_Init, &G_Update, &G_Draw, &TTT_Deinit };
