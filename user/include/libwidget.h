#ifndef USER_LIBWIDGET_H
#define USER_LIBWIDGET_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *user_data;
    void (*draw_rect)(void *user_data, int x, int y, int w, int h, uint32_t color);
    void (*draw_rounded_rect_filled)(void *user_data, int x, int y, int w, int h, int radius, uint32_t color);
    void (*draw_string)(void *user_data, int x, int y, const char *str, uint32_t color);
    int (*measure_string_width)(void *user_data, const char *str);
    void (*mark_dirty)(void *user_data, int x, int y, int w, int h);
    bool use_light_theme;
} widget_context_t;

void widget_context_init(widget_context_t *ctx,
                         void *user_data,
                         void (*draw_rect)(void *user_data, int x, int y, int w, int h, uint32_t color),
                         void (*draw_rounded_rect_filled)(void *user_data, int x, int y, int w, int h, int radius, uint32_t color),
                         void (*draw_string)(void *user_data, int x, int y, const char *str, uint32_t color),
                         int (*measure_string_width)(void *user_data, const char *str),
                         void (*mark_dirty)(void *user_data, int x, int y, int w, int h),
                         bool use_light_theme);

typedef struct {
    int x, y, w, h;
    const char *text;
    bool pressed;
    bool hovered;
    void (*on_click)(void *user_data);
} widget_button_t;

void widget_button_init(widget_button_t *btn, int x, int y, int w, int h, const char *text);
void widget_button_draw(widget_context_t *ctx, widget_button_t *btn);
bool widget_button_handle_mouse(widget_button_t *btn, int mx, int my, bool mouse_down, bool mouse_clicked, void *user_data);

typedef struct {
    int x, y, w, h;
    int content_height;
    int scroll_y;
    bool is_dragging;
    int drag_start_my;
    int drag_start_scroll_y;
    void (*on_scroll)(void *user_data, int new_scroll_y);
} widget_scrollbar_t;

void widget_scrollbar_init(widget_scrollbar_t *sb, int x, int y, int w, int h);
void widget_scrollbar_update(widget_scrollbar_t *sb, int content_height, int scroll_y);
void widget_scrollbar_draw(widget_context_t *ctx, widget_scrollbar_t *sb);
bool widget_scrollbar_handle_mouse(widget_scrollbar_t *sb, int mx, int my, bool mouse_down, void *user_data);

typedef struct {
    int x, y, w, h;
    char *text;
    int max_len;
    int cursor_pos;
    bool focused;
    void (*on_change)(void *user_data);
} widget_textbox_t;

void widget_textbox_init(widget_textbox_t *tb, int x, int y, int w, int h, char *buffer, int max_len);
void widget_textbox_draw(widget_context_t *ctx, widget_textbox_t *tb);
bool widget_textbox_handle_mouse(widget_textbox_t *tb, int mx, int my, bool mouse_clicked, void *user_data);
bool widget_textbox_handle_key(widget_textbox_t *tb, char c, void *user_data);

typedef struct {
    int x, y, w, h;
    const char **items;
    int item_count;
    int selected_idx;
    bool is_open;
    void (*on_select)(void *user_data, int new_idx);
} widget_dropdown_t;

void widget_dropdown_init(widget_dropdown_t *dd, int x, int y, int w, int h, const char **items, int count);
void widget_dropdown_draw(widget_context_t *ctx, widget_dropdown_t *dd);
bool widget_dropdown_handle_mouse(widget_dropdown_t *dd, int mx, int my, bool mouse_clicked, void *user_data);

typedef struct {
    int x, y, w, h;
    const char *text;
    bool checked;
    bool is_radio;
    void (*on_toggle)(void *user_data, bool new_state);
} widget_checkbox_t;

void widget_checkbox_init(widget_checkbox_t *cb, int x, int y, int w, int h, const char *text, bool is_radio);
void widget_checkbox_draw(widget_context_t *ctx, widget_checkbox_t *cb);
bool widget_checkbox_handle_mouse(widget_checkbox_t *cb, int mx, int my, bool mouse_clicked, void *user_data);

#endif /* USER_LIBWIDGET_H */
