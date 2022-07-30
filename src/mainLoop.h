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

extern const Callbacks MM_Callbacks;
extern const Callbacks G1_Callbacks;
extern const Callbacks G2_Callbacks;
extern const Callbacks G3_Callbacks;
extern const Callbacks G4_Callbacks;
extern const Callbacks G5_Callbacks;
extern const Callbacks G6_Callbacks;
extern const Callbacks G7_Callbacks;
extern const Callbacks G8_Callbacks;
extern const Callbacks G9_Callbacks;
extern const Callbacks G10_Callbacks;
extern const Callbacks G11_Callbacks;
extern const Callbacks G12_Callbacks;

extern int breakGame;

#endif//MAIN_LOOP_H_
