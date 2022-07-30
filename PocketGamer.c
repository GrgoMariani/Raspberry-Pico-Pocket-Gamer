#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "src/gameshared.h"
#include "src/interface.h"
#include "src/mainLoop.h"
#include "src/mainLoop.h"
#include "src/gpu.h"

int main()
{
    stdio_init_all();
    
    IF_Setup();
    GPU_Init();
    SetupCallbacks(&MM_Callbacks);

    DoMainLoop();
    return 0;
}
