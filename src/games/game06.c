// Labirinth

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"
#include <math.h>
#include <string.h>
#include "../textures.h"

typedef enum {
    MA_NONE = 0x00,
    MA_WALLLEFT = 0x01,
    MA_WALLRIGHT = 0x02,
    MA_WALLUP = 0x04,
    MA_WALLDOWN = 0x08,
    MA_START = 0x10,
    MA_GOAL = 0x20,
    MA_GEM = 0x40
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
    uint32_t _startTime;
    uint32_t _finishTime;
    uint8_t _visited[16][16];   /* breadcrumb trail shown on the minimap */
    uint8_t _gemsLeft;          /* gems still hidden in the maze */
    uint8_t _gemFlash;          /* ticks of the "+GEM" popup */
} Game6Memory;

static Game6Memory * const memory = (Game6Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestTime = 0;   /* fastest solve of this session, microseconds */

#define LAB_GEMS 6

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
    /* independent draws: deriving both dividers from one random value made
       the horizontal and the vertical split correlated */
    uint8_t x_divider = (uint8_t)(IF_Random() % (lenx-1)); // [x0-x_div] [x_div+1 - x1]
    uint8_t y_divider = (uint8_t)(IF_Random() % (leny-1)); // [y0-y_div] [y_div+1 - y1]

    for (uint8_t i=0; i<lenx; i++)
        _GenerateWall(x0 + i, y0 + y_divider, MA_WALLDOWN);
    for (uint8_t i=0; i<leny; i++)
        _GenerateWall(x0 + x_divider, y0 + i, MA_WALLRIGHT);
    uint8_t x1_door = (uint8_t)(IF_Random() % (x_divider+1));
    uint8_t x2_door = (uint8_t)(IF_Random() % (lenx - x_divider - 1));
    uint8_t y1_door = (uint8_t)(IF_Random() % (y_divider+1));
    uint8_t y2_door = (uint8_t)(IF_Random() % (leny - y_divider - 1));
    uint8_t freewall = (uint8_t)(IF_Random() & 0x03);
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
    memory->_goalx = 15;
    memory->_goaly = 15;
    memory->_map[memory->_goalx][memory->_goaly] |= MA_GOAL;
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLLEFT);
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLRIGHT);
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLUP);
    _ClearWall(memory->_goalx, memory->_goaly, MA_WALLDOWN);
    memory->_startTime = IF_GetCurrentTime();
    memory->_finishTime = 0;
    for (uint8_t i = 0; i < 16; i++)
        for (uint8_t j = 0; j < 16; j++)
            memory->_visited[i][j] = 0;
    memory->_visited[0][0] = 1;
    /* scatter gems through the maze: collecting them is optional, but they
       pull the player into corners the direct route would never visit */
    memory->_gemsLeft = 0;
    for (uint8_t g = 0; g < LAB_GEMS; g++)
    {
        for (uint8_t tries = 0; tries < 40; tries++)
        {
            uint8_t gx = (uint8_t)Random_Range(16);
            uint8_t gy = (uint8_t)Random_Range(16);
            if ((gx == 0 && gy == 0) || (gx == memory->_goalx && gy == memory->_goaly))
                continue;
            if (memory->_map[gx][gy] & MA_GEM)
                continue;
            memory->_map[gx][gy] |= MA_GEM;
            memory->_gemsLeft++;
            break;
        }
    }
    memory->_gemFlash = 0;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
    // Prepare texture
    GPU_ClearTexture();
    Texture_CopyFrom(texture_rpi);
}


#define SCR_WIDTH  (GPU_X/2)
#define SCR_HEIGHT GPU_Y

#define SPEED_FACTOR 0.3
#define TURN_FACTOR 1.3

#include <stdio.h>

#define RESET_BUTTON_W (80)
#define RESET_BUTTON_H (30)
#define RESET_BUTTON_X (GPU_X/2 - RESET_BUTTON_W/2)
#define RESET_BUTTON_Y (GPU_Y/2 + 30)

