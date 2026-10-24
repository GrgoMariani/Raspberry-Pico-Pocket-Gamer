#include "gpu.h"
#include "gpu_fonts.h"
#include "interface.h"

#include <string.h>
#include <math.h>


static uint16_t _framebuffer[GPU_X*GPU_Y];  // GPU VRAM (16bits per color)
static uint16_t _gpuTexture[TEXTURE_SIZE*TEXTURE_SIZE];


void GPU_Init()
{
    GPU_ClearTexture();
    GPU_ClearFramebuffers();
}

void GPU_ClearTexture()
{
    memset(_gpuTexture, 0, sizeof(_gpuTexture));
}

void GPU_ClearFramebuffers()
{
    memset(_framebuffer, 0, sizeof(_framebuffer));
}


//#define SHOW_FPS

#ifdef SHOW_FPS
uint32_t oldTime = 0;
#include "gameshared.h"  // mainmemory.touch_XY
#endif


void GPU_Render()
{
#ifdef SHOW_FPS
	char t[16] = "";
	uint16_t len = 0;
	uint32_t nowTime = IF_GetCurrentTime();

#define COLOR_INFO RGB(217,207,38)
	if (oldTime != nowTime)
	{
		uint16_t fps = ONE_SECOND / (nowTime-oldTime);
		len = sprintf(t, "FPS = %d", fps);
		GPU_DrawText(COLOR_INFO, &fontDescription[1], GPU_X-len*fontDescription[1].width, 0, t, len);
	}

	oldTime = nowTime;

	if (mainMemory.touchPressed)
	{
		len = sprintf(t, "X = %d", mainMemory.touch_X);
		GPU_DrawText(COLOR_INFO, &fontDescription[1], GPU_X-len*fontDescription[1].width, fontDescription[1].height*1, t, len);
		len = sprintf(t, "Y = %d", mainMemory.touch_Y);
		GPU_DrawText(COLOR_INFO, &fontDescription[1], GPU_X-len*fontDescription[1].width, fontDescription[1].height*2, t, len);
	}
#endif
    IF_DrawScreen((uint8_t *)_framebuffer, sizeof(_framebuffer));
}

///////////////////////////////////////////////

/*
 * The framebuffer is column major and the x axis is mirrored, that is how the
 * rotated panel (MADCTL = MV|MX|BGR) expects to receive it over SPI. A whole
 * column is therefore contiguous in memory, which the bulk fills below use.
 */
static inline uint16_t * _pixelPointer(int32_t x, int32_t y)
{
    return &_framebuffer[(GPU_X-x-1)*GPU_Y+y];
}

static inline void _putPixel(GPU_Color color, int32_t x, int32_t y)
{
    if ((uint32_t)x >= (uint32_t)GPU_X || (uint32_t)y >= (uint32_t)GPU_Y)
        return;
    *_pixelPointer(x, y) = (uint16_t)color;
}

void GPU_DrawPixel(GPU_Color color, uint16_t x, uint16_t y)
{
    if (x >= GPU_X || y >= GPU_Y)
        return;
    *_pixelPointer(x, y) = (uint16_t) color;
}

/* horizontal span, clipped; used by the polygon rasterizer */
static void _span(GPU_Color color, int32_t x0, int32_t x1, int32_t y)
{
    if (y < 0 || y >= GPU_Y)
        return;
    if (x0 < 0) x0 = 0;
    if (x0 > GPU_X-1)
        return;
    if (x1 > GPU_X-1) x1 = GPU_X-1;
    if (x0 > x1)
        return;
    uint16_t * pix = _pixelPointer(x0, y);
    for (int32_t x = x0; x <= x1; x++)
    {
        *pix = (uint16_t)color;
        pix -= GPU_Y;   // next column is GPU_Y entries before this one
    }
}

/*
 * Clips the segment against the screen using Liang-Barsky.
 * Returns 0 when the segment is completely outside, 1 otherwise
 * (with the coordinates clipped to the visible area).
 *
 * Clipping (instead of clamping the end points) is important: clamping a point
 * that lies outside of the screen changes the slope of the line and draws it
 * in the wrong direction.
 */
