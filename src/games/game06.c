// Labirinth

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"
#include <math.h>
#include "../textures.h"

typedef enum {
    MA_NONE = 0x00,
    MA_WALLLEFT = 0x01,
    MA_WALLRIGHT = 0x02,
    MA_WALLUP = 0x04,
    MA_WALLDOWN = 0x08,
    MA_START = 0x10,
    MA_GOAL = 0x20
} MapArea;

typedef struct {
    double posX;
    double posY;
    double dirX;
    double dirY;
    double planeX;
    double planeY;
    MapArea _map[16][16];
    uint8_t _goalx;
    uint8_t _goaly;
} Game6Memory;

static Game6Memory * const memory = (Game6Memory*)(&mainMemory.sharedMemory.memblocks[0]);

static void _ClearWall(uint8_t x, uint8_t y, MapArea walldirection)
{
    if (walldirection == MA_WALLUP)
    {
        if (y == 0) return;
        memory->_map[x][y] &= ~MA_WALLUP;
        memory->_map[x][y-1] &= ~MA_WALLDOWN;
    }
    else if (walldirection == MA_WALLDOWN)
    {
        if (y == 15) return;
        memory->_map[x][y] &= ~MA_WALLDOWN;
        memory->_map[x][y+1] &= ~MA_WALLUP;
    }
    else if (walldirection == MA_WALLLEFT)
    {
        if (x == 0) return;
        memory->_map[x][y] &= ~MA_WALLLEFT;
        memory->_map[x-1][y] &= ~MA_WALLRIGHT;
    }
    else if (walldirection == MA_WALLRIGHT)
    {
        if (x == 15) return;
        memory->_map[x][y] &= ~MA_WALLRIGHT;
        memory->_map[x+1][y] &= ~MA_WALLLEFT;
    }
}

static void _GenerateWall(uint8_t x, uint8_t y, MapArea walldirection)
{
    if (walldirection == MA_WALLUP)
    {
        if (y == 0) return;
        memory->_map[x][y] |= MA_WALLUP;
        memory->_map[x][y-1] |= MA_WALLDOWN;
    }
    else if (walldirection == MA_WALLDOWN)
    {
        if (y == 15) return;
        memory->_map[x][y] |= MA_WALLDOWN;
        memory->_map[x][y+1] |= MA_WALLUP;
    }
    else if (walldirection == MA_WALLLEFT)
    {
        if (x == 0) return;
        memory->_map[x][y] |= MA_WALLLEFT;
        memory->_map[x-1][y] |= MA_WALLRIGHT;
    }
    else if (walldirection == MA_WALLRIGHT)
    {
        if (x == 15) return;
        memory->_map[x][y] |= MA_WALLRIGHT;
        memory->_map[x+1][y] |= MA_WALLLEFT;
    }
}

static void _GenerateRecursively(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t lenx = x1 - x0 + 1;
    uint8_t leny = y1 - y0 + 1;
    if (lenx < 2 || leny < 2)
        return;
    uint8_t rand = IF_Random() & 0xff;
    uint8_t x_divider = (rand % (lenx-1)); // [x0-x_div] [x_div+1 - x1]
    uint8_t y_divider = (rand % (leny-1)); // [y0-y_div] [y_div+1 - y1]

    for (uint8_t i=0; i<lenx; i++)
        _GenerateWall(x0 + i, y0 + y_divider, MA_WALLDOWN);
    for (uint8_t i=0; i<leny; i++)
        _GenerateWall(x0 + x_divider, y0 + i, MA_WALLRIGHT);
    rand = IF_Random() & 0xff;
    uint8_t x1_door = (rand % (x_divider+1)); 
    uint8_t x2_door = (rand % (lenx - x_divider - 1));
    rand = IF_Random() & 0xff;
    uint8_t y1_door = (rand % (y_divider+1));
    uint8_t y2_door = (rand % (leny - y_divider - 1));
    rand = IF_Random() & 0xff;
    uint8_t freewall = rand & 0x03;
    if (freewall != 0x00)
    {
        _ClearWall(x0 + x1_door, y0 + y_divider, MA_WALLDOWN);
    }
    if (freewall != 0x01)
    {
        _ClearWall(x0 + x_divider + 1 + x2_door, y0 + y_divider, MA_WALLDOWN);
    }
    if (freewall != 0x02)
    {
        _ClearWall(x0 + x_divider, y0 + y1_door, MA_WALLRIGHT);
    }
    if (freewall != 0x03)
    {
        _ClearWall(x0 + x_divider, y0 + y_divider + 1 + y2_door, MA_WALLRIGHT);
    }
    _GenerateRecursively(x0,             y0,             x0+x_divider, y0+y_divider);   // UP_LEFT
    _GenerateRecursively(x0,             y0+y_divider+1, x0+x_divider, y1);             // DOWN_LEFT
    _GenerateRecursively(x0+x_divider+1, y0+y_divider+1, x1,           y1);             // DOWN_RIGHT
    _GenerateRecursively(x0+x_divider+1, y0,             x1,           y0+y_divider);   // UP_RIGHT
}

