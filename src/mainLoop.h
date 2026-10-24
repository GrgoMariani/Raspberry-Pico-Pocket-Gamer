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

extern int breakGame;

#endif//MAIN_LOOP_H_