static uint8_t _clipLine(int16_t * x0, int16_t * y0, int16_t * x1, int16_t * y1)
{
    const int16_t xmin = 0, ymin = 0, xmax = GPU_X-1, ymax = GPU_Y-1;
    int32_t dx = (int32_t)*x1 - (int32_t)*x0;
    int32_t dy = (int32_t)*y1 - (int32_t)*y0;
    /* p[] / q[] pairs for the four borders: left, right, bottom, top */
    int32_t p[4] = { -dx, dx, -dy, dy };
    int32_t q[4] = { (int32_t)*x0 - xmin, xmax - (int32_t)*x0,
                     (int32_t)*y0 - ymin, ymax - (int32_t)*y0 };
    /* parametric range [t0, t1] scaled by 4096 to stay in integer math */
    int32_t t0 = 0;
    int32_t t1 = 4096;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (p[i] == 0)
        {
            if (q[i] < 0)
                return 0;   // parallel and outside
        }
        else
        {
            int32_t t = (q[i] * 4096) / p[i];
            if (p[i] < 0)
            {
                if (t > t1) return 0;
                if (t > t0) t0 = t;
            }
            else
            {
                if (t < t0) return 0;
                if (t < t1) t1 = t;
            }
        }
    }
    int32_t ox = *x0, oy = *y0;
    *x1 = (int16_t)(ox + (dx * t1 + 2048) / 4096);
    *y1 = (int16_t)(oy + (dy * t1 + 2048) / 4096);
    *x0 = (int16_t)(ox + (dx * t0 + 2048) / 4096);
    *y0 = (int16_t)(oy + (dy * t0 + 2048) / 4096);
    return 1;
}

void GPU_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    // Bresenhams line algorithm
    if (!_clipLine(&x0, &y0, &x1, &y1))
        return;
    int16_t dx =  x1>x0 ? x1-x0 : x0-x1;
    int16_t sx = x0<x1 ? 1 : -1;
    int16_t dy = y1 > y0 ? -(y1-y0): y1-y0;
    int16_t sy = y0<y1 ? 1 : -1;
    int16_t err = dx+dy;  /* error value e_xy */
    while (1)   /* loop */
    {
        GPU_DrawPixel(color, x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        int16_t e2 = 2*err;
        if (e2 >= dy) /* e_xy+e_x > 0 */
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) /* e_xy+e_y < 0 */
        {
            err += dx;
            y0 += sy;
        }
    }
}

void GPU_DrawLetter(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, char letter)
{
    if (letter < 32 || letter > 126)
        letter = 32;    // the font tables cover ' ' .. '~' only
    for (uint16_t i=0; i<font->height; i++)
    {
        uint16_t current_pic = font->fontArray[(letter-32)*font->height+i];
        for (uint8_t j=0; j<font->width; j++)
        {
            // the leftmost column of every glyph is the MSB of the row
            if (current_pic & (1u<<(15-j)))
                GPU_DrawPixel(color, x+j, y+i);
        }
    }
}

void GPU_DrawText(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, const char * text, uint16_t len)
{
    for (uint16_t i=0; i<len; i++)
    {
        if (text[i] == 0)
            break;
        GPU_DrawLetter(color, font, x+i*font->width, y, text[i]);
    }
}

void GPU_DrawEmptySquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height)
{
    GPU_DrawLine(color, x, y, x+width, y);
    GPU_DrawLine(color, x, y+height, x+width, y+height);
    GPU_DrawLine(color, x, y, x, y+height);
    GPU_DrawLine(color, x+width, y, x+width, y+height);
}

void GPU_DrawFilledSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height)
{
    /* clip, a column of the framebuffer is one contiguous block of memory */
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + width;   if (x1 > GPU_X) x1 = GPU_X;
    int32_t y1 = y + height;  if (y1 > GPU_Y) y1 = GPU_Y;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int32_t col = x0; col < x1; col++)
    {
        uint16_t * pix = _pixelPointer(col, y0);
        for (int32_t row = y0; row < y1; row++)
            *pix++ = (uint16_t)color;
    }
}