static void CreateLabirinth()
{
    for (uint8_t x=0; x<16; x++)
        for (uint8_t y=0; y<16; y++)
        {
            memory->_map[x][y] = MA_NONE;
            if (x == 0) memory->_map[x][y] |= MA_WALLLEFT;
            if (x == 15) memory->_map[x][y] |= MA_WALLRIGHT;
            if (y == 0) memory->_map[x][y] |= MA_WALLUP;
            if (y == 15) memory->_map[x][y] |= MA_WALLDOWN;
        }
    
    _GenerateRecursively(0, 0, 15, 15);
}

#define NUM_TICKS 10

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

static uint8_t IsGoalAchieved()
{
    return (uint8_t)memory->posX == memory->_goalx && (uint8_t)memory->posY == memory->_goaly;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    memory->posX = 0.5, memory->posY = 0.5;  //x and y start position
    memory->dirX = 1.0, memory->dirY = 0.0; //initial direction vector
    memory->planeX = 0.0, memory->planeY = 0.66; //the 2d raycaster version of camera plane
    CreateLabirinth();
    do
    {
        memory->_goalx = 15;
        memory->_goaly = 15;
    } while(IsGoalAchieved());
    memory->_map[memory->_goalx][memory->_goalx] |= MA_GOAL;
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLLEFT);
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLRIGHT);
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLUP);
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLDOWN);
    mainMemory._lastRenderedTick = CalculateTick();
    // Prepare texture
    GPU_ClearTexture();
    Texture_CopyFrom(texture_rpi);
}


#define SCR_WIDTH  (GPU_X/2)
#define SCR_HEIGHT GPU_Y

#define SPEED_FACTOR 0.3
#define TURN_FACTOR 1.3

#include <stdio.h>

static void G_Update(void)
{
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
    uint32_t nowTick = CalculateTick();
    
    while (mainMemory._lastRenderedTick < nowTick)
    {
	    KeyPressedEnum keyboard = Keyboard_GetPressedKeys();
        mainMemory._lastRenderedTick++;
        if (keyboard_held != KEY_NONE)
        {
            uint8_t move_complete = 0;
            int8_t posX_before = (int8_t) memory->posX;
            int8_t posY_before = (int8_t) memory->posY;

            if (keyboard_held & KEY_UP)
            {    
                memory->posX += SPEED_FACTOR*memory->dirX;
                memory->posY += SPEED_FACTOR*memory->dirY;
                move_complete = 1;
            }
            if (keyboard_held & KEY_DOWN)
            {
                memory->posX -= SPEED_FACTOR*memory->dirX;
                memory->posY -= SPEED_FACTOR*memory->dirY;
                move_complete = 1;
            }
            if (move_complete)
            {
                if (memory->posX < 0.05)
                    memory->posX = 0.05;
                else if (memory->posX > 15.95)
                    memory->posX = 15.95;
                if (memory->posY<=0.05)
                    memory->posY = 0.05;
                else if (memory->posY > 15.95)
                    memory->posY = 15.95;
                int8_t posX_after = (int8_t) memory->posX;
                int8_t posY_after = (int8_t) memory->posY;
                if (posX_before != posX_after)
                {
                    int8_t diff = posX_after - posX_before;
                    if (diff < 0)
                    {
                        if (memory->_map[posX_before][posY_before] & MA_WALLLEFT)
                            memory->posX = (double)posX_before + 0.1;
                    }
                    else
                    {
                        if (memory->_map[posX_before][posY_before] & MA_WALLRIGHT)
                            memory->posX = (double)posX_before + 0.9;
                    }
                }
                if (posY_before != posY_after)
                {
                    int8_t diff = posY_after - posY_before;
                    if (diff < 0)
                    {
                        if (memory->_map[posX_before][posY_before] & MA_WALLUP)
                            memory->posY = (double)posY_before + 0.1;
                    }
                    else
                    {
                        if (memory->_map[posX_before][posY_before] & MA_WALLDOWN)
                            memory->posY = (double)posY_before + 0.9;
                    }
                }
                if (IsGoalAchieved())
                {
                    ResetGame();
                }
            }
            if (keyboard_held & KEY_LEFT)
            {
                const double rotSpeed = TURN_FACTOR/NUM_TICKS;
                double oldDirX = memory->dirX;
                memory->dirX = memory->dirX * cos(-rotSpeed) - memory->dirY * sin(-rotSpeed);
                memory->dirY = oldDirX * sin(-rotSpeed) + memory->dirY * cos(-rotSpeed);
                double oldPlaneX = memory->planeX;
                memory->planeX = memory->planeX * cos(-rotSpeed) - memory->planeY * sin(-rotSpeed);
                memory->planeY = oldPlaneX * sin(-rotSpeed) + memory->planeY * cos(-rotSpeed);
            }
            if (keyboard_held & KEY_RIGHT)
            {
                const double rotSpeed = -TURN_FACTOR/NUM_TICKS;
                double oldDirX = memory->dirX;
                memory->dirX = memory->dirX * cos(-rotSpeed) - memory->dirY * sin(-rotSpeed);
                memory->dirY = oldDirX * sin(-rotSpeed) + memory->dirY * cos(-rotSpeed);
                double oldPlaneX = memory->planeX;
                memory->planeX = memory->planeX * cos(-rotSpeed) - memory->planeY * sin(-rotSpeed);
                memory->planeY = oldPlaneX * sin(-rotSpeed) + memory->planeY * cos(-rotSpeed);
            }
        }
    }
}

// https://lodev.org/cgtutor/raycasting.html

static uint8_t CheckIfWallHit(int x, int y, MapArea goal)
{
    if (x < 0 || x > 15 || y < 0 || y > 15)
        return 1;
    return (memory->_map[x][y] & goal) ? 1 : 0;
}


static void DrawWall(int16_t x)
{
    double cameraX = 2 * x / (double)SCR_WIDTH - 1; //x-coordinate in camera space
    double rayDirX = memory->dirX + memory->planeX * cameraX;
    double rayDirY = memory->dirY + memory->planeY * cameraX;

    int mapX = (int)(memory->posX);
    int mapY = (int)(memory->posY);
    double posX = (memory->posX);
    double posY = (memory->posY);

    double sideDistX;
    double sideDistY;

    double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1 / rayDirX);
    double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1 / rayDirY);

    double perpWallDist;

    int stepX;
    int stepY;

    int hit = 0;
    int side;

    MapArea goal = MA_NONE;
    if (rayDirX < 0)
    {
        stepX = -1;
        sideDistX = (posX - mapX) * deltaDistX;
        goal |= MA_WALLRIGHT;
    }
    else
    {
        stepX = 1;
        sideDistX = (mapX + 1.0 - posX) * deltaDistX;
        goal |= MA_WALLLEFT;
    }
    if (rayDirY < 0)
    {
        stepY = -1;
        sideDistY = (posY - mapY) * deltaDistY;
        goal |= MA_WALLDOWN;
    }
    else
    {
        stepY = 1;
        sideDistY = (mapY + 1.0 - posY) * deltaDistY;
        goal |= MA_WALLUP;
    }
    while (hit == 0)
    {
        MapArea heregoal;
        if (sideDistX < sideDistY)
        {
            sideDistX += deltaDistX;
            mapX += stepX;
            side = 0;
            heregoal = MA_WALLLEFT | MA_WALLRIGHT;
        }
        else
        {
            sideDistY += deltaDistY;
            mapY += stepY;
            side = 1;
            heregoal = MA_WALLUP | MA_WALLDOWN;
        }
        if (CheckIfWallHit(mapX, mapY, goal & heregoal))
            hit = 1;
    }
    if (side == 0)
        perpWallDist = (sideDistX - deltaDistX);
    else
        perpWallDist = (sideDistY - deltaDistY);

    int lineHeight = (int)(SCR_HEIGHT / perpWallDist);
    int drawStart = -lineHeight / 2 + SCR_HEIGHT / 2;
    if (drawStart < 0)
        drawStart = 0;
    int drawEnd = lineHeight / 2 + SCR_HEIGHT / 2;
    if (drawEnd >= SCR_HEIGHT)
        drawEnd = SCR_HEIGHT - 1;

    GPU_Color color;
    uint8_t drawTexture = 0;
    switch ((mapX + mapY) & 0x07)
    {
    case 0:
        color = C_WHITE;
        break;
    case 1:
        color = C_BLUE;
        break;
    case 2:
        color = C_YELLOW;
        break;
    case 3:
        color = C_RED;
        break;
    case 4:
        color = C_GREEN;
        break;
    case 5:
        color = C_GRAY_LIGHT;
        break;
    case 6:
        color = C_GRAY_MEDIUM;
        break;
    case 7:
    default:
        drawTexture = 1;   
        break;
    }
    if (side)
    {
        color = ((color & 0xff00)>>8) | ((color & 0x00ff)<<8);
        int16_t r = (color & 0xf800)>>(11-3);
		int16_t g = (color & 0x07e0)>>(5-2);
		int16_t b = (color & 0x001f)<<3;
        r = (r>32) ? r-32 : 0;
        g = (g>32) ? g-32 : 0;
        b = (b>32) ? b-32 : 0;
        r &= 0xf8;
        g &= 0xfc;
        b &= 0xf8;
        color = (r << (11-3)) | (g << (5-2)) | (b>>3);
        color = ((color & 0xff00)>>8) | ((color & 0x00ff)<<8);
    }
    
    if (!drawTexture)
    {
        GPU_DrawLine(color, x, drawStart, x, drawEnd);
        return;
    }
    double wallX; //where exactly the wall was hit
    if (side == 0)
        wallX = posY + perpWallDist * rayDirY;
    else
        wallX = posX + perpWallDist * rayDirX;
    wallX -= floor((wallX));
    int texX = (int)(wallX * (double)TEXTURE_SIZE);
    if (side == 0 && rayDirX > 0)
        texX = TEXTURE_SIZE - texX - 1;
    if (side == 1 && rayDirY < 0)
        texX = TEXTURE_SIZE - texX - 1;

    double step = 1.0 * TEXTURE_SIZE / lineHeight;
    double texPos = (drawStart - SCR_HEIGHT / 2 + lineHeight / 2) * step;
    texX = TEXTURE_SIZE - 1 - texX;
    for (int y = drawStart; y < drawEnd; y++)
    {
        int texY = (int)texPos & (TEXTURE_SIZE - 1);
        texPos += step;
        GPU_Color pixel = Texture_GetPixel(texX, texY);
        if (side)
        {
            pixel = ((pixel & 0xff00)>>8) | ((pixel & 0x00ff)<<8);
            int16_t r = (pixel & 0xf800)>>(11-3);
            int16_t g = (pixel & 0x07e0)>>(5-2);
            int16_t b = (pixel & 0x001f)<<3;
            r = (r>32) ? r-32 : 0;
            g = (g>32) ? g-32 : 0;
            b = (b>32) ? b-32 : 0;
            r &= 0xf8;
            g &= 0xfc;
            b &= 0xf8;
            pixel = (r << (11-3)) | (g << (5-2)) | (b>>3);
            pixel = ((pixel & 0xff00)>>8) | ((pixel & 0x00ff)<<8);
        }
        GPU_DrawPixel(pixel, x, y);
    }
}


