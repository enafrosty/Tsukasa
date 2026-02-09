/*
 * hello_gui.c - Hello World graphical app.
 * Creates shared buffer, draws a window, runs in user or kernel context.
 */

#include "../lib/syscall.h"
#include <stddef.h>
#include <stdint.h>

typedef uint32_t color_t;

static inline color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void draw_rect(void *fb, int pitch, int x, int y, int w, int h, color_t color)
{
    uint32_t *row = (uint32_t *)((char *)fb + y * pitch);
    for (int dy = 0; dy < h; dy++) {
        uint32_t *p = (uint32_t *)((char *)row + dy * pitch) + x;
        for (int dx = 0; dx < w; dx++)
            p[dx] = color;
    }
}

void hello_gui_main(void)
{
    int id = shm_create(1024 * 768 * 4);
    if (id < 0)
        return;
    void *buf = shm_attach(id);
    if (!buf)
        return;

    int pitch = 1024 * 4;
    draw_rect(buf, pitch, 100, 100, 200, 150, rgb(64, 64, 128));
    draw_rect(buf, pitch, 110, 110, 180, 130, rgb(200, 200, 220));

    for (;;)
        yield();
}
