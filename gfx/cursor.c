/*
 * cursor.c - Mouse cursor rendering (12x19 arrow bitmap).
 */

#include "cursor.h"
#include "blit.h"
#include "../drv/fb.h"
#include <stdint.h>

static int cur_x, cur_y;

/* 12x19 arrow cursor bitmap (1=black, 2=white, 0=transparent). */
static const uint8_t cursor_bmp[CURSOR_HEIGHT][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0},
};

void cursor_init(void)
{
    cur_x = 0;
    cur_y = 0;
}

void cursor_move(int dx, int dy)
{
    cur_x += dx;
    cur_y += dy;

    /* Clamp to screen. */
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if (fb_info.width && cur_x >= (int)fb_info.width)
        cur_x = (int)fb_info.width - 1;
    if (fb_info.height && cur_y >= (int)fb_info.height)
        cur_y = (int)fb_info.height - 1;
}

void cursor_set(int x, int y)
{
    cur_x = x;
    cur_y = y;
}

int cursor_x(void) { return cur_x; }
int cursor_y(void) { return cur_y; }

void cursor_draw(void)
{
    if (!fb_info.addr || fb_info.bpp != 32)
        return;

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            uint8_t v = cursor_bmp[row][col];
            if (v == 0)
                continue;
            color_t c = (v == 1) ? rgb(0, 0, 0) : rgb(255, 255, 255);
            fb_putpixel(cur_x + col, cur_y + row, c);
        }
    }
}