void GPU_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius)
{
    if (radius < 0)
        return;
    uint16_t xd = 0, yd = (uint16_t)radius;
    int16_t d = 3 - 2 * radius;
    GPU_DrawPixel(color, x+xd, y+yd);
    GPU_DrawPixel(color, x-xd, y+yd);
    GPU_DrawPixel(color, x+xd, y-yd);
    GPU_DrawPixel(color, x-xd, y-yd);
    GPU_DrawPixel(color, x+yd, y+xd);
    GPU_DrawPixel(color, x-yd, y+xd);
    GPU_DrawPixel(color, x+yd, y-xd);
    GPU_DrawPixel(color, x-yd, y-xd);
    while (yd >= xd)
    {
        xd++;
        if (d > 0)
        {
            yd--;
            d = d + 4 * (xd - yd) + 10;
        }
        else
            d = d + 4 * xd + 6;
        GPU_DrawPixel(color, x+xd, y+yd);
        GPU_DrawPixel(color, x-xd, y+yd);
        GPU_DrawPixel(color, x+xd, y-yd);
        GPU_DrawPixel(color, x-xd, y-yd);
        GPU_DrawPixel(color, x+yd, y+xd);
        GPU_DrawPixel(color, x-yd, y+xd);
        GPU_DrawPixel(color, x+yd, y-xd);
        GPU_DrawPixel(color, x-yd, y-xd);
    }
}

void GPU_DrawFilledCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius)
{
    if (radius < 0)
        return;
    uint16_t xd = 0, yd = (uint16_t)radius;
    int16_t d = 3 - 2 * radius;
    GPU_DrawLine(color, x, y-yd, x, y+yd);
    while (yd >= xd)
    {
        xd++;
        if (d > 0)
        {
            yd--;
            d = d + 4 * (xd - yd) + 10;
        }
        else
            d = d + 4 * xd + 6;
        GPU_DrawLine(color, x-xd, y-yd, x+xd, y-yd);
        GPU_DrawLine(color, x-xd, y+yd, x+xd, y+yd);
        GPU_DrawLine(color, x-yd, y-xd, x+yd, y-xd);
        GPU_DrawLine(color, x-yd, y+xd, x+yd, y+xd);
    }
}

///////////////////////////////////////////////
// Shared helpers (used by the games, the menu and the 3d renderer)
///////////////////////////////////////////////

GPU_Color GPU_ShadeColor(GPU_Color color, int16_t level)
{
    if (level <= 0)
        return C_BLACK;
    if (level >= 256)
        return color;
    int16_t r = (int16_t)((uint16_t)GPU_GET_R5(color) * (uint16_t)level) >> 8;
    int16_t g = (int16_t)((uint16_t)GPU_GET_G6(color) * (uint16_t)level) >> 8;
    int16_t b = (int16_t)((uint16_t)GPU_GET_B5(color) * (uint16_t)level) >> 8;
    return GPU_PACK_RGB565(r, g, b);
}

GPU_Color GPU_BlendColor(GPU_Color a, GPU_Color b, uint8_t t)
{
    if (t == 0)
        return a;
    if (t >= 255)
        return b;
    int16_t r = (int16_t)GPU_GET_R5(a) + ((int16_t)GPU_GET_R5(b) - (int16_t)GPU_GET_R5(a)) * t / 256;
    int16_t g = (int16_t)GPU_GET_G6(a) + ((int16_t)GPU_GET_G6(b) - (int16_t)GPU_GET_G6(a)) * t / 256;
    int16_t bl = (int16_t)GPU_GET_B5(a) + ((int16_t)GPU_GET_B5(b) - (int16_t)GPU_GET_B5(a)) * t / 256;
    return GPU_PACK_RGB565(r, g, bl);
}

uint16_t GPU_TextWidth(const FontDescription * font, const char * text)
{
    uint16_t len = 0;
    while (text[len])
        len++;
    return len * font->width;
}

void GPU_DrawTextCentered(GPU_Color color, const FontDescription * font, int16_t centerX, int16_t y, const char * text)
{
    uint16_t len = 0;
    while (text[len])
        len++;
    GPU_DrawText(color, font, centerX - (int16_t)(len*font->width)/2, y, text, len);
}

void GPU_DrawTextOutlined(GPU_Color color, GPU_Color outline, const FontDescription * font, int16_t centerX, int16_t y, const char * text)
{
    uint16_t len = 0;
    while (text[len])
        len++;
    int16_t x = centerX - (int16_t)(len*font->width)/2;
    for (int8_t dx = -1; dx <= 1; dx++)
        for (int8_t dy = -1; dy <= 1; dy++)
        {
            if (dx == 0 && dy == 0)
                continue;
            GPU_DrawText(outline, font, x+dx, y+dy, text, len);
        }
    GPU_DrawText(color, font, x, y, text, len);
}

void GPU_DrawBevelSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height)
{
    if (width <= 0 || height <= 0)
        return;
    GPU_DrawFilledSquare(color, x, y, width, height);
    if (width < 3 || height < 3)
        return;
    GPU_DrawLine(GPU_ShadeColor(color, 340), x, y, x+width-1, y);
    GPU_DrawLine(GPU_ShadeColor(color, 340), x, y, x, y+height-1);
    GPU_DrawLine(GPU_ShadeColor(color, 110), x, y+height-1, x+width-1, y+height-1);
    GPU_DrawLine(GPU_ShadeColor(color, 110), x+width-1, y, x+width-1, y+height-1);
}

void GPU_DrawPanel(GPU_Color background, GPU_Color border, int16_t x, int16_t y, int16_t width, int16_t height)
{
    GPU_DrawFilledSquare(background, x+1, y+1, width-1, height-1);
    GPU_DrawEmptySquare(border, x, y, width, height);
}

void GPU_DrawVerticalGradient(GPU_Color top, GPU_Color bottom, int16_t x, int16_t y, int16_t width, int16_t height)
{
    if (height <= 0 || width <= 0)
        return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + width;   if (x1 > GPU_X) x1 = GPU_X;
    int32_t y1 = y + height;  if (y1 > GPU_Y) y1 = GPU_Y;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int32_t row = y0; row < y1; row++)
    {
        GPU_Color line = GPU_BlendColor(top, bottom, (uint8_t)((int32_t)(row-y)*255/(height > 1 ? height-1 : 1)));
        for (int32_t col = x0; col < x1; col++)
            *_pixelPointer(col, row) = line;
    }
}

