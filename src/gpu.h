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

/*
 * Pixel format
 * ------------
 * A pixel is RGB565, but stored byte swapped: the ILI9341 is fed over SPI in
 * big endian order and the framebuffer is sent to the panel as is, therefore
 * the byte the panel receives first (RRRRRGGG) has to be the LOW byte of the
 * uint16_t in memory.
 *
 *   bits  0.. 2 : GGG   (3 most significant green bits)
 *   bits  3.. 7 : RRRRR
 *   bits  8..12 : BBBBB
 *   bits 13..15 : ggg   (3 least significant green bits)
 */
#define GPU_GET_R5(c)   ((uint8_t)(((c) >> 3) & 0x1F))
#define GPU_GET_B5(c)   ((uint8_t)(((c) >> 8) & 0x1F))
#define GPU_GET_G6(c)   ((uint8_t)((((c) & 0x0007) << 3) | (((c) >> 13) & 0x07)))
#define GPU_PACK_RGB565(r5, g6, b5)                                             \
    ((GPU_Color)(((uint16_t)(r5) << 3) | ((uint16_t)(b5) << 8) |                \
                 ((uint16_t)(g6) >> 3) | (((uint16_t)(g6) & 0x07) << 13)))
#define RGB(r, g, b)    GPU_PACK_RGB565(((r) >> 3) & 0x1F, ((g) >> 2) & 0x3F, ((b) >> 3) & 0x1F)

#define C_WHITE RGB(255,255,255)
#define C_BLACK RGB(0,0,0)
#define C_BLUE RGB(0,0,255)
#define C_YELLOW RGB(255,255,0)
#define C_RED RGB(255,0,0)
#define C_GREEN RGB(0,255,0)
#define C_GRAY_LIGHT RGB(192,192,192)
#define C_GRAY_MEDIUM RGB(128,128,128)
#define C_GRAY_DARK RGB(64,64,64)
#define C_C1 RGB(39,153,216)
#define C_C2 RGB(216,39,153)
#define C_C3 RGB(153,216,39)
#define C_CYAN RGB(0,255,255)
#define C_MAGENTA RGB(255,0,255)
#define C_ORANGE RGB(255,140,0)
#define C_PURPLE RGB(128,0,255)

void GPU_Init();
void GPU_ClearTexture();
void GPU_ClearFramebuffers();
void GPU_Render();

void GPU_DrawPixel(GPU_Color color, uint16_t x, uint16_t y);
void GPU_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void GPU_DrawLetter(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, char letter);
void GPU_DrawText(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, const char * text, uint16_t len);
void GPU_DrawEmptySquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawFilledSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);
void GPU_DrawFilledCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);

/* --- helpers shared by the games, the menu and the 3d renderer --- */
/* level: 0 = black, 256 = unchanged, >256 brightens (saturating) */
GPU_Color GPU_ShadeColor(GPU_Color color, int16_t level);
/* t: 0 = color a, 256 = color b */
GPU_Color GPU_BlendColor(GPU_Color a, GPU_Color b, uint8_t t);
uint16_t GPU_TextWidth(const FontDescription * font, const char * text);
void GPU_DrawTextCentered(GPU_Color color, const FontDescription * font, int16_t centerX, int16_t y, const char * text);
void GPU_DrawTextOutlined(GPU_Color color, GPU_Color outline, const FontDescription * font, int16_t centerX, int16_t y, const char * text);
void GPU_DrawBevelSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawPanel(GPU_Color background, GPU_Color border, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawVerticalGradient(GPU_Color top, GPU_Color bottom, int16_t x, int16_t y, int16_t width, int16_t height);
void GPU_DrawFilledTriangle(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
void GPU_DrawButton(GPU_Color color, GPU_Color textColor, const FontDescription * font, const char * text, int16_t x, int16_t y, int16_t width, int16_t height);

/*
 * Polygon rasterizer used by the 3d renderer (and available to the games).
 * The polygons have to be convex, u/v are Q8 texture coordinates into the
 * currently loaded 64x64 texture and shadeLevel scales the sampled colours
 * (256 = leave them untouched).
 */
#define GPU_MAX_POLYGON_VERTS 8
void GPU_DrawConvexPolygon(GPU_Color color, const int16_t * sx, const int16_t * sy, uint8_t n);
void GPU_DrawTexturedPolygon(const int16_t * sx, const int16_t * sy,
                             const int32_t * u, const int32_t * v, uint8_t n,
                             int16_t shadeLevel);

GPU_Color Texture_GetPixel(uint8_t x, uint8_t y);
void Texture_DrawPixel(GPU_Color color, uint16_t x, uint16_t y);
void Texture_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void Texture_DrawLetter(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, char letter);
void Texture_DrawText(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, const char * text, uint16_t len);
void Texture_DrawEmptySquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void Texture_DrawFilledSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height);
void Texture_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);
void Texture_DrawFilledCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius);
void Texture_CopyFrom(const uint16_t * texture);

#endif//GPU_H_
