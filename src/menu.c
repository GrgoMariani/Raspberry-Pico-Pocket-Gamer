#include "mainLoop.h"
#include "interface.h"
#include "gameshared.h"
#include "gpu.h"
#include "gpu3d.h"

#include <math.h>
#include <string.h>

#include "textures.h"

typedef struct {
    uint8_t _choiceStart;
    uint8_t _choiceCurrent;
    uint8_t _choiceEnd;
    uint8_t screenLines;
} MainScreenMemory;

static MainScreenMemory * const memory = (MainScreenMemory*)(&mainMemory.sharedMemory.memblocks[0]);

typedef struct {
    const char name[14];
    const Callbacks * callbacks;
} GameItem;

static const FontDescription * f_small = &fontDescription[0];
static const FontDescription * f_big = &fontDescription[2];

#define NO_OF_GAMES 23
static const GameItem listOfGames[NO_OF_GAMES] = {
    {"TicTacToe 1P", &G1_Callbacks},
    {"TicTacToe 2P", &G2_Callbacks},
    {"Lights Out",   &G3_Callbacks},
    {"Math",         &G4_Callbacks},
    {"Tetramino",    &G5_Callbacks},
    {"Labirinth",    &G6_Callbacks},
    {"Connect4 1P",  &G7_Callbacks},
    {"Connect4 2P",  &G8_Callbacks},
    {"2048",         &G9_Callbacks},
    {"Paint.NOT",    &G10_Callbacks},
    {"Snake",        &G11_Callbacks},
    {"Calculator",   &G12_Callbacks},
    {"Pong",         &G13_Callbacks},
    {"Breakout",     &G14_Callbacks},
    {"Q*bert",       &G15_Callbacks},
    {"Mission Ctrl", &G16_Callbacks},
    {"Asteroids",    &G17_Callbacks},
    {"Frogger",      &G18_Callbacks},
    {"Tunnel Rush",  &G19_Callbacks},
    {"Missile Def",  &G20_Callbacks},
    {"PacMan",       &G21_Callbacks},
    {"Invaders",     &G22_Callbacks},
    {"Turbo Drive",  &G23_Callbacks},
};

/* layout of the list on the left half of the screen */
#define LIST_X 3
#define LIST_W (GPU_X/2 - 10)
#define LINE_H (f_big->height + 1)
#define LIST_Y 3

/* layout of the panel with the cube on the right half */
#define PANEL_X (GPU_X/2 + 4)
#define PANEL_W (GPU_X/2 - 4)
#define CUBE_CENTER_X (PANEL_X + PANEL_W/2)
#define CUBE_CENTER_Y (GPU_Y/2 - 6)

/*
 * The sprite of the original main screen is a raspberry on a white
 * background. On a 3d cube that white would swallow the shading, so the logo is
 * composited onto a dark blue background once, when the menu starts.
 */
static void BuildCubeTexture(void)
{
    GPU_ClearTexture();
    for (uint8_t y = 0; y < TEXTURE_SIZE; y++)
        for (uint8_t x = 0; x < TEXTURE_SIZE; x++)
        {
            GPU_Color logo = texture_rpi[y*TEXTURE_SIZE + x];
            if (logo == C_WHITE)
            {
                /* dark blue with a subtle vignette */
                uint8_t edge = (uint8_t)((x < 4 || y < 4 || x > TEXTURE_SIZE-5 || y > TEXTURE_SIZE-5) ? 0 : 1);
                Texture_DrawPixel(edge ? RGB(24,28,72) : RGB(60,70,160), x, y);
            }
            else
            {
                Texture_DrawPixel(logo, x, y);
            }
        }
}

static void SetupCubeCamera(void)
{
    GPU3D_Init();
    GPU3D_SetPerspective(CUBE_CENTER_X, CUBE_CENTER_Y, FX(230.0), FX(0.5));
    GPU3D_SetLightDirection(FX(-0.45), FX(0.75), FX(-0.6));
    GPU3D_SetAmbient(110);
}

static void MM_Init(void)
{
    mainMemory._gameState = GS_NOT_STARTED;
	mainMemory._lastRenderedTime = 0;
	mainMemory.touch_X = 0;
	mainMemory.touch_Y = 0;
    memory->_choiceCurrent = 0;
    memory->_choiceStart = 0;
    memory->screenLines = (uint8_t)((GPU_Y - LIST_Y - 2*LINE_H)/LINE_H);
    memory->_choiceEnd = (memory->screenLines - 1) < (NO_OF_GAMES-1) ? (memory->screenLines - 1) : (NO_OF_GAMES -1);
    BuildCubeTexture();
    SetupCubeCamera();
}

static void ScrollTo(uint8_t index)
{
    if (index < memory->_choiceStart)
    {
        memory->_choiceStart = index;
        memory->_choiceEnd = (uint8_t)(memory->_choiceStart + memory->screenLines - 1);
        if (memory->_choiceEnd >= NO_OF_GAMES)
            memory->_choiceEnd = NO_OF_GAMES-1;
    }
    else if (index > memory->_choiceEnd)
    {
        memory->_choiceEnd = index;
        memory->_choiceStart = (uint8_t)(memory->_choiceEnd + 1 - memory->screenLines);
    }
}

