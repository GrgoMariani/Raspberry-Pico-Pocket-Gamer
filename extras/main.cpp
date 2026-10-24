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
                // GetPixel() returns plain (not byte swapped) RGB565
				RGB_Pixel pixel = {
					static_cast<uint8_t>((result & 0xf800)>>8),
					static_cast<uint8_t>((result & 0x07e0)>>3),
					static_cast<uint8_t>((result & 0x001f)<<3)
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
	if (gDeviceOutput.GetKey(olc::Key::Q).bHeld)	result |= KEY_MENU;
	if (gDeviceOutput.GetKey(olc::Key::W).bHeld)	result |= KEY_UP;
	if (gDeviceOutput.GetKey(olc::Key::A).bHeld)	result |= KEY_LEFT;
	if (gDeviceOutput.GetKey(olc::Key::S).bHeld)	result |= KEY_DOWN;
	if (gDeviceOutput.GetKey(olc::Key::D).bHeld)	result |= KEY_RIGHT;
	return result;
}

#include "gameshared.h"

extern "C" void GetPressedMouse(uint8_t * pressed, uint16_t * x, uint16_t * y)
{
	if (gDeviceOutput.GetMouse(0).bHeld)
	{
		*pressed = 1;
		int mouseX = gDeviceOutput.GetMouseX();
		int mouseY = gDeviceOutput.GetMouseY();
		*x = static_cast<uint16_t>(mouseX < 0 ? 0 : (mouseX >= GPU_X ? GPU_X-1 : mouseX));
		*y = static_cast<uint16_t>(mouseY < 0 ? 0 : (mouseY >= GPU_Y ? GPU_Y-1 : mouseY));
	}
	else
	{
		*pressed = 0;
	}
}


void *CreateGUIAsync(void *threadarg)
{
    std::cout << "Starting mainLoop in async " << std::endl;
    (void)threadarg;

	if (gDeviceOutput.Construct(GPU_X, GPU_Y, 4, 4))
    {
        gDeviceOutput.Start();
    }

    pthread_exit(NULL);
}


int main(int argc, char *argv[])
{
    pthread_t thread;

    (void)argc;
    (void)argv;

    IF_Setup();
    GPU_Init();
    SetupCallbacks(&MM_Callbacks);

    // set another thread for GUI...
    pthread_create(&thread, NULL, CreateGUIAsync, NULL);

    DoMainLoop();
	return 0;
}