static void G_Update(void)
{
    if (mainMemory._gameState == GS_DONE)
    {
        /* the maze is solved: show the result until the player wants a new one */
        if (mainMemory.touchPressedNew)
        {
            if (mainMemory.touch_X >= RESET_BUTTON_X && mainMemory.touch_X <= RESET_BUTTON_X+RESET_BUTTON_W &&
                mainMemory.touch_Y >= RESET_BUTTON_Y && mainMemory.touch_Y <= RESET_BUTTON_Y+RESET_BUTTON_H)
                G_Init();
        }
        return;
    }
	KeyPressedEnum keyboard_held = Keyboard_GetHeldKeys();
    /* Touch steers through the first person view: the left and right thirds
       turn, the middle column moves forward (top half) or back (bottom half).
       The minimap half stays free so it can still be read while driving. */
    if (mainMemory.touchPressed && mainMemory.touch_X < GPU_X/2)
    {
        if (mainMemory.touch_X < (GPU_X/2)/3)
            keyboard_held |= KEY_LEFT;
        else if (mainMemory.touch_X > 2*(GPU_X/2)/3)
            keyboard_held |= KEY_RIGHT;
        else if (mainMemory.touch_Y < GPU_Y/2)
            keyboard_held |= KEY_UP;
        else
            keyboard_held |= KEY_DOWN;
    }
    uint32_t nowTick = CalculateTick();
    
    while (mainMemory._lastRenderedTick < nowTick)
    {
        mainMemory._lastRenderedTick++;
        if (memory->_gemFlash)
            memory->_gemFlash--;
        memory->_visited[(uint8_t)memory->posX][(uint8_t)memory->posY] = 1;
        /* pick up a gem when entering its cell */
        {
            uint8_t cx = (uint8_t)memory->posX, cy = (uint8_t)memory->posY;
            if (memory->_map[cx][cy] & MA_GEM)
            {
                memory->_map[cx][cy] &= ~MA_GEM;
                if (memory->_gemsLeft)
                    memory->_gemsLeft--;
                memory->_gemFlash = 20;
            }
        }
        if (keyboard_held != KEY_NONE)
        {
            double stepX = 0.0, stepY = 0.0;
            if (keyboard_held & KEY_UP)
            {
                stepX += SPEED_FACTOR*memory->dirX;
                stepY += SPEED_FACTOR*memory->dirY;
            }
            if (keyboard_held & KEY_DOWN)
            {
                stepX -= SPEED_FACTOR*memory->dirX;
                stepY -= SPEED_FACTOR*memory->dirY;
            }
            if (stepX != 0.0 || stepY != 0.0)
            {
                /* resolve the axes one after another so that a diagonal move
                   cannot slip through the corner of a wall: the y check has
                   to use the cell the x move ended up in */
                int8_t cellX = (int8_t)memory->posX;
                int8_t cellY = (int8_t)memory->posY;
                double newX = memory->posX + stepX;
                if (newX < 0.05) newX = 0.05;
                else if (newX > 15.95) newX = 15.95;
                int8_t newCellX = (int8_t)newX;
                if (newCellX < cellX && (memory->_map[cellX][cellY] & MA_WALLLEFT))
                    newX = (double)cellX + 0.1;
                else if (newCellX > cellX && (memory->_map[cellX][cellY] & MA_WALLRIGHT))
                    newX = (double)cellX + 0.9;
                memory->posX = newX;

                cellX = (int8_t)memory->posX;
                double newY = memory->posY + stepY;
                if (newY < 0.05) newY = 0.05;
                else if (newY > 15.95) newY = 15.95;
                int8_t newCellY = (int8_t)newY;
                if (newCellY < cellY && (memory->_map[cellX][cellY] & MA_WALLUP))
                    newY = (double)cellY + 0.1;
                else if (newCellY > cellY && (memory->_map[cellX][cellY] & MA_WALLDOWN))
                    newY = (double)cellY + 0.9;
                memory->posY = newY;

                if (IsGoalAchieved())
                {
                    memory->_finishTime = IF_GetCurrentTime();
                    {
                        uint32_t elapsed = memory->_finishTime - memory->_startTime;
                        if (bestTime == 0 || elapsed < bestTime)
                            bestTime = elapsed;
                    }
                    mainMemory._gameState = GS_DONE;
                    return;
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

/* per column wall distance, so gem sprites can be depth tested against walls */
static double _wallDepth[SCR_WIDTH];

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
    _wallDepth[x] = perpWallDist;

    int lineHeight = (int)(SCR_HEIGHT / perpWallDist);
    int drawStart = -lineHeight / 2 + SCR_HEIGHT / 2;
    if (drawStart < 0)
        drawStart = 0;
    int drawEnd = lineHeight / 2 + SCR_HEIGHT / 2;
    if (drawEnd >= SCR_HEIGHT)
        drawEnd = SCR_HEIGHT - 1;

    GPU_Color color = C_WHITE;
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
    /* side walls are darker, and everything fades into the distance: the fog
       makes the depth of a corridor readable at a glance */
    int16_t fog = (int16_t)(256 - (int)(perpWallDist*22.0));
    if (fog < 70) fog = 70;
    if (fog > 256) fog = 256;
    int16_t light = side ? (int16_t)(fog*200/256) : fog;
    color = GPU_ShadeColor(color, light);
    
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
        GPU_DrawPixel(GPU_ShadeColor(pixel, light), x, y);
    }
}

/* the gems, drawn as small floating diamonds inside the first person view.
   They are billboards: projected with the raycaster camera and depth tested
   against the wall of the column they appear in. */
static void DrawGems(void)
{
    double invDet = 1.0/(memory->planeX*memory->dirY - memory->dirX*memory->planeY);
    for (uint8_t i = 0; i < 16; i++)
        for (uint8_t j = 0; j < 16; j++)
        {
            if (!(memory->_map[i][j] & MA_GEM))
                continue;
            double rx = (double)i + 0.5 - memory->posX;
            double ry = (double)j + 0.5 - memory->posY;
            double tx = invDet*(memory->dirY*rx - memory->dirX*ry);
            double ty = invDet*(-memory->planeY*rx + memory->planeX*ry);
            if (ty < 0.3 || ty > 14.0)
                continue;
            int sx = (int)((SCR_WIDTH/2)*(1.0 + tx/ty));
            if (sx < 2 || sx >= SCR_WIDTH-2)
                continue;
            int size = (int)(SCR_HEIGHT/ty/7.0);
            if (size < 2) size = 2;
            if (size > 13) size = 13;
            /* hidden behind a wall? test the centre and both tips */
            if (ty >= _wallDepth[sx])
                continue;
            if (sx-size >= 0 && ty >= _wallDepth[sx-size])
                continue;
            if (sx+size < SCR_WIDTH && ty >= _wallDepth[sx+size])
                continue;
            int cy = SCR_HEIGHT/2 + size;   /* floating a bit below the horizon */
            GPU_DrawGlow(C_CYAN, (int16_t)sx, (int16_t)cy, (int16_t)(size+3));
            GPU_DrawFilledTriangle(C_CYAN, (int16_t)sx, (int16_t)(cy-size), (int16_t)(sx-size), (int16_t)cy, (int16_t)(sx+size), (int16_t)cy);
            GPU_DrawFilledTriangle(RGB(30,140,190), (int16_t)(sx-size), (int16_t)cy, (int16_t)(sx+size), (int16_t)cy, (int16_t)sx, (int16_t)(cy+size));
            GPU_DrawPixel(C_WHITE, (uint16_t)(sx-size/3), (uint16_t)(cy-size/2));
        }
}


#define BLOCK_SIZE ( (GPU_X/2 < GPU_Y ) ? (GPU_X/32) : (GPU_Y/16) )
#define START_X ( GPU_X/2 + (GPU_X/2-16*BLOCK_SIZE)/2)
#define START_Y ( (GPU_Y-16*BLOCK_SIZE)/2)

static void G_Draw(void)
{
    GPU_ClearFramebuffers();
    /* a bit of sky and floor behind the raycasted view */
    GPU_DrawVerticalGradient(RGB(10,10,30), RGB(30,30,60), 0, 0, GPU_X/2, GPU_Y/2);
    GPU_DrawVerticalGradient(RGB(40,36,30), RGB(12,10,8), 0, GPU_Y/2, GPU_X/2, GPU_Y/2);

    // Draw Map
    uint8_t x = (uint8_t)memory->posX;
    uint8_t y = (uint8_t)memory->posY;
    GPU_DrawPanel(RGB(12,14,36), C_GRAY_DARK, (int16_t)(START_X-3), (int16_t)(START_Y-3),
                  (int16_t)(16*BLOCK_SIZE+6), (int16_t)(16*BLOCK_SIZE+6));
    // Draw player as an arrow pointing into the view direction
    {
        int16_t px = (int16_t)(START_X + x*BLOCK_SIZE + BLOCK_SIZE/2);
        int16_t py = (int16_t)(START_Y + y*BLOCK_SIZE + BLOCK_SIZE/2);
        int16_t tx = (int16_t)(px + (int16_t)(memory->dirX*BLOCK_SIZE));
        int16_t ty = (int16_t)(py + (int16_t)(memory->dirY*BLOCK_SIZE));
        GPU_DrawFilledCircle(C_GREEN, px, py, (int16_t)(BLOCK_SIZE/2-1));
        GPU_DrawLine(C_WHITE, px, py, tx, ty);
    }
    for (uint8_t i=0; i<16; i++)
        for (uint8_t j=0; j<16; j++)
        {
            /* breadcrumb trail of where the player has been */
            if (memory->_visited[i][j] && !(memory->_map[i][j] & MA_GOAL))
                GPU_DrawPixel(RGB(40,70,40), (uint16_t)(START_X + i*BLOCK_SIZE + BLOCK_SIZE/2), (uint16_t)(START_Y + j*BLOCK_SIZE + BLOCK_SIZE/2));
            if (memory->_map[i][j] & MA_WALLLEFT)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE,              START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1);
            if (memory->_map[i][j] & MA_WALLRIGHT)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE,              START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1);
            if (memory->_map[i][j] & MA_WALLUP)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE,              START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE);
            if (memory->_map[i][j] & MA_WALLDOWN)
                GPU_DrawLine(C_GRAY_MEDIUM, START_X + i*BLOCK_SIZE,              START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1, START_X + i*BLOCK_SIZE+BLOCK_SIZE-1, START_Y + j*BLOCK_SIZE+BLOCK_SIZE-1);
            if (memory->_map[i][j] & MA_GEM)
            {
                int16_t gx = (int16_t)(START_X + i*BLOCK_SIZE + BLOCK_SIZE/2);
                int16_t gy = (int16_t)(START_Y + j*BLOCK_SIZE + BLOCK_SIZE/2);
                GPU_DrawFilledTriangle(C_CYAN, gx, (int16_t)(gy-2), (int16_t)(gx-2), gy, (int16_t)(gx+2), gy);
                GPU_DrawFilledTriangle(C_CYAN, (int16_t)(gx-2), gy, (int16_t)(gx+2), gy, gx, (int16_t)(gy+2));
            }
            if (memory->_map[i][j] & MA_GOAL)
            {
                GPU_DrawGlow(C_BLUE, (int16_t)(START_X + i*BLOCK_SIZE + BLOCK_SIZE/2), (int16_t)(START_Y + j*BLOCK_SIZE + BLOCK_SIZE/2), (int16_t)(BLOCK_SIZE/2+2));
                GPU_DrawFilledSquare(C_BLUE, START_X + i*BLOCK_SIZE,      START_Y + j*BLOCK_SIZE,              BLOCK_SIZE, BLOCK_SIZE);
            }
        }
    // Draw FirstPerson
    for (int16_t i=0; i<GPU_X/2; i++)
    {
        DrawWall(i);
    }
    DrawGems();

    /* time and a compass that points towards the goal */
    {
        const FontDescription * fs = &fontDescription[0];
        char text[16];
        uint32_t now = (mainMemory._gameState == GS_DONE) ? memory->_finishTime : IF_GetCurrentTime();
        sprintf(text, "TIME %u", (unsigned)((now - memory->_startTime)/ONE_SECOND));
        GPU_DrawText(C_WHITE, fs, 4, 4, text, (uint16_t)strlen(text));
        sprintf(text, "GEMS %u/%u", (unsigned)(LAB_GEMS - memory->_gemsLeft), (unsigned)LAB_GEMS);
        GPU_DrawText(C_CYAN, fs, 4, 14, text, (uint16_t)strlen(text));
        if (bestTime)
        {
            sprintf(text, "BEST %u", (unsigned)(bestTime/ONE_SECOND));
            GPU_DrawText(C_GRAY_MEDIUM, fs, 4, 24, text, (uint16_t)strlen(text));
        }
        if (memory->_gemFlash)
            GPU_DrawTextOutlined(C_CYAN, C_BLACK, &fontDescription[1], GPU_X/4, 40, "+GEM");
        GPU_DrawText(C_GRAY_DARK, fs, 4, GPU_Y-10, "touch: sides turn, middle walks", 31);
        double toGoalX = (double)memory->_goalx + 0.5 - memory->posX;
        double toGoalY = (double)memory->_goaly + 0.5 - memory->posY;
        /* angle of the goal relative to the view direction */
        double rel = atan2(toGoalY, toGoalX) - atan2(memory->dirY, memory->dirX);
        int16_t cx = (int16_t)(GPU_X/2 - 18), cy = 14;
        int16_t tx = (int16_t)(cx + 9.0*cos(rel));
        int16_t ty = (int16_t)(cy + 9.0*sin(rel));
        GPU_DrawEmptyCircle(C_GRAY_DARK, cx, cy, 11);
        GPU_DrawLine(C_BLUE, cx, cy, tx, ty);
        GPU_DrawFilledCircle(C_BLUE, tx, ty, 2);
    }

    if (mainMemory._gameState == GS_DONE)
    {
        const FontDescription * fb = &fontDescription[2];
        const FontDescription * fm = &fontDescription[1];
        char text[24];
        GPU_DrawPanel(RGB(16,18,48), C_C3, (int16_t)(GPU_X/2-90), (int16_t)(GPU_Y/2-44), 180, 92);
        GPU_DrawTextOutlined(C_C3, C_BLACK, fb, GPU_X/2, (int16_t)(GPU_Y/2-38), "WELL DONE");
        sprintf(text, "TIME %u s   GEMS %u/%u", (unsigned)((memory->_finishTime - memory->_startTime)/ONE_SECOND),
                (unsigned)(LAB_GEMS - memory->_gemsLeft), (unsigned)LAB_GEMS);
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-14), text);
        if (bestTime)
        {
            sprintf(text, "BEST %u s", (unsigned)(bestTime/ONE_SECOND));
            GPU_DrawTextCentered(C_YELLOW, fm, GPU_X/2, (int16_t)(GPU_Y/2+2), text);
        }
        GPU_DrawButton(C_YELLOW, C_BLACK, fb, "RESET", RESET_BUTTON_X, RESET_BUTTON_Y,
                       RESET_BUTTON_W, RESET_BUTTON_H);
    }
}

static void G_Deinit(void)
{

}

const Callbacks G6_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };
