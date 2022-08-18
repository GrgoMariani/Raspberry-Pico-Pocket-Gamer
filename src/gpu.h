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

#define GPU_Color uint16_t

extern const FontDescription fontDescription[4];


#define RGB(r, g, b) ( ((r)&0xf8 ) | (((b)&0xf8)<<5) | (((g)&0xe0)>>5) | (((g)&0x1c)<<6) )
#define C_WHITE RGB(255,255,255)
#define C_BLACK RGB(0,0,0)
#define C_BLUE RGB(0,0,255)
#define C_YELLOW RGB(255,255,0)
#define C_RED RGB(255,0,0)
#define C_GREEN RGB(0,255,0)
#define C_GRAY_LIGHT RGB(64,64,64)
#define C_GRAY_MEDIUM RGB(128,128,128)
#define C_GRAY_DARK RGB(192,192,192)
#define C_C1 RGB(39,153,216)
#define C_C2 RGB(216,39,153)
#define C_C3 RGB(153,216,39)

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
