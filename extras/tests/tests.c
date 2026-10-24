/*
 * Test runner for the pocket gamer.
 *
 * It links the real sources (gpu, gpu3d, touch, gameshared, mainLoop, menu and
 * every game) against the host implementation of the platform layer, so the
 * very same code that runs on the Pico is exercised here.
 *
 *   ./tests            run every test
 *   ./tests shot NAME OUT.ppm [FRAMES]
 *                      render FRAMES frames of a game/menu and dump the screen
 *
 * Build with -fsanitize=address,undefined to catch out of bounds accesses.
 */
#define _DEFAULT_SOURCE     /* usleep() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#include "host_hooks.h"
#include "interface.h"
#include "gameshared.h"
#include "gpu.h"
#include "gpu3d.h"
#include "mainLoop.h"
#include "touch.h"
#include "textures.h"

/* ------------------------------------------------------------------ input */

static int _simKeys = KEY_NONE;
static uint8_t _simTouch = 0;
static uint16_t _simTouchX = 0;
static uint16_t _simTouchY = 0;

int GetPressedKey(void)
{
    return _simKeys;
}

void GetPressedMouse(uint8_t * pressed, uint16_t * x, uint16_t * y)
{
    *pressed = _simTouch;
    *x = _simTouchX;
    *y = _simTouchY;
}

static void setKeys(int keys)
{
    _simKeys = keys;
}

static void setTouch(uint8_t pressed, uint16_t x, uint16_t y)
{
    _simTouch = pressed;
    _simTouchX = x;
    _simTouchY = y;
}

/* the main loop under test lives in mainLoop.c */
extern Callbacks _callbacks;

/* ------------------------------------------------------------- reporting */

static int _checks = 0;
static int _failures = 0;
static const char * _currentTest = "";