static void LaunchSelected(void)
{
    const GameItem * selectedGame = &listOfGames[memory->_choiceCurrent];
    if (selectedGame->callbacks)
        SetupCallbacks(selectedGame->callbacks);
}

static void MM_Update(void)
{
	KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
    if (keyboard & KEY_UP)
    {
        if (memory->_choiceCurrent == 0)
            memory->_choiceCurrent = NO_OF_GAMES - 1;
        else
            memory->_choiceCurrent--;
        ScrollTo(memory->_choiceCurrent);
    }
    if (keyboard & KEY_DOWN)
    {
        memory->_choiceCurrent++;
        if (memory->_choiceCurrent >= NO_OF_GAMES)
            memory->_choiceCurrent = 0;
        ScrollTo(memory->_choiceCurrent);
    }
    if (keyboard & KEY_RIGHT)
    {
        LaunchSelected();
        return;
    }
    /* the list can also be used with the touchscreen */
    if (mainMemory.touchPressedNew && mainMemory.touch_X < GPU_X/2)
    {
        int32_t line = (mainMemory.touch_Y - LIST_Y)/LINE_H;
        if (line >= 0 && line < memory->screenLines)
        {
            uint8_t index = (uint8_t)(memory->_choiceStart + line);
            if (index < NO_OF_GAMES)
            {
                if (index == memory->_choiceCurrent)
                    LaunchSelected();
                else
                    memory->_choiceCurrent = index;
            }
        }
    }
}


static void DrawList(void)
{
    for ( uint8_t i=memory->_choiceStart; i<=memory->_choiceEnd; i++ )
    {
        int16_t y = (int16_t)(LIST_Y + (i - memory->_choiceStart)*LINE_H);
        if ( memory->_choiceCurrent == i )
        {
            GPU_DrawBevelSquare(C_YELLOW, LIST_X-2, y-1, LIST_W, LINE_H);
            GPU_DrawText(C_BLACK, f_big, LIST_X+8, y, listOfGames[i].name, (uint16_t)strlen(listOfGames[i].name));
        }
        else
        {
            GPU_DrawText(C_WHITE, f_big, LIST_X+8, y, listOfGames[i].name, (uint16_t)strlen(listOfGames[i].name));
        }
    }
    /* up/down arrows when the list continues outside of the screen */
    if (memory->_choiceStart > 0)
        GPU_DrawFilledTriangle(C_GRAY_MEDIUM, GPU_X/2-12, LIST_Y+2, GPU_X/2-4, LIST_Y+2, GPU_X/2-8, LIST_Y-3);
    if (memory->_choiceEnd < NO_OF_GAMES-1)
    {
        int16_t y = (int16_t)(LIST_Y + memory->screenLines*LINE_H);
        GPU_DrawFilledTriangle(C_GRAY_MEDIUM, GPU_X/2-12, y-4, GPU_X/2-4, y-4, GPU_X/2-8, y+1);
    }
}

static void DrawCubePanel(void)
{
    GPU_DrawVerticalGradient(RGB(16,20,64), C_BLACK, PANEL_X, 0, PANEL_W, GPU_Y);
    GPU_DrawLine(C_GRAY_DARK, PANEL_X-2, 0, PANEL_X-2, GPU_Y-1);

    GPU_DrawTextCentered(C_C1, f_big, PANEL_X + PANEL_W/2, 10, "POCKET");
    GPU_DrawTextCentered(C_C1, f_big, PANEL_X + PANEL_W/2, 10 + f_big->height + 1, "GAMER");

    /*
     * The rotating cube: the raspberry sprite of the old main screen is used as
     * the texture of all six faces, so it is the same logo, only in 3d now.
     */
    uint32_t ms = IF_GetCurrentTime()/1000;
    int32_t angleY = (int32_t)((ms * ANG_FULL) / 5000);
    int32_t angleX = (int32_t)((ms * ANG_FULL) / 11000);
    Vec3 position = GPU3D_Vec(0, 0, FX(3.2));
    Vec3 scale = GPU3D_Vec(FX(1.15), FX(1.15), FX(1.15));
    GPU3D_DrawMesh(&GPU3D_TexturedCubeMesh, &position, &scale, angleX, angleY, 0, 0);

    GPU_DrawTextCentered(C_GRAY_LIGHT, f_small, PANEL_X + PANEL_W/2, GPU_Y - 42, "MENU 1x = reset game");
    GPU_DrawTextCentered(C_GRAY_LIGHT, f_small, PANEL_X + PANEL_W/2, GPU_Y - 42 + f_small->height + 2, "MENU 2x = this menu");
    GPU_DrawTextCentered(C_GRAY_MEDIUM, f_small, PANEL_X + PANEL_W/2, GPU_Y - 14, "UP/DOWN + RIGHT or touch");
}

static void MM_Draw(void)
{
    GPU_ClearFramebuffers();
    DrawList();
    SetupCubeCamera();
    DrawCubePanel();
}

static void MM_Deinit(void)
{

}

const Callbacks MM_Callbacks = { &MM_Init, &MM_Update, &MM_Draw, &MM_Deinit };
