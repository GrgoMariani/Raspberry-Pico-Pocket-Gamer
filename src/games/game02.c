// TIC TAC TOE 2P - two players on one device, see tictactoe.c

#include "tictactoe.h"

static void G_Init(void)   { TTT_Init(0); }
static void G_Update(void) { TTT_Update(0); }
static void G_Draw(void)   { TTT_Draw(0); }

const Callbacks G2_Callbacks = { &G_Init, &G_Update, &G_Draw, &TTT_Deinit };