#define CHECK(cond, fmt, ...)                                                  \
    do {                                                                       \
        _checks++;                                                             \
        if (!(cond)) {                                                         \
            _failures++;                                                       \
            printf("  FAIL [%s] %s:%d " fmt "\n", _currentTest, __FILE__,      \
                   __LINE__, ##__VA_ARGS__);                                   \
        }                                                                      \
    } while (0)

#define BEGIN_TEST(name) do { _currentTest = name; printf("-- %s\n", name); } while (0)

/* ------------------------------------------------------------ frame tools */

/* GetPixel() of the host layer returns plain (not byte swapped) RGB565, so the
   pixel format macros of gpu.h must not be used on it */
#define PIX_R5(c) ((uint8_t)(((c) >> 11) & 0x1F))
#define PIX_G6(c) ((uint8_t)(((c) >> 5) & 0x3F))
#define PIX_B5(c) ((uint8_t)((c) & 0x1F))

/* the games draw into the gpu framebuffer, GetPixel() reads the copy that
   IF_DrawScreen() made, so every read has to be preceded by a render */
static void flush(void)
{
    GPU_Render();
}

static uint32_t frameHash(void)
{
    flush();
    uint32_t hash = 2166136261u;
    for (uint16_t x = 0; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++)
        {
            hash ^= GetPixel(x, y);
            hash *= 16777619u;
        }
    return hash;
}

static uint32_t countPixels(GPU_Color color)
{
    flush();
    uint32_t n = 0;
    for (uint16_t x = 0; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++)
            if (GetPixel(x, y) == color)
                n++;
    return n;
}

static uint32_t countNonBlack(void)
{
    flush();
    uint32_t n = 0;
    for (uint16_t x = 0; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++)
            if (GetPixel(x, y) != 0)
                n++;
    return n;
}

static void savePpm(const char * path)
{
    flush();
    FILE * f = fopen(path, "wb");
    if (!f)
    {
        printf("cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", GPU_X, GPU_Y);
    for (uint16_t y = 0; y < GPU_Y; y++)
        for (uint16_t x = 0; x < GPU_X; x++)
        {
            GPU_Color c = GetPixel(x, y);
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((c >> 11) << 3);
            rgb[1] = (unsigned char)(((c >> 5) & 0x3f) << 2);
            rgb[2] = (unsigned char)((c & 0x1f) << 3);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* -------------------------------------------------------- simulation step */

#define FRAME_US (ONE_SECOND/GPU_TARGET_FPS)

/* one iteration of what DoMainLoop() does, with the virtual clock advanced by
   one frame so that the games run at their nominal speed */
static void stepFrame(void)
{
    Host_AdvanceVirtualTime(FRAME_US);
    IF_Keyboard();
    IF_Touchscreen();
    if (_callbacks.updateCallback) (*_callbacks.updateCallback)();
    if (_callbacks.drawCallback)
    {
        (*_callbacks.drawCallback)();
        GPU_Render();
    }
}

/* one press of a key: long enough to survive the debounce, short enough to be
   a single press */
static void tapKey(int keys)
{
    setKeys(keys);
    for (int i = 0; i < 3; i++) stepFrame();
    setKeys(KEY_NONE);
    for (int i = 0; i < 3; i++) stepFrame();
}

static uint32_t _rngState = 12345;
static uint32_t testRandom(void)
{
    uint32_t x = _rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    _rngState = x;
    return x;
}

/* drive a game with pseudo random input, the way a child would mash buttons */
static void runFramesRandom(const Callbacks * cb, int frames)
{
    SetupCallbacks(cb);
    for (int i = 0; i < frames; i++)
    {
        if ((i % 7) == 0)
        {
            int keys = 0;
            if (testRandom() & 1) keys |= KEY_UP;
            if (testRandom() & 1) keys |= KEY_DOWN;
            if (testRandom() & 1) keys |= KEY_LEFT;
            if (testRandom() & 1) keys |= KEY_RIGHT;
            setKeys(keys);
        }
        if ((i % 11) == 0)
        {
            if (testRandom() & 1)
                setTouch(1, (uint16_t)(testRandom() % GPU_X), (uint16_t)(testRandom() % GPU_Y));
            else
                setTouch(0, 0, 0);
        }
        stepFrame();
    }
    setKeys(KEY_NONE);
    setTouch(0, 0, 0);
}

/* just let the game run on its own, no input at all */
static void runFramesIdle(const Callbacks * cb, int frames)
{
    SetupCallbacks(cb);
    for (int i = 0; i < frames; i++)
        stepFrame();
}

/* play with a fixed rotation of single key presses, builds up a real board */
static void runFramesCycle(const Callbacks * cb, int frames)
{
    static const int order[4] = { KEY_LEFT, KEY_DOWN, KEY_RIGHT, KEY_UP };
    SetupCallbacks(cb);
    for (int i = 0; i < frames; i++)
    {
        tapKey(order[(i/3) & 3]);
    }
}

/* ===================================================================== */

static void test_rgb_format(void)
{
    BEGIN_TEST("rgb565 byte swapped packing");
    /* independent reference: byte0 = RRRRRGGG, byte1 = GGGBBBBB (what the panel
       receives first is the low byte of the little endian uint16) */
    struct { uint8_t r, g, b; uint16_t expected; } cases[] = {
        { 255, 255, 255, 0xFFFF },
        {   0,   0,   0, 0x0000 },
        { 255,   0,   0, 0x00F8 },
        {   0, 255,   0, 0xE007 },
        {   0,   0, 255, 0x1F00 },
        { 255, 255,   0, 0xE0FF },
        {   0, 255, 255, 0xFF07 },
        { 255,   0, 255, 0x1FF8 },
        { 112, 168,  40, 0x4575 },   /* the green of the raspberry sprite */
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
    {
        uint16_t got = RGB(cases[i].r, cases[i].g, cases[i].b);
        CHECK(got == cases[i].expected, "RGB(%d,%d,%d) = 0x%04x, expected 0x%04x",
              cases[i].r, cases[i].g, cases[i].b, got, cases[i].expected);
    }
    /* the accessors have to undo the packing */
    uint16_t c = RGB(112, 168, 40);
    CHECK(GPU_GET_R5(c) == (112 >> 3), "R5 %d", GPU_GET_R5(c));
    CHECK(GPU_GET_G6(c) == (168 >> 2), "G6 %d", GPU_GET_G6(c));
    CHECK(GPU_GET_B5(c) == (40 >> 3), "B5 %d", GPU_GET_B5(c));

    /* the raspberry sprite in textures.c must be the same green */
    CHECK(texture_rpi[5*64+17] == RGB(112,168,40) || texture_rpi[5*64+17] == 0x4575,
          "sprite green is 0x%04x", texture_rpi[5*64+17]);

    /* shading */
    CHECK(GPU_ShadeColor(C_WHITE, 256) == C_WHITE, "shade 256 changes white");
    CHECK(GPU_ShadeColor(C_WHITE, 0) == C_BLACK, "shade 0 is not black");
    CHECK(GPU_ShadeColor(C_WHITE, 128) != C_WHITE, "shade 128 does nothing");
    GPU_Color half = GPU_ShadeColor(RGB(200, 100, 50), 128);
    CHECK(abs((int)GPU_GET_R5(half)*8 - 100) <= 12, "shaded red %d", GPU_GET_R5(half)*8);
}

static void test_font_rendering(void)
{
    BEGIN_TEST("font glyph columns");
    /* Font16x26 uses all 16 bits of a row, so both the leftmost (bit 15) and
       the rightmost (bit 0) column of a glyph have to end up on screen. The
       old code shifted by 16-j, which dropped the last column of every glyph
       and left the first one empty. */
    const FontDescription * big = &fontDescription[3];
    GPU_ClearFramebuffers();
    GPU_DrawLetter(C_WHITE, big, 0, 0, '#');      /* row 13 of '#' is 0xFFFF */
    flush();
    CHECK(GetPixel(0, 13) != 0, "leftmost column of '#' is not drawn");
    CHECK(GetPixel(big->width-1, 13) != 0, "rightmost column of '#' is not drawn");
    CHECK(GetPixel(big->width, 13) == 0, "glyph spills into the next cell");

    /* every glyph of every font has to stay inside its cell */
    for (int f = 0; f < 4; f++)
    {
        const FontDescription * fd = &fontDescription[f];
        for (char ch = 32; ch < 127; ch++)
        {
            GPU_ClearFramebuffers();
            GPU_DrawLetter(C_WHITE, fd, 10, 10, ch);
            flush();
            for (int x = 0; x < GPU_X; x++)
                for (int y = 0; y < GPU_Y; y++)
                {
                    if (GetPixel((uint16_t)x, (uint16_t)y) == 0)
                        continue;
                    CHECK(x >= 10 && x < 10+fd->width && y >= 10 && y < 10+fd->height,
                          "font %d letter '%c' paints outside of its cell at %d,%d",
                          f, ch, x, y);
                    x = GPU_X; y = GPU_Y;   /* one report per glyph is enough */
                }
        }
    }
    /* control characters must not read outside of the font table */
    GPU_ClearFramebuffers();
    GPU_DrawLetter(C_WHITE, big, 0, 0, (char)1);
    GPU_DrawLetter(C_WHITE, big, 0, 0, (char)200);
    CHECK(1, "control characters survived");
}

static void test_line_clipping(void)
{
    BEGIN_TEST("line clipping");
    GPU_ClearFramebuffers();
    /* a line that leaves the screen on the right has to keep its slope: the
       old code clamped the end point, which turned it into a vertical line */
    GPU_DrawLine(C_WHITE, 200, 0, 400, 200);
    flush();
    CHECK(GetPixel(250, 50) != 0, "clipped diagonal missing at 250,50");
    CHECK(GetPixel(251, 50) == 0, "clipped diagonal is not 1px wide");
    CHECK(GetPixel(319, 119) != 0, "diagonal does not reach the right border");

    GPU_ClearFramebuffers();
    GPU_DrawLine(C_WHITE, -100, 120, 100, 120);
    flush();
    CHECK(GetPixel(0, 120) != 0 && GetPixel(100, 120) != 0, "negative start not clipped");
    CHECK(GetPixel(0, 121) == 0, "horizontal line is not horizontal");

    /* completely outside */
    GPU_ClearFramebuffers();
    GPU_DrawLine(C_WHITE, -50, -50, -10, -10);
    GPU_DrawLine(C_WHITE, 400, 300, 500, 400);
    CHECK(countNonBlack() == 0, "invisible line painted %u pixels", countNonBlack());

    /* filled shapes outside of the screen must not crash nor paint */
    GPU_ClearFramebuffers();
    GPU_DrawFilledSquare(C_WHITE, -20, -20, 40, 40);
    flush();
    CHECK(GetPixel(0, 0) != 0 && GetPixel(19, 19) != 0, "clipped square wrong");
    CHECK(GetPixel(20, 20) == 0, "clipped square too big");
    GPU_DrawFilledCircle(C_RED, 0, 0, 30);
    GPU_DrawFilledCircle(C_RED, 319, 239, 30);
    GPU_DrawEmptyCircle(C_RED, -5, -5, 3);
    CHECK(1, "circles at the border survived");
}

static void test_touch_mapping(void)
{
    BEGIN_TEST("touch coordinate mapping");
    uint16_t x = 12345, y = 12345;
    uint16_t rawx[TOUCH_SAMPLES_MAX], rawy[TOUCH_SAMPLES_MAX];

    /* too few samples -> no touch */
    rawx[0] = 16000; rawy[0] = 16000;
    CHECK(Touch_ComputeCoordinates(rawx, rawy, 1, &x, &y) == 0, "single sample accepted");
    CHECK(Touch_ComputeCoordinates(rawx, rawy, 0, &x, &y) == 0, "zero samples accepted");

    /* the calibrated ends map onto the first and the last pixel */
    for (int i = 0; i < TOUCH_SAMPLES_MAX; i++) { rawx[i] = 1500; rawy[i] = 3276; }
    CHECK(Touch_ComputeCoordinates(rawx, rawy, TOUCH_SAMPLES_MAX, &x, &y) == 1, "min rejected");
    CHECK(x == 0 && y == 0, "min maps to %d,%d", x, y);
    for (int i = 0; i < TOUCH_SAMPLES_MAX; i++) { rawx[i] = 31000; rawy[i] = 30110; }
    CHECK(Touch_ComputeCoordinates(rawx, rawy, TOUCH_SAMPLES_MAX, &x, &y) == 1, "max rejected");
    CHECK(x == TOUCH_SCREEN_X-1 && y == TOUCH_SCREEN_Y-1, "max maps to %d,%d (must be %d,%d)",
          x, y, TOUCH_SCREEN_X-1, TOUCH_SCREEN_Y-1);

    /* the middle of the panel is the middle of the screen */
    for (int i = 0; i < TOUCH_SAMPLES_MAX; i++) { rawx[i] = 16250; rawy[i] = 16693; }
    Touch_ComputeCoordinates(rawx, rawy, TOUCH_SAMPLES_MAX, &x, &y);
    CHECK(x >= 158 && x <= 161 && y >= 118 && y <= 121, "middle maps to %d,%d", x, y);

    /* out of range raw values clamp instead of wrapping around */
    for (int i = 0; i < TOUCH_SAMPLES_MAX; i++) { rawx[i] = 65535; rawy[i] = 1; }
    Touch_ComputeCoordinates(rawx, rawy, TOUCH_SAMPLES_MAX, &x, &y);
    CHECK(x == TOUCH_SCREEN_X-1, "over range x = %d", x);
    CHECK(y == 0, "under range y = %d", y);

    /* one wild sample must not move the result: the old plain average moved by
       ~25% of the screen for a single glitch */
    for (int i = 0; i < TOUCH_SAMPLES_MAX; i++) { rawx[i] = 16250; rawy[i] = 16693; }
    rawx[3] = 60000; rawy[5] = 500;
    Touch_ComputeCoordinates(rawx, rawy, TOUCH_SAMPLES_MAX, &x, &y);
    CHECK(x >= 150 && x <= 170, "glitch moved x to %d", x);
    CHECK(y >= 110 && y <= 130, "glitch moved y to %d", y);

    /* a partial burst (finger lifted while sampling) is still usable */
    for (int i = 0; i < 3; i++) { rawx[i] = 16250; rawy[i] = 16693; }
    CHECK(Touch_ComputeCoordinates(rawx, rawy, 3, &x, &y) == 1, "3 samples rejected");
    CHECK(x >= 158 && x <= 161, "3 samples map to %d", x);

    /* the y axis must use its own calibration (a copy/paste of the x minimum
       made small y values underflow and jump to the other end) */
    for (int i = 0; i < TOUCH_SAMPLES_MAX; i++) { rawx[i] = 16250; rawy[i] = 2000; }
    Touch_ComputeCoordinates(rawx, rawy, TOUCH_SAMPLES_MAX, &x, &y);
    CHECK(y == 0, "raw y below the y minimum maps to %d instead of 0", y);
}

static void test_touch_filtering(void)
{
    BEGIN_TEST("touch press edges and jitter filter");
    Touch_Update(0, 0, 0);
    CHECK(mainMemory.touchPressed == 0, "pressed without a touch");
    Touch_Update(1, 100, 100);
    CHECK(mainMemory.touchPressedNew == 1, "no press edge");
    CHECK(mainMemory.touch_X == 100 && mainMemory.touch_Y == 100, "press position %d,%d",
          mainMemory.touch_X, mainMemory.touch_Y);
    Touch_Update(1, 101, 101);
    CHECK(mainMemory.touchPressedNew == 0, "press edge repeats while held");
    CHECK(mainMemory.touch_X >= 100 && mainMemory.touch_X <= 101, "filtered x %d", mainMemory.touch_X);
    /* jitter of a few pixels has to be damped */
    for (int i = 0; i < 8; i++)
        Touch_Update(1, (uint16_t)(100 + (i & 1)), 100);
    CHECK(mainMemory.touch_X == 100, "jitter not filtered: %d", mainMemory.touch_X);
    /* a real movement is followed immediately */
    Touch_Update(1, 180, 180);
    CHECK(mainMemory.touch_X == 180 && mainMemory.touch_Y == 180, "jump filtered away: %d,%d",
          mainMemory.touch_X, mainMemory.touch_Y);
    Touch_Update(0, 180, 180);
    CHECK(mainMemory.touchReleasedNew == 1 && mainMemory.touchPressed == 0, "no release edge");
    Touch_Update(0, 180, 180);
    CHECK(mainMemory.touchReleasedNew == 0, "release edge repeats");
}

static void test_keyboard_debounce(void)
{
    BEGIN_TEST("keyboard debounce and press latch");
    Host_SetVirtualTime(1000000);
    setKeys(KEY_NONE);
    IF_Keyboard();
    Keyboard_GetPressedKeys();

    /* a bounce must not produce two presses */
    setKeys(KEY_UP);
    IF_Keyboard();
    CHECK((Keyboard_GetHeldKeys() & KEY_UP) == 0, "key is held before it is debounced");
    Host_AdvanceVirtualTime(2000);
    setKeys(KEY_NONE);          /* bounce */
    IF_Keyboard();
    setKeys(KEY_UP);
    IF_Keyboard();
    Host_AdvanceVirtualTime(KEY_DEBOUNCE_US);
    IF_Keyboard();
    KeyPressedEnum pressed = Keyboard_GetPressedKeys();
    CHECK((pressed & KEY_UP) != 0, "debounced press not reported");
    CHECK((Keyboard_GetHeldKeys() & KEY_UP) != 0, "key not held after debouncing");
    pressed = Keyboard_GetPressedKeys();
    CHECK((pressed & KEY_UP) == 0, "press latch not cleared");

    /* releasing and pressing again gives exactly one new press */
    setKeys(KEY_NONE);
    IF_Keyboard();
    CHECK((Keyboard_GetHeldKeys() & KEY_UP) == 0, "key still held after release");
    setKeys(KEY_UP);
    IF_Keyboard();
    Host_AdvanceVirtualTime(KEY_DEBOUNCE_US);
    IF_Keyboard();
    pressed = Keyboard_GetPressedKeys();
    CHECK((pressed & KEY_UP) != 0, "second press not reported");

    /* taking a single key leaves the others in the latch */
    setKeys(KEY_LEFT | KEY_RIGHT);
    IF_Keyboard();
    Host_AdvanceVirtualTime(KEY_DEBOUNCE_US);
    IF_Keyboard();
    CHECK(Keyboard_TakeKey(KEY_LEFT) == 1, "KEY_LEFT not taken");
    CHECK(Keyboard_TakeKey(KEY_LEFT) == 0, "KEY_LEFT taken twice");
    pressed = Keyboard_GetPressedKeys();
    CHECK((pressed & KEY_RIGHT) != 0, "KEY_RIGHT was consumed by TakeKey");
    CHECK((pressed & KEY_LEFT) == 0, "KEY_LEFT still in the latch");
    setKeys(KEY_NONE);
    IF_Keyboard();
}

static void test_random_quality(void)
{
    BEGIN_TEST("random generator quality");
    Random_Seed(0x12345678);
    /* the old generator returned a quadratic function of the microsecond
       counter, whose bit0 alternated with every call. Games that used
       "IF_Random() & 1" (snake fruit, connect4 who starts, ...) therefore
       alternated between two values forever. */
    int lastBit = -1, alternations = 0;
    int buckets[8] = {0};
    for (int i = 0; i < 8000; i++)
    {
        uint32_t r = Random_Next();
        int bit = (int)(r & 1);
        if (lastBit >= 0 && bit != lastBit)
            alternations++;
        lastBit = bit;
        buckets[r % 8]++;
    }
    CHECK(alternations > 3000 && alternations < 5000,
          "bit0 alternates %d times out of 8000 (a fair coin gives ~4000)", alternations);
    for (int i = 0; i < 8; i++)
        CHECK(buckets[i] > 700 && buckets[i] < 1300, "bucket %d has %d of 8000", i, buckets[i]);

    /* different seeds give different sequences */
    Random_Seed(1);
    uint32_t a = Random_Next();
    Random_Seed(2);
    uint32_t b = Random_Next();
    CHECK(a != b, "seeds 1 and 2 produce the same value");
    Random_Seed(0);      /* a zero seed must not lock the generator at zero */
    CHECK(Random_Next() != 0, "zero seed produces zero");
}

static void test_gpu3d_math(void)
{
    BEGIN_TEST("3d math");
    for (int32_t a = 0; a < ANG_FULL; a += 7)
    {
        fixed s = GPU3D_Sin(a), c = GPU3D_Cos(a);
        CHECK(s <= FX_ONE && s >= -FX_ONE, "sin(%d) = %d out of range", a, s);
        int64_t sum = ((int64_t)s*s + (int64_t)c*c) >> 16;
        CHECK(llabs((long long)sum - FX_ONE) < 400, "sin^2+cos^2 = %ld at %d", (long)sum, a);
    }
    CHECK(abs(GPU3D_Sin(ANG_FULL/4) - FX_ONE) < 8, "sin(90deg) = %d", GPU3D_Sin(ANG_FULL/4));
    CHECK(abs(GPU3D_Sin(0)) < 8, "sin(0) = %d", GPU3D_Sin(0));
    CHECK(abs(GPU3D_Cos(0) - FX_ONE) < 8, "cos(0) = %d", GPU3D_Cos(0));
    CHECK(abs(GPU3D_Sin(-ANG_FULL/4) + FX_ONE) < 8, "sin(-90deg) = %d", GPU3D_Sin(-ANG_FULL/4));

    CHECK(abs(GPU3D_Sqrt(FX_ONE) - FX_ONE) < 16, "sqrt(1) = %d", GPU3D_Sqrt(FX_ONE));
    CHECK(abs(GPU3D_Sqrt(FX(4.0)) - FX(2.0)) < 16, "sqrt(4) = %d", GPU3D_Sqrt(FX(4.0)));
    CHECK(abs(GPU3D_Sqrt(FX(0.25)) - FX(0.5)) < 64, "sqrt(0.25) = %d", GPU3D_Sqrt(FX(0.25)));
    CHECK(GPU3D_Sqrt(0) == 0 && GPU3D_Sqrt(-5) == 0, "sqrt of zero/negative");

    /* every built in mesh has to be wound so that the face normal points out of
       the body, otherwise the back faces are drawn over the front ones */
    const struct { const char * name; const GPU3D_Mesh * mesh; } meshes[] = {
        { "cube", &GPU3D_CubeMesh },
        { "textured cube", &GPU3D_TexturedCubeMesh },
        { "ball", &GPU3D_BallMesh },
        { "pyramid", &GPU3D_PyramidMesh },
    };
    for (unsigned m = 0; m < sizeof(meshes)/sizeof(meshes[0]); m++)
    {
        const GPU3D_Mesh * mesh = meshes[m].mesh;
        for (uint8_t f = 0; f < mesh->nFaces; f++)
        {
            const GPU3D_Face * face = &mesh->faces[f];
            CHECK(face->nVerts >= 3 && face->nVerts <= GPU3D_MAX_FACE_VERTS,
                  "%s face %d has %d vertices", meshes[m].name, f, face->nVerts);
            Vec3 a = mesh->verts[face->verts[0]];
            Vec3 b = mesh->verts[face->verts[1]];
            Vec3 c = mesh->verts[face->verts[2]];
            Vec3 d1 = GPU3D_Vec(b.x-a.x, b.y-a.y, b.z-a.z);
            Vec3 d2 = GPU3D_Vec(c.x-a.x, c.y-a.y, c.z-a.z);
            Vec3 n;
            GPU3D_Cross(&d1, &d2, &n);
            Vec3 centroid = { 0, 0, 0 };
            for (uint8_t i = 0; i < face->nVerts; i++)
            {
                centroid.x += mesh->verts[face->verts[i]].x;
                centroid.y += mesh->verts[face->verts[i]].y;
                centroid.z += mesh->verts[face->verts[i]].z;
            }
            int64_t dot = (int64_t)n.x*centroid.x + (int64_t)n.y*centroid.y + (int64_t)n.z*centroid.z;
            CHECK(dot > 0, "%s face %d is wound the wrong way round", meshes[m].name, f);
            if (face->textured)
                for (uint8_t i = 0; i < face->nVerts; i++)
                    CHECK(face->u[i] <= TEXTURE_SIZE && face->v[i] <= TEXTURE_SIZE,
                          "%s face %d uv out of range", meshes[m].name, f);
        }
    }

    /* projection */
    GPU3D_Init();
    GPU3D_SetPerspective(100, 50, FX(200.0), FX(0.1));
    GPU3D_SetCameraOrientation(0, 0, 0);
    GPU3D_SetCameraPosition(0, 0, 0);
    int16_t sx, sy;
    Vec3 p = GPU3D_Vec(0, 0, FX(4.0));
    CHECK(GPU3D_Project(&p, &sx, &sy) == 1, "point in front not projected");
    CHECK(sx == 100 && sy == 50, "centre projects to %d,%d", sx, sy);
    p = GPU3D_Vec(FX(1.0), 0, FX(4.0));
    GPU3D_Project(&p, &sx, &sy);
    CHECK(sx == 100 + 200/4, "x=1 at z=4 projects to %d", sx);
    p = GPU3D_Vec(0, FX(1.0), FX(4.0));
    GPU3D_Project(&p, &sx, &sy);
    CHECK(sy == 50 - 200/4, "y=1 (up) projects to %d (screen y grows downwards)", sy);
    p = GPU3D_Vec(0, 0, FX(-4.0));
    CHECK(GPU3D_Project(&p, &sx, &sy) == 0, "point behind the camera projects");

    /* ortho projection ignores the distance */
    GPU3D_SetOrtho(100, 50, FX(10.0));
    p = GPU3D_Vec(FX(2.0), FX(3.0), FX(4.0));
    GPU3D_Project(&p, &sx, &sy);
    CHECK(sx == 120 && sy == 20, "ortho projects to %d,%d", sx, sy);
    p = GPU3D_Vec(FX(2.0), FX(3.0), FX(40.0));
    GPU3D_Project(&p, &sx, &sy);
    CHECK(sx == 120 && sy == 20, "ortho is not distance independent: %d,%d", sx, sy);
    GPU3D_Init();
}

static void test_gpu3d_rendering(void)
{
    BEGIN_TEST("3d rendering");
    Texture_CopyFrom(texture_rpi);

    /* a cube in front of the camera covers pixels, one behind it does not */
    GPU_ClearFramebuffers();
    GPU3D_Init();
    GPU3D_SetPerspective(GPU_X/2, GPU_Y/2, FX(230.0), FX(0.2));
    GPU3D_SetAmbient(120);
    Vec3 pos = GPU3D_Vec(0, 0, FX(3.0));
    Vec3 scale = GPU3D_Vec(FX_ONE, FX_ONE, FX_ONE);
    GPU3D_DrawBox(C_RED, &pos, FX_ONE, FX_ONE, FX_ONE, 0, ANG(20), 0, 0);
    uint32_t inFront = countNonBlack();
    CHECK(inFront > 3000, "cube in front of the camera paints %u pixels", inFront);

    GPU_ClearFramebuffers();
    Vec3 behind = GPU3D_Vec(0, 0, FX(-3.0));
    GPU3D_DrawBox(C_RED, &behind, FX_ONE, FX_ONE, FX_ONE, 0, ANG(20), 0, 0);
    CHECK(countNonBlack() == 0, "cube behind the camera paints %u pixels", countNonBlack());

    /* a cube that straddles the near plane must be clipped, not garbled */
    GPU_ClearFramebuffers();
    Vec3 straddle = GPU3D_Vec(0, 0, FX(0.3));
    GPU3D_DrawBox(C_GREEN, &straddle, FX_ONE, FX_ONE, FX_ONE, 0, 0, 0, 0);
    CHECK(countNonBlack() > 0, "straddling cube paints nothing");

    /* extreme coordinates must not overflow into a crash or a wild line */
    GPU_ClearFramebuffers();
    Vec3 far = GPU3D_Vec(FX(5000.0), FX(-5000.0), FX(2.0));
    GPU3D_DrawBox(C_BLUE, &far, FX(3000.0), FX(3000.0), FX(3000.0), ANG(123), ANG(456), ANG(789), 0);
    CHECK(1, "huge object survived");

    /* depth sorting: the nearer box has to win where they overlap. Note that
       this only holds inside one object/one call - between two separate calls
       the painter's algorithm means "whoever is drawn last wins". */
    GPU_ClearFramebuffers();
    Vec3 near = GPU3D_Vec(0, 0, FX(2.0));
    Vec3 far2 = GPU3D_Vec(0, 0, FX(6.0));
    GPU3D_DrawBox(C_BLUE, &far2, FX(2.0), FX(2.0), FX(2.0), 0, 0, 0, 0);
    GPU3D_DrawBox(C_RED, &near, FX(2.0), FX(2.0), FX(2.0), 0, 0, 0, 0);
    GPU_Color centre = 0;
    flush();
    uint32_t reddish = 0, bluish = 0;
    for (uint16_t x = 0; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++)
        {
            GPU_Color c = GetPixel(x, y);
            if (PIX_R5(c) > PIX_B5(c) + 4) reddish++;
            else if (PIX_B5(c) > PIX_R5(c) + 4) bluish++;
        }
    CHECK(reddish > bluish, "near cube does not cover the far one (red %u blue %u)", reddish, bluish);
    flush();
    centre = GetPixel(GPU_X/2, GPU_Y/2);
    CHECK(PIX_R5(centre) > PIX_B5(centre), "centre pixel is the far cube (0x%04x)", centre);

    /* flat shading: with the light coming from above and in front, the top face
       of a cube has to be brighter than the side faces, and the faces that look
       away have to fall back to the ambient level */
    GPU_ClearFramebuffers();
    GPU3D_Init();
    GPU3D_SetPerspective(GPU_X/2, GPU_Y/2, FX(230.0), FX(0.2));
    GPU3D_SetLightDirection(0, FX_ONE, 0);          /* straight from above */
    GPU3D_SetAmbient(60);
    Vec3 lit = GPU3D_Vec(0, 0, FX(4.0));
    GPU3D_DrawBox(C_WHITE, &lit, FX(2.0), FX(2.0), FX(2.0), ANG(20), ANG(30), 0, 0);
    flush();
    uint16_t brightest = 0, darkest = 0xffff;
    for (uint16_t x = 0; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++)
        {
            GPU_Color c = GetPixel(x, y);
            if (c == 0) continue;
            uint16_t luma = (uint16_t)(PIX_R5(c) + PIX_G6(c)/2 + PIX_B5(c));
            if (luma > brightest) brightest = luma;
            if (luma < darkest) darkest = luma;
        }
    CHECK(brightest > 2*darkest, "shading has no effect (brightest %u darkest %u)",
          brightest, darkest);

    /* the textured cube shows the sprite: the raspberry logo is mostly green */
    GPU_ClearFramebuffers();
    GPU3D_Init();
    Texture_CopyFrom(texture_rpi);
    GPU3D_SetPerspective(GPU_X/2, GPU_Y/2, FX(230.0), FX(0.2));
    GPU3D_DrawMesh(&GPU3D_TexturedCubeMesh, &pos, &scale, ANG(20), ANG(30), 0, 0);
    flush();
    uint32_t total = 0;
    uint16_t seen[512];
    unsigned seenCount = 0;
    for (uint16_t x = 0; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++)
        {
            GPU_Color c = GetPixel(x, y);
            if (c == 0) continue;
            total++;
            unsigned i;
            for (i = 0; i < seenCount; i++)
                if (seen[i] == c) break;
            if (i == seenCount && seenCount < sizeof(seen)/sizeof(seen[0]))
                seen[seenCount++] = c;
        }
    CHECK(total > 3000, "textured cube paints %u pixels", total);
    /* the sprite has a white background and a green logo, so a textured face
       has to produce far more colours than a flat shaded one */
    CHECK(seenCount > 10, "textured cube only shows %u different colours", seenCount);

    /* wireframe mode draws outlines only */
    GPU_ClearFramebuffers();
    GPU3D_DrawBox(C_WHITE, &pos, FX_ONE, FX_ONE, FX_ONE, 0, ANG(20), 0, 1);
    uint32_t wire = countNonBlack();   /* countNonBlack flushes */
    CHECK(wire > 100 && wire < inFront, "wireframe paints %u pixels (filled %u)", wire, inFront);

    /* 3d lines are clipped against the near plane */
    GPU_ClearFramebuffers();
    Vec3 a = GPU3D_Vec(FX(-2.0), 0, FX(1.0));
    Vec3 b = GPU3D_Vec(FX(2.0), 0, FX(5.0));
    GPU3D_DrawLineWorld(C_YELLOW, &a, &b);
    CHECK(countNonBlack() > 10, "3d line not drawn");
    GPU_ClearFramebuffers();
    Vec3 c1 = GPU3D_Vec(0, 0, FX(-5.0));
    Vec3 c2 = GPU3D_Vec(0, 0, FX(-1.0));
    GPU3D_DrawLineWorld(C_YELLOW, &c1, &c2);
    CHECK(countNonBlack() == 0, "line behind the camera is drawn");
    GPU3D_Init();
}

/* ---------------------------------------------------- main loop / menu key */

static void * runMainLoop(void * arg)
{
    (void)arg;
    DoMainLoop();
    return NULL;
}

/* waits until the virtual clock (which the main loop advances on its own) has
   moved forward by the given amount */
static void waitVirtual(uint32_t us)
{
    uint32_t target = Host_GetVirtualTime() + us;
    /* busy wait with a yield: the main loop advances the virtual clock several
       hundred times faster than real time, so even a short usleep() would
       overshoot the timing we want to test */
    for (int i = 0; i < 200000000; i++)
    {
        if ((int32_t)(Host_GetVirtualTime() - target) >= 0)
            return;
        sched_yield();
    }
    printf("  FAIL [mainloop] virtual clock did not advance\n");
    _failures++;
}

static void pressMenuOnce(void)
{
    setKeys(KEY_MENU);
    waitVirtual(KEY_DEBOUNCE_US*2);
    setKeys(KEY_NONE);
    waitVirtual(KEY_DEBOUNCE_US*2);
}

static void test_main_loop_menu_button(void)
{
    BEGIN_TEST("menu button: 1x reset, 2x main menu");
    Host_SetTimeAutoAdvance(100);       /* the loop drives its own clock */
    Host_SetVirtualTime(0);
    Random_Seed(99);
    SetupCallbacks(&MM_Callbacks);
    breakGame = 0;
    pthread_t thread;
    pthread_create(&thread, NULL, runMainLoop, NULL);
    waitVirtual(200000);

    CHECK(_callbacks.updateCallback != NULL, "main loop did not start");

    /* select a game with the keyboard (the first entry is game 1) */
    setKeys(KEY_RIGHT);
    waitVirtual(KEY_DEBOUNCE_US*3);
    setKeys(KEY_NONE);
    waitVirtual(100000);
    CHECK(_callbacks.initCallback == G1_Callbacks.initCallback,
          "RIGHT in the menu did not start a game");

    /* one press restarts the running game and stays in it */
    pressMenuOnce();
    waitVirtual(50000);
    CHECK(_callbacks.initCallback == G1_Callbacks.initCallback,
          "a single menu press left the game");

    /* two quick presses go back to the main menu */
    pressMenuOnce();
    pressMenuOnce();
    waitVirtual(50000);
    CHECK(_callbacks.initCallback == MM_Callbacks.initCallback,
          "a double menu press did not open the main menu");

    /* back into the game for the slow double press test */
    setKeys(KEY_RIGHT);
    waitVirtual(KEY_DEBOUNCE_US*3);
    setKeys(KEY_NONE);
    waitVirtual(100000);
    CHECK(_callbacks.initCallback == G1_Callbacks.initCallback, "game not started again");

    /* two slow presses must NOT open the menu, they restart twice */
    pressMenuOnce();
    waitVirtual(MENU_DOUBLE_PRESS_US + 200000);
    pressMenuOnce();
    waitVirtual(50000);
    CHECK(_callbacks.initCallback == G1_Callbacks.initCallback,
          "two slow menu presses opened the main menu");

    /* holding the button down must fire exactly one reset, not a second one */
    waitVirtual(MENU_DOUBLE_PRESS_US + 100000);
    setKeys(KEY_MENU);
    waitVirtual(1500000);
    setKeys(KEY_NONE);
    waitVirtual(50000);
    CHECK(_callbacks.initCallback == G1_Callbacks.initCallback,
          "holding the menu button left the game");

    breakGame = 1;
    pthread_join(thread, NULL);
    Host_SetTimeAutoAdvance(0);
}

/* ------------------------------------------------------------- game tests */

typedef struct {
    const char * name;
    const Callbacks * cb;
} GameEntry;

static const GameEntry allGames[] = {
    { "menu",        &MM_Callbacks },
    { "tictactoe1p", &G1_Callbacks },
    { "tictactoe2p", &G2_Callbacks },
    { "lightsout",   &G3_Callbacks },
    { "math",        &G4_Callbacks },
    { "tetramino",   &G5_Callbacks },
    { "labirinth",   &G6_Callbacks },
    { "connect4_1p", &G7_Callbacks },
    { "connect4_2p", &G8_Callbacks },
    { "2048",        &G9_Callbacks },
    { "paint",       &G10_Callbacks },
    { "snake",       &G11_Callbacks },
    { "calculator",  &G12_Callbacks },
    { "pong",        &G13_Callbacks },
    { "breakout",    &G14_Callbacks },
    { "qbert",       &G15_Callbacks },
    { "missionctl",  &G16_Callbacks },
    { "asteroids",   &G17_Callbacks },
    { "frogger",     &G18_Callbacks },
    { "tunnelrush",  &G19_Callbacks },
    { "missiledef",  &G20_Callbacks },
    { "pacman",      &G21_Callbacks },
    { "invaders",    &G22_Callbacks },
    { "turbodrive",  &G23_Callbacks },
};

static const GameEntry * findGame(const char * name)
{
    for (unsigned i = 0; i < sizeof(allGames)/sizeof(allGames[0]); i++)
        if (strcmp(allGames[i].name, name) == 0)
            return &allGames[i];
    return NULL;
}

static void test_games_smoke(void)
{
    BEGIN_TEST("games run and draw");
    Host_SetTimeAutoAdvance(0);
    for (unsigned i = 0; i < sizeof(allGames)/sizeof(allGames[0]); i++)
    {
        _rngState = 1000 + i;
        Host_SetVirtualTime(10000000);
        Random_Seed(0xC0FFEE + i);
        runFramesRandom(allGames[i].cb, 400);
        uint32_t pixels = countNonBlack();
        CHECK(pixels > 50, "%s draws only %u pixels after 400 random frames",
              allGames[i].name, pixels);
        /* the game must still be the one we started (the menu may of course
           launch a game when RIGHT is pressed) */
        if (strcmp(allGames[i].name, "menu") != 0)
            CHECK(_callbacks.initCallback == allGames[i].cb->initCallback,
                  "%s switched away during the run", allGames[i].name);
        printf("     %-12s %7u pixels drawn\n", allGames[i].name, pixels);
    }
}

static void test_game_specifics(void)
{
    BEGIN_TEST("game logic");
    Host_SetTimeAutoAdvance(0);
    Host_SetVirtualTime(10000000);

    /* --- tic tac toe: a full game has to end and offer a reset --- */
    Random_Seed(7);
    SetupCallbacks(&G2_Callbacks);
    for (int i = 0; i < 40; i++) stepFrame();
    /* play cell by cell through the touch interface: a short tap has to be
       enough, the game must not only look at the touch panel twice a second */
    for (int move = 0; move < 9; move++)
    {
        uint16_t tx = (uint16_t)(GPU_BORDER_X + (move % 3)*53 + 20);
        uint16_t ty = (uint16_t)(GPU_BORDER_Y + (move / 3)*53 + 20);
        setTouch(1, tx, ty);
        stepFrame();
        setTouch(0, 0, 0);
        for (int i = 0; i < 20; i++) stepFrame();
        if (mainMemory._gameState == GS_DONE)
            break;
    }
    CHECK(mainMemory._gameState == GS_DONE, "tic tac toe 2p is still running after 9 moves");

    /* --- lights out: the board has to be solvable, i.e. not empty at start -- */
    SetupCallbacks(&G3_Callbacks);
    for (int i = 0; i < 5; i++) stepFrame();
    CHECK(countNonBlack() > 500, "lights out does not draw a board");

    /* --- connect4 1p: filling a column must not corrupt memory (the old code
           indexed row 255 when the AI probed a full column) --- */
    SetupCallbacks(&G7_Callbacks);
    for (int i = 0; i < 60; i++)
        tapKey(KEY_DOWN);
    for (int i = 0; i < 60; i++)
        tapKey(KEY_LEFT);
    CHECK(1, "connect4 survived a full board");

    /* --- snake: it must not grow on its own and must die in the wall --- */
    SetupCallbacks(&G11_Callbacks);
    for (int i = 0; i < 5; i++) stepFrame();
    uint32_t before = countPixels(C_GREEN);
    for (int i = 0; i < 60; i++) stepFrame();
    uint32_t after = countPixels(C_GREEN);
    /* one body segment is 8x8 pixels; at most two fruits can be eaten on the
       straight path to the wall */
    CHECK(after >= before && after <= before + 2*64 + 8,
          "snake changed length without eating (%u -> %u)", before, after);
    for (int i = 0; i < 60; i++) stepFrame();
    CHECK(mainMemory._gameState == GS_DONE, "snake did not die running into the wall");

    /* --- pong: the ball has to move --- */
    SetupCallbacks(&G13_Callbacks);
    setKeys(KEY_UP);
    for (int i = 0; i < 10; i++) stepFrame();
    uint32_t h1 = frameHash();
    for (int i = 0; i < 10; i++) stepFrame();
    uint32_t h2 = frameHash();
    setKeys(KEY_NONE);
    CHECK(h1 != h2, "pong frame does not change");

    /* --- 2048: a move that changes the board spawns a tile, one that does not
           change anything must not --- */
    SetupCallbacks(&G9_Callbacks);
    for (int i = 0; i < 5; i++) stepFrame();
    uint32_t tiles = frameHash();
    setKeys(KEY_LEFT);
    for (int i = 0; i < 40; i++) stepFrame();
    setKeys(KEY_NONE);
    for (int i = 0; i < 10; i++) stepFrame();
    CHECK(frameHash() != tiles, "2048 does not react to a move");

    SetupCallbacks(&MM_Callbacks);
    setKeys(KEY_NONE);
    setTouch(0, 0, 0);
}

static void test_menu(void)
{
    BEGIN_TEST("main menu");
    Host_SetTimeAutoAdvance(0);
    Host_SetVirtualTime(10000000);
    Random_Seed(3);
    SetupCallbacks(&MM_Callbacks);
    for (int i = 0; i < 10; i++) stepFrame();
    /* the list starts at the top with the first entry highlighted */
    flush();
    CHECK(GetPixel(5, 5) != 0, "no highlight bar on the first entry");

    /* scrolling down moves the highlight and wraps around at the end */
    for (int i = 0; i < 30; i++)
        tapKey(KEY_DOWN);
    flush();
    CHECK(GetPixel(5, 5) == 0, "the highlight did not move away from the top");

    /* touch selects an entry, a second touch on it starts the game */
    SetupCallbacks(&MM_Callbacks);
    for (int i = 0; i < 5; i++) stepFrame();
    setTouch(1, 40, 30);
    stepFrame();
    setTouch(0, 0, 0);
    stepFrame();
    CHECK(_callbacks.initCallback == MM_Callbacks.initCallback, "a single touch started a game");
    setTouch(1, 40, 30);
    stepFrame();
    setTouch(0, 0, 0);
    for (int i = 0; i < 5; i++) stepFrame();
    CHECK(_callbacks.initCallback != MM_Callbacks.initCallback, "touching the entry twice did not start a game");

    /* the right half shows the rotating cube: it has to change over time */
    SetupCallbacks(&MM_Callbacks);
    for (int i = 0; i < 20; i++) stepFrame();
    uint32_t hashA = 0, hashB = 0;
    for (uint16_t x = GPU_X/2; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++) { hashA = hashA*31 + GetPixel(x, y); }
    for (int i = 0; i < 30; i++) stepFrame();
    for (uint16_t x = GPU_X/2; x < GPU_X; x++)
        for (uint16_t y = 0; y < GPU_Y; y++) { hashB = hashB*31 + GetPixel(x, y); }
    CHECK(hashA != hashB, "the cube does not rotate");
    uint32_t lit = 0;
    for (uint16_t x = GPU_X/2; x < GPU_X; x++)
        for (uint16_t y = 40; y < 200; y++)
            if (GetPixel(x, y) != 0) lit++;
    CHECK(lit > 2000, "the cube area has only %u lit pixels", lit);
    setKeys(KEY_NONE);
    setTouch(0, 0, 0);
}

/* ------------------------------------------------------------------- main */

int main(int argc, char ** argv)
{
    IF_Setup();
    GPU_Init();
    Host_UseVirtualTime(1);
    Host_SetVirtualTime(1000000);

    if (argc >= 3 && strcmp(argv[1], "shot") == 0)
    {
        const GameEntry * game = findGame(argv[2]);
        if (!game)
        {
            printf("unknown name '%s'. Known:\n", argv[2]);
            for (unsigned i = 0; i < sizeof(allGames)/sizeof(allGames[0]); i++)
                printf("  %s\n", allGames[i].name);
            return 2;
        }
        int frames = (argc >= 5) ? atoi(argv[4]) : 60;
        const char * mode = (argc >= 6) ? argv[5] : "random";
        int idle = (strcmp(mode, "idle") == 0);
        int cycle = (strcmp(mode, "cycle") == 0);
        _rngState = 4242;
        Host_SetVirtualTime(10000000);
        Random_Seed(0xBEEF);
        if (idle)
            runFramesIdle(game->cb, frames);
        else if (cycle)
            runFramesCycle(game->cb, frames);
        else
            runFramesRandom(game->cb, frames);
        savePpm(argv[3]);
        printf("wrote %s (%s, %d frames, %s)\n", argv[3], game->name, frames, mode);
        return 0;
    }

    printf("running pocket gamer tests\n");
    test_rgb_format();
    test_font_rendering();
    test_line_clipping();
    test_touch_mapping();
    test_touch_filtering();
    test_keyboard_debounce();
    test_random_quality();
    test_gpu3d_math();
    test_gpu3d_rendering();
    test_menu();
    test_games_smoke();
    test_game_specifics();
    test_main_loop_menu_button();

    printf("\n%d checks, %d failures\n", _checks, _failures);
    return _failures ? 1 : 0;
}
