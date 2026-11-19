#ifndef MAIN_LOOP_H_
#define MAIN_LOOP_H_

typedef struct Callbacks {
    void (* initCallback)(void);
    void (* updateCallback) (void);
    void (* drawCallback) (void);
    void (* deinitCallback) (void);
} Callbacks;

void SetupCallbacks(const Callbacks * callbacks);
void ResetGame();
void DoMainLoop();

/* how long two MENU presses may be apart to count as a double press */
#define MENU_DOUBLE_PRESS_US 400000

extern const Callbacks MM_Callbacks;    // Main Menu
extern const Callbacks G1_Callbacks;    // TicTacToe 1P
extern const Callbacks G2_Callbacks;    // TicTacToe 2P
extern const Callbacks G3_Callbacks;    // Lights Out
extern const Callbacks G4_Callbacks;    // Math
extern const Callbacks G5_Callbacks;    // Tetramino
extern const Callbacks G6_Callbacks;    // Labirinith
extern const Callbacks G7_Callbacks;    // Connect4 1P
extern const Callbacks G8_Callbacks;    // Connect4 2P
extern const Callbacks G9_Callbacks;    // 2048
extern const Callbacks G10_Callbacks;   // Paint.NOT
extern const Callbacks G11_Callbacks;   // Snake
extern const Callbacks G12_Callbacks;   // Calculator
extern const Callbacks G13_Callbacks;   // Pong
extern const Callbacks G14_Callbacks;   // Breakout
extern const Callbacks G15_Callbacks;   // Q*bert
extern const Callbacks G16_Callbacks;   // Mission Control
extern const Callbacks G17_Callbacks;   // Asteroids
extern const Callbacks G18_Callbacks;   // Frogger
extern const Callbacks G19_Callbacks;   // Tunnel Rush
extern const Callbacks G20_Callbacks;   // Missile Defense
extern const Callbacks G21_Callbacks;   // PacMan
extern const Callbacks G22_Callbacks;   // Space Invaders
extern const Callbacks G23_Callbacks;   // Turbo Drive (3d racing)
extern const Callbacks G24_Callbacks;   // Minesweeper
extern const Callbacks G25_Callbacks;   // Simon
extern const Callbacks G26_Callbacks;   // Flappy Bird
extern const Callbacks G27_Callbacks;   // Memory
extern const Callbacks G28_Callbacks;   // Sokoban
extern const Callbacks G29_Callbacks;   // Whack-a-mole
extern const Callbacks G30_Callbacks;   // Doodle Jump
extern const Callbacks G31_Callbacks;   // Solitaire
extern const Callbacks G32_Callbacks;   // Pinball
extern const Callbacks G33_Callbacks;   // 15 Puzzle
extern const Callbacks G34_Callbacks;   // Flood-It
extern const Callbacks G35_Callbacks;   // Reversi
extern const Callbacks G36_Callbacks;   // Hangman
extern const Callbacks G37_Callbacks;   // Bomberman
extern const Callbacks G38_Callbacks;   // Tank Fight
extern const Callbacks G39_Callbacks;   // Dino Run
extern const Callbacks G40_Callbacks;   // Number Train (counting game)
extern const Callbacks G41_Callbacks;   // Count It! (counting 1..10)
extern const Callbacks G42_Callbacks;   // ABC Match (letter recognition)
extern const Callbacks G43_Callbacks;   // Shape Hunt (shapes & colours)
extern const Callbacks G44_Callbacks;   // Add It Up (picture addition)
extern const Callbacks G45_Callbacks;   // Big or Small (number comparison)
extern const Callbacks G46_Callbacks;   // Subtract It (picture subtraction)
extern const Callbacks G47_Callbacks;   // Pattern Play (pattern completion)
extern const Callbacks G48_Callbacks;   // What Time? (reading the clock)
extern const Callbacks G49_Callbacks;   // Spot the Diff (finding differences)
extern const Callbacks G50_Callbacks;   // Wagon Order (educational shunting)

extern int breakGame;

#endif//MAIN_LOOP_H_
