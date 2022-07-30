MAKEFILE_BUILDER = make -f makefile_builder.mk --no-print-directory


all: game_sources


GAME_SOURCES_CPP := main.cpp
GAME_SOURCES_C := interface.c ../src/menu.c ../src/gameshared.c ../src/gpu.c ../src/mainLoop.c ../src/textures.c  \
	../src/games/game01.c ../src/games/game02.c ../src/games/game03.c ../src/games/game04.c ../src/games/game05.c ../src/games/game06.c \
	../src/games/game07.c ../src/games/game08.c ../src/games/game09.c ../src/games/game10.c ../src/games/game11.c ../src/games/game12.c

olcPixelGameEngine.h:
	git clone https://github.com/OneLoneCoder/olcPixelGameEngine.git
	mv olcPixelGameEngine/olcPixelGameEngine.h $@
	rm -rf olcPixelGameEngine

game_sources: olcPixelGameEngine.h
	@$(MAKEFILE_BUILDER) CPP_TO_COMPILE="$(GAME_SOURCES_CPP)" C_TO_COMPILE="$(GAME_SOURCES_C)" EXE_TARGET="exe"

clean:
	rm -rf build

run: game_sources
	./build/exe