void GPU_DrawFilledTriangle(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    /* sort the vertices by y */
    if (y0 > y1) { int16_t t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
    if (y1 > y2) { int16_t t; t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }
    if (y0 > y1) { int16_t t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
    if (y2 < y0)
        return;
    int32_t total = y2 - y0;
    for (int32_t y = y0; y <= y2; y++)
    {
        int32_t xa, xb;
        if (total == 0)
        {
            xa = x0; xb = x2;
        }
        else if (y < y1)
        {
            int32_t span = (y1 - y0) ? (y1 - y0) : 1;
            xa = x0 + (x1 - x0) * (y - y0) / span;
            xb = x0 + (x2 - x0) * (y - y0) / total;
        }
        else
        {
            int32_t span = (y2 - y1) ? (y2 - y1) : 1;
            xa = x1 + (x2 - x1) * (y - y1) / span;
            xb = x0 + (x2 - x0) * (y - y0) / total;
        }
        if (xa > xb) { int32_t t = xa; xa = xb; xb = t; }
        GPU_DrawLine(color, (int16_t)xa, (int16_t)y, (int16_t)xb, (int16_t)y);
    }
}

void GPU_DrawButton(GPU_Color color, GPU_Color textColor, const FontDescription * font, const char * text, int16_t x, int16_t y, int16_t width, int16_t height)
{
    GPU_DrawBevelSquare(color, x, y, width, height);
    GPU_DrawTextCentered(textColor, font, x + width/2, y + (height - font->height)/2, text);
}

void GPU_DrawConvexPolygon(GPU_Color color, const int16_t * sx, const int16_t * sy, uint8_t n)
{
    if (n < 3 || n > GPU_MAX_POLYGON_VERTS)
        return;
    int32_t ymin = sy[0], ymax = sy[0];
    for (uint8_t i = 1; i < n; i++)
    {
        if (sy[i] < ymin) ymin = sy[i];
        if (sy[i] > ymax) ymax = sy[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > GPU_Y-1) ymax = GPU_Y-1;
    for (int32_t y = ymin; y <= ymax; y++)
    {
        int32_t xmin = 0x7fffffff, xmax = -0x7fffffff;
        for (uint8_t i = 0; i < n; i++)
        {
            uint8_t j = (uint8_t)((i + 1) % n);
            int32_t y0 = sy[i], y1 = sy[j];
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y))
            {
                int32_t x = (int32_t)sx[i] + ((int32_t)sx[j] - (int32_t)sx[i]) * (y - y0) / (y1 - y0);
                if (x < xmin) xmin = x;
                if (x > xmax) xmax = x;
            }
        }
        if (xmin <= xmax)
            _span(color, xmin, xmax, y);
    }
}

static inline GPU_Color _texel(int32_t u, int32_t v)
{
    /* u and v are Q8 texture coordinates */
    int32_t tx = u >> 8;
    int32_t ty = v >> 8;
    if (tx < 0) tx = 0;
    if (tx > TEXTURE_SIZE-1) tx = TEXTURE_SIZE-1;
    if (ty < 0) ty = 0;
    if (ty > TEXTURE_SIZE-1) ty = TEXTURE_SIZE-1;
    return _gpuTexture[ty*TEXTURE_SIZE+tx];
}

static void _texturedTriangle(int16_t x0, int16_t y0, int32_t u0, int32_t v0,
                              int16_t x1, int16_t y1, int32_t u1, int32_t v1,
                              int16_t x2, int16_t y2, int32_t u2, int32_t v2,
                              int16_t shadeLevel)
{
    /* sort the three corners by y */
    #define SWAP_PA(a,b) do { int16_t t = a; a = b; b = t; } while (0)
    #define SWAP_PX(a,b) do { int32_t w = a; a = b; b = w; } while (0)
    if (y0 > y1) { SWAP_PA(x0,x1); SWAP_PA(y0,y1); SWAP_PX(u0,u1); SWAP_PX(v0,v1); }
    if (y1 > y2) { SWAP_PA(x1,x2); SWAP_PA(y1,y2); SWAP_PX(u1,u2); SWAP_PX(v1,v2); }
    if (y0 > y1) { SWAP_PA(x0,x1); SWAP_PA(y0,y1); SWAP_PX(u0,u1); SWAP_PX(v0,v1); }
    #undef SWAP_PA
    #undef SWAP_PX

    int32_t totalHeight = y2 - y0;
    if (totalHeight <= 0)
        return;
    for (int32_t y = y0; y < y2; y++)
    {
        if (y < 0 || y >= GPU_Y)
            continue;
        int32_t xa, xb, ua, ub, va, vb;
        /* the long edge always spans y0..y2 */
        xa = x0 + (x2 - x0) * (y - y0) / totalHeight;
        ua = u0 + (u2 - u0) * (y - y0) / totalHeight;
        va = v0 + (v2 - v0) * (y - y0) / totalHeight;
        if (y < y1)
        {
            int32_t span = (y1 - y0) ? (y1 - y0) : 1;
            xb = x0 + (x1 - x0) * (y - y0) / span;
            ub = u0 + (u1 - u0) * (y - y0) / span;
            vb = v0 + (v1 - v0) * (y - y0) / span;
        }
        else
        {
            int32_t span = (y2 - y1) ? (y2 - y1) : 1;
            xb = x1 + (x2 - x1) * (y - y1) / span;
            ub = u1 + (u2 - u1) * (y - y1) / span;
            vb = v1 + (v2 - v1) * (y - y1) / span;
        }
        if (xa > xb)
        {
            int16_t t = xa; xa = xb; xb = t;
            int32_t w = ua; ua = ub; ub = w;
            w = va; va = vb; vb = w;
        }
        if (xa < 0) xa = 0;
        if (xb > GPU_X-1) xb = GPU_X-1;
        int32_t width = xb - xa;
        if (width < 0)
            continue;
        int32_t du = (width > 0) ? (ub - ua) / width : 0;
        int32_t dv = (width > 0) ? (vb - va) / width : 0;
        int32_t u = ua, v = va;
        uint16_t * pix = _pixelPointer(xa, y);
        for (int32_t x = xa; x <= xb; x++)
        {
            GPU_Color pixel = _texel(u, v);
            if (shadeLevel != 256)
                pixel = GPU_ShadeColor(pixel, shadeLevel);
            *pix = pixel;
            pix -= GPU_Y;
            u += du;
            v += dv;
        }
    }
}

void GPU_DrawTexturedPolygon(const int16_t * sx, const int16_t * sy,
                             const int32_t * u, const int32_t * v, uint8_t n,
                             int16_t shadeLevel)
{
    if (n < 3 || n > GPU_MAX_POLYGON_VERTS)
        return;
    /* fan triangulation, exact for convex polygons */
    for (uint8_t i = 1; i + 1 < n; i++)
        _texturedTriangle(sx[0], sy[0], u[0], v[0],
                          sx[i], sy[i], u[i], v[i],
                          sx[i+1], sy[i+1], u[i+1], v[i+1],
                          shadeLevel);
}

///////////////////////////////////////////////
// Texture (64x64) drawing
///////////////////////////////////////////////

GPU_Color Texture_GetPixel(uint8_t x, uint8_t y)
{
	return _gpuTexture[y*TEXTURE_SIZE+x];
}

void Texture_DrawPixel(GPU_Color color, uint16_t x, uint16_t y)
{
    if (x >= TEXTURE_SIZE || y >= TEXTURE_SIZE)
        return;
    _gpuTexture[y*TEXTURE_SIZE+x] = color;
}

static uint8_t _clipTextureLine(int16_t * x0, int16_t * y0, int16_t * x1, int16_t * y1)
{
    const int16_t xmin = 0, ymin = 0, xmax = TEXTURE_SIZE-1, ymax = TEXTURE_SIZE-1;
    int32_t dx = (int32_t)*x1 - (int32_t)*x0;
    int32_t dy = (int32_t)*y1 - (int32_t)*y0;
    int32_t p[4] = { -dx, dx, -dy, dy };
    int32_t q[4] = { (int32_t)*x0 - xmin, xmax - (int32_t)*x0,
                     (int32_t)*y0 - ymin, ymax - (int32_t)*y0 };
    int32_t t0 = 0;
    int32_t t1 = 4096;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (p[i] == 0)
        {
            if (q[i] < 0)
                return 0;
        }
        else
        {
            int32_t t = (q[i] * 4096) / p[i];
            if (p[i] < 0)
            {
                if (t > t1) return 0;
                if (t > t0) t0 = t;
            }
            else
            {
                if (t < t0) return 0;
                if (t < t1) t1 = t;
            }
        }
    }
    int32_t ox = *x0, oy = *y0;
    *x1 = (int16_t)(ox + (dx * t1 + 2048) / 4096);
    *y1 = (int16_t)(oy + (dy * t1 + 2048) / 4096);
    *x0 = (int16_t)(ox + (dx * t0 + 2048) / 4096);
    *y0 = (int16_t)(oy + (dy * t0 + 2048) / 4096);
    return 1;
}

