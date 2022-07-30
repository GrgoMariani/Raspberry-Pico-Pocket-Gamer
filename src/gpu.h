#ifndef GPU_H_
#define GPU_H_

#include <stdint.h>


#define GPU_TARGET_FPS (6 * 5)
#define GPU_X 320
#define GPU_Y 240
#define GPU_MIN ( GPU_X/2 < GPU_Y ? GPU_X : GPU_Y )
#define GPU_BORDER_X ( GPU_X/2 > GPU_Y ? (GPU_X/2-GPU_Y)/2 : 0 )
#define GPU_BORDER_Y ( GPU_X/2 < GPU_Y ? (GPU_Y-GPU_X/2)/2 : 0 )
#define GPU_REST ( GPU_X/2-2*GPU_BORDER_X )
#define S1_CENTER_X ( GPU_X/4 )
#define S1_CENTER_Y ( GPU_Y/2 )
#define S2_CENTER_X ( 3*GPU_X/4 )
#define S2_CENTER_Y ( GPU_Y/2 )
#define TEXTURE_SIZE 64
#define B_WIDTH 8

typedef struct {
    const uint16_t * fontArray;
    const uint8_t width;
    const uint8_t height;
} FontDescription;

/*
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t o;
} GPU_Color;*/

#define GPU_Color uint16_t

extern const FontDescription fontDescription[4];



#define C_WHITE 0xffff
#define C_BLACK 0x0000
#define C_BLUE 0x1f00
#define C_YELLOW 0xe0ff
#define C_RED 0x00f8
#define C_GREEN 0xe007
#define C_GRAY_LIGHT 0x4444
#define C_GRAY_MEDIUM 0x8888
#define C_GRAY_DARK 0xcccc
#define C_C1 0x4444
#define C_C2 0x8888
#define C_C3 0xcccc

void GPU_Init();
void GPU_ClearTexture();
void GPU_ClearFramebuffers();
void GPU_Render();

void GPU_DrawPixel(GPU_Color color, uint16_t x, uint16_t y);
void GPU_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void GPU_DrawLetter(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, char letter);
void GPU_DrawText(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, const char * text, uint16_t len);
void GPU_DrawEmptySquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawFilledSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);
void GPU_DrawFilledCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);

GPU_Color Texture_GetPixel(uint8_t x, uint8_t y);
void Texture_DrawPixel(GPU_Color color, uint16_t x, uint16_t y);
void Texture_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void Texture_DrawLetter(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, char letter);
void Texture_DrawText(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, const char * text, uint16_t len);
void Texture_DrawEmptySquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void Texture_DrawFilledSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void Texture_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);
void Texture_DrawFilledCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);
void Texture_CopyFrom(const uint16_t * texture);

void GPU_DrawRotatedTexture(int16_t x, int16_t y, float factor, float angle);

#endif//GPU_H_
