# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
## Uncomment and set custom path if needed
# set(PICO_SDK_PATH "$(set_path_here)/pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(PocketGamer C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(PocketGamer 
        PocketGamer.c
        src/interface/ili9341.c
        src/interface/ili9341_touchscreen.c
        src/gameshared.c
        src/gpu.c
        src/interface.c
        src/mainLoop.c
        src/menu.c
        src/textures.c
        src/games/game01.c
        src/games/game02.c
        src/games/game03.c
        src/games/game04.c
        src/games/game05.c
        src/games/game06.c
        src/games/game07.c
        src/games/game08.c
        src/games/game09.c
        src/games/game10.c
        src/games/game11.c
        src/games/game12.c
        src/games/game13.c
        )

pico_set_program_name(PocketGamer "PocketGamer")
pico_set_program_version(PocketGamer "0.1")

pico_enable_stdio_uart(PocketGamer 0)
pico_enable_stdio_usb(PocketGamer 1)

# Add the standard library to the build
target_link_libraries(PocketGamer pico_stdlib)

# Add any user requested libraries
target_link_libraries(PocketGamer
        hardware_spi
        )

pico_add_extra_outputs(PocketGamer)

