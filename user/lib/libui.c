#include "../include/libui.h"
#include "../include/syscall_nums.h"

#ifdef TSUKASA_USERLIB_KERNEL
extern uintptr_t syscall_handler(uintptr_t num,
                                 uintptr_t arg1,
                                 uintptr_t arg2,
                                 uintptr_t arg3,
                                 uintptr_t arg4,
                                 uintptr_t arg5);
#endif

static long syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
#ifdef TSUKASA_USERLIB_KERNEL
    return (long)syscall_handler((uintptr_t)n,
                                 (uintptr_t)a1,
                                 (uintptr_t)a2,
                                 (uintptr_t)a3,
                                 (uintptr_t)a4,
                                 (uintptr_t)a5);
#else
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory"
    );
    return ret;
#endif
}

static uint64_t pack_i32(int a, int b)
{
    return ((uint64_t)(uint32_t)b << 32) | (uint64_t)(uint32_t)a;
}

ui_window_t ui_window_create(const char *title, int x, int y, int w, int h)
{
    return (ui_window_t)syscall5(
        SYS_GUI,
        GUI_CMD_WINDOW_CREATE,
        (long)title,
        (long)pack_i32(x, y),
        (long)pack_i32(w, h),
        0
    );
}

int ui_window_destroy(ui_window_t win)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_WINDOW_DESTROY, (long)win, 0, 0, 0);
}

int ui_window_set_title_ex(ui_window_t win, const char *title)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_WINDOW_SET_TITLE, (long)win, (long)title, 0, 0);
}

int ui_window_set_resizable_ex(ui_window_t win, bool resizable)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_WINDOW_SET_RESIZABLE, (long)win, (long)resizable, 0, 0);
}

int ui_get_screen_size_ex(uint64_t *out_w, uint64_t *out_h)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_GET_SCREEN_SIZE, (long)out_w, (long)out_h, 0, 0);
}

int ui_draw_rect_ex(ui_window_t win, int x, int y, int w, int h, uint32_t color)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_DRAW_RECT, (long)win, (long)pack_i32(x, y), (long)pack_i32(w, h), (long)color);
}

int ui_draw_rounded_rect_filled_ex(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color)
{
    return (int)syscall5(
        SYS_GUI,
        GUI_CMD_DRAW_ROUNDED_RECT_FILLED,
        (long)win,
        (long)pack_i32(x, y),
        (long)pack_i32(w, h),
        (long)pack_i32(radius, (int)color)
    );
}

int ui_draw_string_ex(ui_window_t win, int x, int y, const char *str, uint32_t color)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_DRAW_STRING, (long)win, (long)pack_i32(x, y), (long)str, (long)color);
}

int ui_draw_image_ex(ui_window_t win, int x, int y, int w, int h, const uint32_t *image_data)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_DRAW_IMAGE, (long)win, (long)pack_i32(x, y), (long)pack_i32(w, h), (long)image_data);
}

int ui_mark_dirty_ex(ui_window_t win, int x, int y, int w, int h)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_MARK_DIRTY, (long)win, (long)pack_i32(x, y), (long)pack_i32(w, h), 0);
}

uint32_t ui_get_string_width(const char *str)
{
    return (uint32_t)syscall5(SYS_GUI, GUI_CMD_GET_STRING_WIDTH, (long)str, 0, 0, 0);
}

uint32_t ui_get_font_height(void)
{
    return (uint32_t)syscall5(SYS_GUI, GUI_CMD_GET_FONT_HEIGHT, 0, 0, 0, 0);
}

bool ui_get_event(ui_window_t win, ui_event_t *ev)
{
    return ui_get_event_ex(win, ev) == UI_OK;
}

int ui_get_event_ex(ui_window_t win, ui_event_t *ev)
{
    return (int)syscall5(SYS_GUI, GUI_CMD_GET_EVENT, (long)win, (long)ev, 0, 0);
}

void ui_window_set_title(ui_window_t win, const char *title)
{
    (void)ui_window_set_title_ex(win, title);
}

void ui_window_set_resizable(ui_window_t win, bool resizable)
{
    (void)ui_window_set_resizable_ex(win, resizable);
}

void ui_get_screen_size(uint64_t *out_w, uint64_t *out_h)
{
    (void)ui_get_screen_size_ex(out_w, out_h);
}

void ui_draw_rect(ui_window_t win, int x, int y, int w, int h, uint32_t color)
{
    (void)ui_draw_rect_ex(win, x, y, w, h, color);
}

void ui_draw_rounded_rect_filled(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color)
{
    (void)ui_draw_rounded_rect_filled_ex(win, x, y, w, h, radius, color);
}

void ui_draw_string(ui_window_t win, int x, int y, const char *str, uint32_t color)
{
    (void)ui_draw_string_ex(win, x, y, str, color);
}

void ui_draw_image(ui_window_t win, int x, int y, int w, int h, const uint32_t *image_data)
{
    (void)ui_draw_image_ex(win, x, y, w, h, image_data);
}

void ui_mark_dirty(ui_window_t win, int x, int y, int w, int h)
{
    (void)ui_mark_dirty_ex(win, x, y, w, h);
}
