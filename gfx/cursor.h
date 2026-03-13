/*
 * cursor.h - Mouse cursor rendering.
 */

#ifndef CURSOR_H
#define CURSOR_H

#include "blit.h"

#define CURSOR_W  12
#define CURSOR_HEIGHT  19

/**
 * Initialize cursor state.
 */
void cursor_init(void);

/**
 * Update cursor position (called from mouse handler).
 */
void cursor_move(int dx, int dy);

/**
 * Set cursor position absolutely.
 */
void cursor_set(int x, int y);

/**
 * Get cursor X position.
 */
int cursor_x(void);

/**
 * Get cursor Y position.
 */
int cursor_y(void);

/**
 * Draw the cursor at its current position.
 * Call AFTER drawing all windows.
 */
void cursor_draw(void);

#endif /* CURSOR_H */
