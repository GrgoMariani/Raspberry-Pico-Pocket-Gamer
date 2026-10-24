# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

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
        src/gpu3d.c
        src/interface.c
        src/touch.c
        src/mainLoop.c
        src/menu.c
        src/textures.c
        src/games/tictactoe.c
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
        src/games/game14.c
        src/games/game15.c
        src/games/game16.c
        src/games/game17.c
        src/games/game18.c
        src/games/game19.c
        src/games/game20.c
        src/games/game21.c
        src/games/game22.c
        src/games/game23.c
        )

# Warnings for the project sources (SDK sources keep their own flags)

# Warnings for the project sources (the SDK keeps its own flags)
target_compile_options(PocketGamer PRIVATE -Wall -Wextra -Wshadow)

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

