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

void GPU_DrawPixel(GPU_Color color, uint16_t x, uint16_t y)
{
    if (x >= GPU_X || y >= GPU_Y)
        return;
    uint16_t * pix = &_framebuffer[(GPU_X-x-1)*GPU_Y+y];
    (*pix) = (uint16_t) color;
}

void GPU_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    // Bresenhams line algorithm
    x0 = (x0 >= GPU_X) ? GPU_X-1 : x0;
    x1 = (x1 >= GPU_X) ? GPU_X-1 : x1;
    y0 = (y0 >= GPU_Y) ? GPU_Y-1 : y0;
    y1 = (y1 >= GPU_Y) ? GPU_Y-1 : y1;
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

void GPU_DrawLetter(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, char letter)
{
    for (uint16_t i=0; i<fontDescription->height; i++)
    {
        uint16_t current_pic = fontDescription->fontArray[(letter-32)*fontDescription->height+i];
        for (uint8_t j=0; j<fontDescription->width; j++)
        {
            if (current_pic & (1<<(16-j)))
                GPU_DrawPixel(color, x+j, y+i);
        }
    }
}

void GPU_DrawText(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, const char * text, uint16_t len)
{
    for (uint16_t i=0; i<len; i++)
    {
        GPU_DrawLetter(color, fontDescription, x+i*fontDescription->width, y, text[i]);
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
    for (int16_t i=0; i<width; i++)
    {
        for (int16_t j=0; j<height; j++)
        {
            GPU_DrawPixel(color, x+i, y+j);
        }    
    }
}

void GPU_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius)
{
    uint16_t xd = 0, yd = radius;
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
    uint16_t xd = 0, yd = radius;
    int16_t d = 3 - 2 * radius;
    int16_t mxxd = x-xd > 0 ? x-xd : 0;
    int16_t myyd = y-yd > 0 ? y-yd : 0;
    int16_t mxyd = x-yd > 0 ? x-yd : 0;
    int16_t myxd = y-xd > 0 ? y-xd : 0;
    GPU_DrawLine(color, mxxd, myyd, x+xd, myyd);
    GPU_DrawLine(color, mxxd, y+yd, x+xd, y+yd);
    GPU_DrawLine(color, mxyd, myxd, x+yd, myxd);
    GPU_DrawLine(color, mxyd, y+xd, x+yd, y+xd);
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
        
        mxxd = x-xd > 0 ? x-xd : 0;
        myyd = y-yd > 0 ? y-yd : 0;
        mxyd = x-yd > 0 ? x-yd : 0;
        myxd = y-xd > 0 ? y-xd : 0;
        GPU_DrawLine(color, x-xd, y-yd, x+xd, y-yd);
        GPU_DrawLine(color, x-xd, y+yd, x+xd, y+yd);
        GPU_DrawLine(color, x-yd, y-xd, x+yd, y-xd);
        GPU_DrawLine(color, x-yd, y+xd, x+yd, y+xd);
    }
}

GPU_Color Texture_GetPixel(uint8_t x, uint8_t y)
{
	return _gpuTexture[y*TEXTURE_SIZE+x];
}

void Texture_DrawPixel(GPU_Color color, uint16_t x, uint16_t y)
{
    if (x >= TEXTURE_SIZE || y >= TEXTURE_SIZE)
        return;

	uint16_t * pix = &_gpuTexture[y*TEXTURE_SIZE+x];
    *pix = color;
}

void Texture_DrawLine(GPU_Color color, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    // Bresenhams line algorithm
    x0 = (x0 >= GPU_X) ? GPU_X-1 : x0;
    x1 = (x1 >= GPU_X) ? GPU_X-1 : x1;
    y0 = (y0 >= GPU_Y) ? GPU_Y-1 : y0;
    y1 = (y1 >= GPU_Y) ? GPU_Y-1 : y1;
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

void Texture_DrawLetter(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, char letter)
{
    for (uint16_t i=0; i<fontDescription->height; i++)
    {
        uint16_t current_pic = fontDescription->fontArray[(letter-32)*fontDescription->height+i];
        for (uint8_t j=0; j<fontDescription->width; j++)
        {
            if (current_pic & (1<<(16-j)))
                Texture_DrawPixel(color, x+j, y+i);
        }
    }
}

void Texture_DrawText(GPU_Color color, const FontDescription * fontDescription, int16_t x, int16_t y, const char * text, uint16_t len)
{
    for (uint16_t i=0; i<len; i++)
    {
        Texture_DrawLetter(color, fontDescription, x+i*fontDescription->width, y, text[i]);
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
    // Could be sped up probably by x16
    for (uint8_t i=0; i<width; i++)
    {
        for (uint8_t j=0; j<height; j++)
        {
            Texture_DrawPixel(color, x+i, y+j);
        }    
    }
}

void Texture_DrawEmptyCircle(GPU_Color color, int16_t x, int16_t y, int16_t radius)
{
    uint16_t xd = 0, yd = radius;
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
    uint16_t xd = 0, yd = radius;
    int16_t d = 3 - 2 * radius;
    int16_t mxxd = x-xd > 0 ? x-xd : 0;
    int16_t myyd = y-yd > 0 ? y-yd : 0;
    int16_t mxyd = x-yd > 0 ? x-yd : 0;
    int16_t myxd = y-xd > 0 ? y-xd : 0;
    Texture_DrawLine(color, mxxd, myyd, x+xd, myyd);
    Texture_DrawLine(color, mxxd, y+yd, x+xd, y+yd);
    Texture_DrawLine(color, mxyd, myxd, x+yd, myxd);
    Texture_DrawLine(color, mxyd, y+xd, x+yd, y+xd);
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
        
        mxxd = x-xd > 0 ? x-xd : 0;
        myyd = y-yd > 0 ? y-yd : 0;
        mxyd = x-yd > 0 ? x-yd : 0;
        myxd = y-xd > 0 ? y-xd : 0;
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


void GPU_DrawRotatedTexture(int16_t x, int16_t y, float factor, float angle)
{
    if (factor == 0)
        return;
    const float s = sin(angle);
    const float c = cos(angle);
    const uint16_t tex_size = TEXTURE_SIZE * factor;
    const int16_t d = tex_size / 2;
    float cx = x - d * (c + s);
    float cy = y - d * (c - s);
    float step = 1.0/factor;
    float ix = 0;
    float iy = 0;
    for (uint16_t i = 0; i < tex_size; i++)
    {
        float rx = cx;
        float ry = cy;
        iy = 0;
        for (uint16_t j = 0; j < tex_size; j++)
        {
            GPU_Color pixel = Texture_GetPixel(ix, iy);
            GPU_DrawPixel(pixel, rx, ry);
            rx += s;
            ry += c;
            iy += step;
        }
        cx += c;
        cy -= s;
        ix += step;
    }
}