#define BLOCK_SIZE ( (GPU_X/2 < GPU_Y ) ? (GPU_X/32) : (GPU_Y/16) )
#define START_X ( GPU_X/2 + (GPU_X/2-16*BLOCK_SIZE)/2)
#define START_Y ( (GPU_Y-16*BLOCK_SIZE)/2)

static void G_Draw(void)
{
    GPU_ClearFramebuffers();

    // Draw Map
    uint8_t x = (uint8_t)memory->posX;
    uint8_t y = (uint8_t)memory->posY;
    // Draw player
    GPU_DrawFilledSquare(C_GREEN, START_X + x*BLOCK_SIZE, START_Y + y*BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
    for (uint8_t i=0; i<16; i++)
        for (uint8_t j=0; j<16; j++)
        {
            if (memory->_map[i][j] & MA_WALLLEFT)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE,              START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1);
            if (memory->_map[i][j] & MA_WALLRIGHT)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE,              START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1);
            if (memory->_map[i][j] & MA_WALLUP)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE,              START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE);
            if (memory->_map[i][j] & MA_WALLDOWN)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1, START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1);
            if (memory->_map[i][j] & MA_GOAL)
                GPU_DrawFilledSquare(C_BLUE, START_X + i*BLOCK_SIZE,      START_Y + j*BLOCK_SIZE,              BLOCK_SIZE, BLOCK_SIZE);
        }
    // Draw FirstPerson
    for (int16_t i=0; i<GPU_X/2; i++)
    {
        DrawWall(i);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G6_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
