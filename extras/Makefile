MAKEFILE_BUILDER = make -f makefile_builder.mk --no-print-directory


all: game_sources


GAME_SOURCES_CPP := main.cpp
GAME_SOURCES_C := interface.c ../src/menu.c ../src/games/tictactoe.c ../src/gameshared.c ../src/gpu.c ../src/gpu3d.c ../src/mainLoop.c \
	../src/textures.c ../src/touch.c \
	../src/games/game01.c ../src/games/game02.c ../src/games/game03.c ../src/games/game04.c ../src/games/game05.c ../src/games/game06.c \
	../src/games/game07.c ../src/games/game08.c ../src/games/game09.c ../src/games/game10.c ../src/games/game11.c ../src/games/game12.c \
	../src/games/game13.c ../src/games/game14.c ../src/games/game15.c ../src/games/game16.c ../src/games/game17.c \
	../src/games/game18.c ../src/games/game19.c ../src/games/game20.c ../src/games/game21.c ../src/games/game22.c \
	../src/games/game23.c

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

# every source the host builds need (the emulator and the test runner)
HOST_SOURCES := interface.c ../src/menu.c ../src/gameshared.c ../src/gpu.c ../src/gpu3d.c \
	../src/mainLoop.c ../src/textures.c ../src/touch.c ../src/games/tictactoe.c \
	$(wildcard ../src/games/game*.c)

# host test runner: unit tests plus a smoke test of every game
test:
	@mkdir -p build
	gcc -std=c11 -Wall -Wextra -Wshadow -g -O1 -I../src -I. -o build/tests tests/tests.c \
		$(HOST_SOURCES) -lm -lpthread
	./build/tests

# the same runner with the sanitizers, catches out of bounds accesses
test-asan:
	@mkdir -p build
	gcc -std=c11 -Wall -Wextra -Wshadow -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
		-I../src -I. -o build/tests_asan tests/tests.c \
		$(HOST_SOURCES) -lm -lpthread
	./build/tests_asan

# render one frame of a game/menu into a ppm file: make shot NAME=qbert FRAMES=120
NAME ?= menu
FRAMES ?= 120
MODE ?= random
shot:
	@mkdir -p build
	gcc -std=c11 -Wall -Wextra -g -O2 -I../src -I. -o build/shot tests/tests.c \
		$(HOST_SOURCES) -lm -lpthread
	./build/shot shot $(NAME) build/$(NAME).ppm $(FRAMES) $(MODE)
	@python3 tests/ppm2png.py build/$(NAME).ppm build/$(NAME).png