#include <stdio.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <pthread.h>

#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

extern "C"
{
    #include "interface.h"
    #include "mainLoop.h"
    #include "gpu.h"
    
    GPU_Color GetPixel(uint16_t x, uint16_t y);

	typedef struct RGB_Pixel {
		uint8_t r;
		uint8_t g;
		uint8_t b;
	} RGB_Pixel;
}

class GameDevice : public olc::PixelGameEngine
{
public:
	GameDevice()
	{
		sAppName = "PocketGamer";
	}

public:
	bool OnUserCreate() override
	{
		// Called once at the start, so create things here
		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		// called once per frame
		for (uint16_t x = 0; x < ScreenWidth(); x++)
			for (uint16_t y = 0; y < ScreenHeight(); y++)
            {
                GPU_Color result = GetPixel(x, y);
				RGB_Pixel pixel = {
					(result & 0xf800)>>(11-3),
					(result & 0x07e0)>>(5-2),
					(result & 0x001f)<<3
				};
				Draw(x, y, olc::Pixel(pixel.r, pixel.g, pixel.b));
            }
		return true;
	}
};

GameDevice gDeviceOutput;

extern "C" int GetPressedKey()
{
	int result = KEY_NONE;
	if (gDeviceOutput.GetKey(olc::Q).bHeld)	result |= KEY_MENU;
	if (gDeviceOutput.GetKey(olc::W).bHeld)	result |= KEY_UP;
	if (gDeviceOutput.GetKey(olc::A).bHeld)	result |= KEY_LEFT;
	if (gDeviceOutput.GetKey(olc::S).bHeld)	result |= KEY_DOWN;
	if (gDeviceOutput.GetKey(olc::D).bHeld)	result |= KEY_RIGHT;
	return result;
}

#include "gameshared.h"

extern "C" void GetPressedMouse()
{
	 if (gDeviceOutput.GetMouse(0).bHeld)
	 {
		 mainMemory.touchPressed = 1;
		 mainMemory.touch_X = gDeviceOutput.GetMouseX();
		 mainMemory.touch_Y = gDeviceOutput.GetMouseY();
	 }
	 else
	 {
		 mainMemory.touchPressed = 0;
	 }
}


void *CreateGUIAsync(void *threadarg)
{
    std::cout << "Starting mainLoop in async " << std::endl;

	if (gDeviceOutput.Construct(GPU_X, GPU_Y, 4, 4))
    {
        gDeviceOutput.Start();
    }

    pthread_exit(NULL);
}


int main(int argc, char *argv[])
{
    pthread_t thread;


    IF_Setup();
    GPU_Init();
    SetupCallbacks(&MM_Callbacks);

    // set another thread for GUI...
    pthread_create(&thread, NULL, CreateGUIAsync, NULL);

    DoMainLoop();
	return 0;
}
