#include "mainLoop.h"

#include "gpu.h"
#include "interface.h"
#include "gameshared.h"

Callbacks _callbacks = { 0, 0, 0, 0 };

int breakGame = 0;

void SetupCallbacks(const Callbacks * callbacksNew)
{
    // Clear the pressed keys buffer
    Keyboard_GetPressedKeys();
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

static uint32_t _lastDrawTime;

void DoMainLoop()
{
    KeyPressedEnum keys_held;
    _lastDrawTime = IF_GetCurrentTime();
    while (1)
    {
        // Register keyboard and touch events
        IF_Keyboard();
        IF_Touchscreen();
        // Reset to main menu
        if (Keyboard_GetHeldKeys() & KEY_MENU)
        {
            SetupCallbacks(&MM_Callbacks);
        }
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
        };
        if (breakGame)
            break;
    }
}