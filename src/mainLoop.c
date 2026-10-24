#include "mainLoop.h"

#include "gpu.h"
#include "interface.h"
#include "gameshared.h"

Callbacks _callbacks = { 0, 0, 0, 0 };

int breakGame = 0;

static uint32_t _lastMenuPressTime = 0;
static uint8_t _menuPressArmed = 0;

void SetupCallbacks(const Callbacks * callbacksNew)
{
    // Clear the pressed/released keys buffer
    Keyboard_GetPressedKeys();
    Keyboard_ClearReleases();
    // a switch of the running program always disarms the double press detection
    _menuPressArmed = 0;
    // Maybe a previous game needs to clear the heap
    if (_callbacks.deinitCallback) (*_callbacks.deinitCallback)();
    // Initialize/reset a new game
    if (callbacksNew->initCallback) (*callbacksNew->initCallback)();
    _callbacks = *callbacksNew;
}

void ResetGame()
{
    SetupCallbacks(&_callbacks);
}

/*
 * The MENU switch is the reset button of the console:
 *   - one press            -> restart the running game (or the main menu)
 *   - two presses in a row -> jump back to the main menu
 * The press edges are debounced by gameshared, a single press is acted upon
 * immediately so that the reset does not feel delayed.
 */
static void HandleMenuKey()
{
    if (!Keyboard_TakeKey(KEY_MENU))
        return;
    uint32_t now = IF_GetCurrentTime();
    if (_menuPressArmed && (now - _lastMenuPressTime) < MENU_DOUBLE_PRESS_US)
    {
        SetupCallbacks(&MM_Callbacks);
        return;
    }
    ResetGame();
    _lastMenuPressTime = now;
    _menuPressArmed = 1;
}

static uint32_t _lastDrawTime;

void DoMainLoop()
{
    _lastDrawTime = IF_GetCurrentTime();
    while (1)
    {
        // Register keyboard and touch events
        IF_Keyboard();
        IF_Touchscreen();
        // Reset button: single press restarts, quick double press opens the menu
        HandleMenuKey();
        // Do main loop logic
        if (_callbacks.updateCallback) (*_callbacks.updateCallback)();
        // Render screen at desired FPS
        if (_callbacks.drawCallback)
        {
            uint32_t new_draw_time = IF_GetCurrentTime();
            if ( new_draw_time - _lastDrawTime > (ONE_SECOND/GPU_TARGET_FPS) )
            {
                _lastDrawTime = new_draw_time;
                (*_callbacks.drawCallback)();
                GPU_Render();
            }
        }
        if (breakGame)
            break;
    }
}