void Texture_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    // Bresenhams line algorithm
    if (!_clipTextureLine(&x0, &y0, &x1, &y1))
        return;
    int16_t dx =  x1>x0 ? x1-x0 : x0-x1;
    int16_t sx = x0<x1 ? 1 : -1;
    int16_t dy = y1 > y0 ? -(y1-y0): y1-y0;
    int16_t sy = y0<y1 ? 1 : -1;
    int16_t err = dx+dy;  /* error value e_xy */
    while (1)   /* loop */
    {
        Texture_DrawPixel(color, x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        int16_t e2 = 2*err;
        if (e2 >= dy) /* e_xy+e_x > 0 */
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) /* e_xy+e_y < 0 */
        {
            err += dx;
            y0 += sy;
        }
    }
}

void Texture_DrawLetter(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, char letter)
{
    if (letter < 32 || letter > 126)
        letter = 32;
    for (uint16_t i=0; i<font->height; i++)
    {
        uint16_t current_pic = font->fontArray[(letter-32)*font->height+i];
        for (uint8_t j=0; j<font->width; j++)
        {
            if (current_pic & (1u<<(15-j)))
                Texture_DrawPixel(color, x+j, y+i);
        }
    }
}

void Texture_DrawText(GPU_Color color, const FontDescription * font, int16_t x, int16_t y, const char * text, uint16_t len)
{
    for (uint16_t i=0; i<len; i++)
    {
        if (text[i] == 0)
            break;
        Texture_DrawLetter(color, font, x+i*font->width, y, text[i]);
    }
}

void Texture_DrawEmptySquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height)
{
    Texture_DrawLine(color, x, y, x+width, y);
    Texture_DrawLine(color, x, y+height, x+width, y+height);
    Texture_DrawLine(color, x, y, x, y+height);
    Texture_DrawLine(color, x+width, y, x+width, y+height);
}

void Texture_DrawFilledSquare(GPU_Color color, int16_t x, int16_t y, int16_t width, int16_t height)
{
    for (int16_t i=0; i<width; i++)
    {
        for (int16_t j=0; j<height; j++)
        {
            Texture_DrawPixel(color, x+i, y+j);
        }
    }
}

void Texture_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius)
{
    if (radius < 0)
        return;
    uint16_t xd = 0, yd = (uint16_t)radius;
    int16_t d = 3 - 2 * radius;
    Texture_DrawPixel(color, x+xd, y+yd);
    Texture_DrawPixel(color, x-xd, y+yd);
    Texture_DrawPixel(color, x+xd, y-yd);
    Texture_DrawPixel(color, x-xd, y-yd);
    Texture_DrawPixel(color, x+yd, y+xd);
    Texture_DrawPixel(color, x-yd, y+xd);
    Texture_DrawPixel(color, x+yd, y-xd);
    Texture_DrawPixel(color, x-yd, y-xd);
    while (yd >= xd)
    {
        xd++;
        if (d > 0)
        {
            yd--;
            d = d + 4 * (xd - yd) + 10;
        }
        else
            d = d + 4 * xd + 6;
        Texture_DrawPixel(color, x+xd, y+yd);
        Texture_DrawPixel(color, x-xd, y+yd);
        Texture_DrawPixel(color, x+xd, y-yd);
        Texture_DrawPixel(color, x-xd, y-yd);
        Texture_DrawPixel(color, x+yd, y+xd);
        Texture_DrawPixel(color, x-yd, y+xd);
        Texture_DrawPixel(color, x+yd, y-xd);
        Texture_DrawPixel(color, x-yd, y-xd);
    }
}

void Texture_DrawFilledCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius)
{
    if (radius < 0)
        return;
    uint16_t xd = 0, yd = (uint16_t)radius;
    int16_t d = 3 - 2 * radius;
    Texture_DrawLine(color, x, y-yd, x, y+yd);
    while (yd >= xd)
    {
        xd++;
        if (d > 0)
        {
            yd--;
            d = d + 4 * (xd - yd) + 10;
        }
        else
            d = d + 4 * xd + 6;
        Texture_DrawLine(color, x-xd, y-yd, x+xd, y-yd);
        Texture_DrawLine(color, x-xd, y+yd, x+xd, y+yd);
        Texture_DrawLine(color, x-yd, y-xd, x+yd, y-xd);
        Texture_DrawLine(color, x-yd, y+xd, x+yd, y+xd);
    }
}

void Texture_CopyFrom(const uint16_t * texture)
{
    memcpy(_gpuTexture, texture, TEXTURE_SIZE*TEXTURE_SIZE*sizeof(uint16_t));
}
