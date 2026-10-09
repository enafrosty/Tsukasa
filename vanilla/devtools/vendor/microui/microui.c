/*
** Copyright (c) 2020 rxi
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in all
** copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
** SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "microui.h"

#define unused(x) ((void) (x))

#define HASH_INITIAL 2166136261u

static void hash(mu_Id *hash, const void *data, int size) {
  const unsigned char *p = data;
  while (size--) {
    *hash = (*hash ^ *p++) * 16777619u;
  }
}


static const mu_Style default_style = {
  /* font | size | padding | spacing | indent */
  NULL, { 68, 10 }, 5, 4, 24,
  /* title_height | scrollbar_size | thumb_size */
  24, 12, 8,
  {
    { 230, 230, 230, 255 }, /* MU_COLOR_TEXT */
    { 25,  25,  25,  255 }, /* MU_COLOR_BORDER */
    { 50,  50,  50,  255 }, /* MU_COLOR_WINDOWBG */
    { 25,  25,  25,  255 }, /* MU_COLOR_TITLEBG */
    { 240, 240, 240, 255 }, /* MU_COLOR_TITLETEXT */
    { 0,   0,   0,   0   }, /* MU_COLOR_PANELBG */
    { 75,  75,  75,  255 }, /* MU_COLOR_BUTTON */
    { 95,  95,  95,  255 }, /* MU_COLOR_BUTTONHOVER */
    { 115, 115, 115, 255 }, /* MU_COLOR_BUTTONFOCUS */
    { 30,  30,  30,  255 }, /* MU_COLOR_BASE */
    { 35,  35,  35,  255 }, /* MU_COLOR_BASEHOVER */
    { 40,  40,  40,  255 }, /* MU_COLOR_BASEFOCUS */
    { 43,  43,  43,  255 }, /* MU_COLOR_SCROLLBASE */
    { 30,  30,  30,  255 }  /* MU_COLOR_SCROLLTHUMB */
  }
};


mu_Vec2 mu_vec2(int x, int y) {
  mu_Vec2 res;
  res.x = x; res.y = y;
  return res;
}


mu_Rect mu_rect(int x, int y, int w, int h) {
  mu_Rect res;
  res.x = x; res.y = y; res.w = w; res.h = h;
  return res;
}


mu_Color mu_color(int r, int g, int b, int a) {
  mu_Color res;
  res.r = r; res.g = g; res.b = b; res.a = a;
  return res;
}


static mu_Rect expand_rect(mu_Rect rect, int n) {
  return mu_rect(rect.x - n, rect.y - n, rect.w + n * 2, rect.h + n * 2);
}


static mu_Rect intersect_rects(mu_Rect r1, mu_Rect r2) {
  int x1 = mu_max(r1.x, r2.x);
  int y1 = mu_max(r1.y, r2.y);
  int x2 = mu_min(r1.x + r1.w, r2.x + r2.w);
  int y2 = mu_min(r1.y + r1.h, r2.y + r2.h);
  if (x2 < x1) { x2 = x1; }
  if (y2 < y1) { y2 = y1; }
  return mu_rect(x1, y1, x2 - x1, y2 - y1);
}


static int rect_overlaps_vec2(mu_Rect r, mu_Vec2 p) {
  return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}


static void draw_frame(mu_Context *ctx, mu_Rect rect, int colorid) {
  mu_draw_rect(ctx, rect, ctx->style->colors[colorid]);
  if (colorid == MU_COLOR_SCROLLBASE  ||
      colorid == MU_COLOR_SCROLLTHUMB ||
      colorid == MU_COLOR_TITLEBG) { return; }
  if (ctx->style->colors[MU_COLOR_BORDER].a) {
    mu_draw_box(ctx, expand_rect(rect, 1), ctx->style->colors[MU_COLOR_BORDER]);
  }
}


void mu_init(mu_Context *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->_style = default_style;
  ctx->style = &ctx->_style;
  ctx->draw_frame = draw_frame;
}


void mu_begin(mu_Context *ctx) {
  ctx->command_list.idx = 0;
  ctx->root_list.idx = 0;
  ctx->scroll_target = NULL;
  ctx->hover_root = ctx->next_hover_root;
  ctx->next_hover_root = NULL;
  ctx->mouse_delta.x = ctx->mouse_pos.x - ctx->last_mouse_pos.x;
  ctx->mouse_delta.y = ctx->mouse_pos.y - ctx->last_mouse_pos.y;
  ctx->last_hover_root_rect = ctx->hover_root_rect;
  ctx->hover_root_rect = ctx->next_hover_root_rect;
  ctx->next_hover_root_rect = mu_rect(0, 0, 0, 0);
}


static int compare_zindex(const void *a, const void *b) {
  return (*(mu_Container**) a)->zindex - (*(mu_Container**) b)->zindex;
}


void mu_end(mu_Context *ctx) {
  int i, n;
  /* check stacks */
  /* sort root containers by zindex */
  n = ctx->root_list.idx;
  qsort(ctx->root_list.items, n, sizeof(mu_Container*), compare_zindex);
  /* set root command list jumps */
  for (i = 0; i < n; i++) {
    mu_Container *cnt = ctx->root_list.items[i];
    if (i == 0) {
      /* command list start jumps to first root container */
      mu_Command *cmd = (mu_Command*) ctx->command_list.items;
      cmd->jump.dst = (char*) cnt->head + sizeof(mu_JumpCommand);
    } else {
      /* previous container jumps to this one */
      mu_Container *prev = ctx->root_list.items[i - 1];
      prev->tail->jump.dst = (char*) cnt->head + sizeof(mu_JumpCommand);
    }
    if (i == n - 1) {
      /* last container jumps to end of command list */
      cnt->tail->jump.dst = ctx->command_list.items + ctx->command_list.idx;
    }
  }
  /* clear input state */
  ctx->mouse_pressed = 0;
  ctx->key_pressed = 0;
  ctx->input_text[0] = '\0';
  ctx->scroll_delta = mu_vec2(0, 0);
  ctx->last_mouse_pos = ctx->mouse_pos;
  /* update focus */
  if (ctx->updated_focus) {
    ctx->focus = ctx->updated_focus;
    ctx->updated_focus = 0;
  }
}


void mu_set_focus(mu_Context *ctx, mu_Id id) {
  ctx->focus = id;
  ctx->updated_focus = id;
}


/* 32bit fnv-1a hash */
mu_Id mu_get_id(mu_Context *ctx, const void *data, int size) {
  int idx = ctx->id_stack.idx;
  mu_Id res = (idx > 0) ? ctx->id_stack.items[idx - 1] : HASH_INITIAL;
  hash(&res, data, size);
  return res;
}


void mu_push_id(mu_Context *ctx, const void *data, int size) {
  int idx = ctx->id_stack.idx;
  mu_Id res = (idx > 0) ? ctx->id_stack.items[idx - 1] : HASH_INITIAL;
  hash(&res, data, size);
  ctx->id_stack.items[ctx->id_stack.idx++] = res;
}


void mu_pop_id(mu_Context *ctx) {
  ctx->id_stack.idx--;
}


void mu_push_clip_rect(mu_Context *ctx, mu_Rect rect) {
  mu_Rect last = mu_get_clip_rect(ctx);
  ctx->clip_stack.items[ctx->clip_stack.idx++] = intersect_rects(rect, last);
}


void mu_pop_clip_rect(mu_Context *ctx) {
  ctx->clip_stack.idx--;
}


mu_Rect mu_get_clip_rect(mu_Context *ctx) {
  int idx = ctx->clip_stack.idx;
  if (idx > 0) {
    return ctx->clip_stack.items[idx - 1];
  }
  return mu_rect(0, 0, 1000000, 1000000);
}


int mu_check_clip(mu_Context *ctx, mu_Rect r) {
  mu_Rect cr = mu_get_clip_rect(ctx);
  if (r.x > cr.x + cr.w || r.x + r.w < cr.x ||
      r.y > cr.y + cr.h || r.y + r.h < cr.y) { return MU_CLIP_ALL; }
  if (r.x >= cr.x && r.x + r.w <= cr.x + cr.w &&
      r.y >= cr.y && r.y + r.h <= cr.y + cr.h) { return 0; }
  return MU_CLIP_PART;
}


static void push_layout(mu_Context *ctx, mu_Rect body, mu_Vec2 scroll) {
  mu_Layout layout;
  int width = 0;
  memset(&layout, 0, sizeof(layout));
  layout.body = mu_rect(body.x - scroll.x, body.y - scroll.y, body.w, body.h);
  layout.max = mu_vec2(-0x1000000, -0x1000000);
  ctx->layout_stack.items[ctx->layout_stack.idx++] = layout;
  mu_layout_row(ctx, 1, &width, 0);
}


static mu_Layout* get_layout(mu_Context *ctx) {
  return &ctx->layout_stack.items[ctx->layout_stack.idx - 1];
}


static void pop_container(mu_Context *ctx) {
  mu_Container *cnt = mu_get_current_container(ctx);
  mu_Layout *layout = get_layout(ctx);
  cnt->content_size.x = layout->max.x - layout->body.x;
  cnt->content_size.y = layout->max.y - layout->body.y;
  /* pop container, layout and clip */
  ctx->container_stack.idx--;
  ctx->layout_stack.idx--;
  mu_pop_clip_rect(ctx);
}


mu_Container* mu_get_current_container(mu_Context *ctx) {
  return ctx->container_stack.items[ctx->container_stack.idx - 1];
}


static mu_Container* get_container(mu_Context *ctx, mu_Id id, int opt) {
  static mu_Container pool[MU_CONTAINERPOOL_SIZE];
  static int pool_inited = 0;
  int idx;

  if (!pool_inited) {
    memset(pool, 0, sizeof(pool));
    pool_inited = 1;
  }

  idx = mu_pool_get(ctx, (int*) pool, MU_CONTAINERPOOL_SIZE, id);
  if (idx >= 0) {
    if (pool[idx].open || ~opt & MU_OPT_CLOSED) {
      mu_pool_update(ctx, (int*) pool, MU_CONTAINERPOOL_SIZE, idx);
    }
    return &pool[idx];
  }
  if (opt & MU_OPT_CLOSED) { return NULL; }
  /* container not found in pool: init new container */
  idx = mu_pool_init(ctx, (int*) pool, MU_CONTAINERPOOL_SIZE, id);
  memset(&pool[idx], 0, sizeof(mu_Container));
  pool[idx].open = 1;
  mu_bring_to_front(ctx, &pool[idx]);
  return &pool[idx];
}


mu_Container* mu_get_container(mu_Context *ctx, const char *name) {
  mu_Id id = mu_get_id(ctx, name, strlen(name));
  return get_container(ctx, id, 0);
}


void mu_bring_to_front(mu_Context *ctx, mu_Container *cnt) {
  unused(ctx);
  static int top_zindex = 0;
  cnt->zindex = ++top_zindex;
}


/*============================================================================
** pool
**============================================================================*/

struct mu_PoolItem {
  mu_Id id;
  int last_update;
};

int mu_pool_init(mu_Context *ctx, int *pool, int pool_size, mu_Id id) {
  struct mu_PoolItem *items = (struct mu_PoolItem*) pool;
  int i, n = -1, f = ctx->last_hover_root_rect.x;
  unused(ctx);
  for (i = 0; i < pool_size; i++) {
    if (items[i].last_update < f) {
      f = items[i].last_update;
      n = i;
    }
  }
  if (n < 0) { n = 0; }
  items[n].id = id;
  items[n].last_update = 1;
  return n;
}


int mu_pool_get(mu_Context *ctx, int *pool, int pool_size, mu_Id id) {
  struct mu_PoolItem *items = (struct mu_PoolItem*) pool;
  int i;
  unused(ctx);
  for (i = 0; i < pool_size; i++) {
    if (items[i].id == id) { return i; }
  }
  return -1;
}


void mu_pool_update(mu_Context *ctx, int *pool, int pool_size, int idx) {
  struct mu_PoolItem *items = (struct mu_PoolItem*) pool;
  unused(ctx); unused(pool_size);
  items[idx].last_update++;
}


/*============================================================================
** input handlers
**============================================================================*/

void mu_input_mousemove(mu_Context *ctx, int x, int y) {
  ctx->mouse_pos = mu_vec2(x, y);
}


void mu_input_mousedown(mu_Context *ctx, int x, int y, int btn) {
  mu_input_mousemove(ctx, x, y);
  ctx->mouse_down |= btn;
  ctx->mouse_pressed |= btn;
}


void mu_input_mouseup(mu_Context *ctx, int x, int y, int btn) {
  mu_input_mousemove(ctx, x, y);
  ctx->mouse_down &= ~btn;
}


void mu_input_scroll(mu_Context *ctx, int x, int y) {
  ctx->scroll_delta.x += x;
  ctx->scroll_delta.y += y;
}


void mu_input_keydown(mu_Context *ctx, int key) {
  ctx->key_pressed |= key;
  ctx->key_down |= key;
}


void mu_input_keyup(mu_Context *ctx, int key) {
  ctx->key_down &= ~key;
}


void mu_input_text(mu_Context *ctx, const char *text) {
  int len = strlen(ctx->input_text);
  int size = strlen(text) + 1;
  if (len + size <= (int) sizeof(ctx->input_text)) {
    memcpy(ctx->input_text + len, text, size);
  }
}


/*============================================================================
** command list
**============================================================================*/

mu_Command* mu_push_command(mu_Context *ctx, int type, int size) {
  mu_Command *cmd = (mu_Command*) (ctx->command_list.items + ctx->command_list.idx);
  cmd->base.type = type;
  cmd->base.size = size;
  ctx->command_list.idx += size;
  return cmd;
}


int mu_next_command(mu_Context *ctx, mu_Command **cmd) {
  if (*cmd) {
    *cmd = (mu_Command*) (((char*) *cmd) + (*cmd)->base.size);
  } else {
    *cmd = (mu_Command*) ctx->command_list.items;
  }
  while ((char*) *cmd != ctx->command_list.items + ctx->command_list.idx) {
    if ((*cmd)->type != MU_COMMAND_JUMP) { return 1; }
    *cmd = (*cmd)->jump.dst;
  }
  return 0;
}


static mu_Command* push_jump(mu_Context *ctx, void *dst) {
  mu_Command *cmd;
  cmd = mu_push_command(ctx, MU_COMMAND_JUMP, sizeof(mu_JumpCommand));
  cmd->jump.dst = dst;
  return cmd;
}


void mu_set_clip(mu_Context *ctx, mu_Rect rect) {
  mu_Command *cmd;
  cmd = mu_push_command(ctx, MU_COMMAND_CLIP, sizeof(mu_ClipCommand));
  cmd->clip.rect = rect;
}


void mu_draw_rect(mu_Context *ctx, mu_Rect rect, mu_Color color) {
  mu_Command *cmd;
  rect = intersect_rects(rect, mu_get_clip_rect(ctx));
  if (rect.w > 0 && rect.h > 0) {
    cmd = mu_push_command(ctx, MU_COMMAND_RECT, sizeof(mu_RectCommand));
    cmd->rect.rect = rect;
    cmd->rect.color = color;
  }
}


void mu_draw_box(mu_Context *ctx, mu_Rect rect, mu_Color color) {
  mu_draw_rect(ctx, mu_rect(rect.x + 1, rect.y, rect.w - 2, 1), color);
  mu_draw_rect(ctx, mu_rect(rect.x + 1, rect.y + rect.h - 1, rect.w - 2, 1), color);
  mu_draw_rect(ctx, mu_rect(rect.x, rect.y, 1, rect.h), color);
  mu_draw_rect(ctx, mu_rect(rect.x + rect.w - 1, rect.y, 1, rect.h), color);
}


void mu_draw_text(mu_Context *ctx, mu_Font font, const char *str, int len,
  mu_Vec2 pos, mu_Color color)
{
  mu_Command *cmd;
  mu_Rect rect = mu_rect(pos.x, pos.y, ctx->text_width(font, str, len), ctx->text_height(font));
  int clipped = mu_check_clip(ctx, rect);
  if (clipped == MU_CLIP_ALL ) { return; }
  if (len < 0) { len = strlen(str); }
  cmd = mu_push_command(ctx, MU_COMMAND_TEXT, sizeof(mu_TextCommand) + len);
  memcpy(cmd->text.str, str, len);
  cmd->text.str[len] = '\0';
  cmd->text.pos = pos;
  cmd->text.color = color;
  cmd->text.font = font;
}


void mu_draw_icon(mu_Context *ctx, int id, mu_Rect rect, mu_Color color) {
  mu_Command *cmd;
  /* do clip test for icon bounds */
  int clipped = mu_check_clip(ctx, rect);
  if (clipped == MU_CLIP_ALL ) { return; }
  cmd = mu_push_command(ctx, MU_COMMAND_ICON, sizeof(mu_IconCommand));
  cmd->icon.id = id;
  cmd->icon.rect = rect;
  cmd->icon.color = color;
}


/*============================================================================
** layout
**============================================================================*/

void mu_layout_begin_column(mu_Context *ctx) {
  push_layout(ctx, mu_layout_next(ctx), mu_vec2(0, 0));
}


void mu_layout_end_column(mu_Context *ctx) {
  mu_Layout *b = get_layout(ctx);
  ctx->layout_stack.idx--;
  /* inherit max position from column */
  mu_Layout *a = get_layout(ctx);
  a->max.x = mu_max(a->max.x, b->max.x);
  a->max.y = mu_max(a->max.y, b->max.y);
}


void mu_layout_row(mu_Context *ctx, int items, const int *widths, int height) {
  mu_Layout *layout = get_layout(ctx);
  if (widths) {
    int i;
    for (i = 0; i < items; i++) {
      layout->widths[i] = widths[i];
    }
  }
  layout->items = items;
  layout->position = mu_vec2(layout->indent, layout->next_row);
  layout->size.y = height;
  layout->item_index = 0;
}


void mu_layout_width(mu_Context *ctx, int width) {
  get_layout(ctx)->size.x = width;
}


void mu_layout_height(mu_Context *ctx, int height) {
  get_layout(ctx)->size.y = height;
}


void mu_layout_set_next(mu_Context *ctx, mu_Rect r, int relative) {
  mu_Layout *layout = get_layout(ctx);
  layout->next = r;
  layout->next_type = relative ? 1 : 2;
}


mu_Rect mu_layout_next(mu_Context *ctx) {
  mu_Layout *layout = get_layout(ctx);
  mu_Style *style = ctx->style;
  mu_Rect res;

  if (layout->next_type) {
    /* handle rect set by `mu_layout_set_next` */
    int type = layout->next_type;
    layout->next_type = 0;
    res = layout->next;
    if (type == 2) { return res; }
    if (type == 1) {
      res.x += layout->position.x + layout->body.x;
      res.y += layout->position.y + layout->body.y;
      return res;
    }
  }

  if (layout->item_index == layout->items) {
    /* reset position for next row */
    mu_layout_row(ctx, layout->items, NULL, layout->size.y);
  }

  /* position */
  res.x = layout->position.x;
  res.y = layout->position.y;

  /* size */
  if (layout->items > 0) {
    res.w = layout->widths[layout->item_index];
  } else {
    res.w = layout->size.x;
  }
  if (res.w == 0) {
    res.w = style->size.x + style->padding * 2;
  }
  if (res.w < 0) {
    res.w += layout->body.w - res.x + 1;
  }
  res.h = layout->size.y;
  if (res.h == 0) {
    res.h = style->size.y + style->padding * 2;
  }
  if (res.h < 0) {
    res.h += layout->body.h - res.y + 1;
  }

  /* update position */
  layout->item_index++;
  layout->position.x += res.w + style->spacing;
  layout->next_row = mu_max(layout->next_row, res.y + res.h + style->spacing);

  /* apply body offset */
  res.x += layout->body.x;
  res.y += layout->body.y;

  /* update max position */
  layout->max.x = mu_max(layout->max.x, res.x + res.w);
  layout->max.y = mu_max(layout->max.y, res.y + res.h);

  return res;
}


/*============================================================================
** controls
**============================================================================*/

static int in_hover_root(mu_Context *ctx) {
  int i = ctx->root_list.idx;
  while (i--) {
    if (ctx->root_list.items[i] == ctx->hover_root) { return 1; }
    if (ctx->root_list.items[i]->head) { break; }
  }
  return 0;
}


void mu_draw_control_frame(mu_Context *ctx, mu_Id id, mu_Rect rect,
  int colorid, int opt)
{
  if (opt & MU_OPT_NOFRAME) { return; }
  colorid += (ctx->focus == id) ? 2 : (ctx->hover == id) ? 1 : 0;
  ctx->draw_frame(ctx, rect, colorid);
}


void mu_draw_control_text(mu_Context *ctx, const char *str, mu_Rect rect,
  int colorid, int opt)
{
  mu_Vec2 pos;
  mu_Font font = ctx->style->font;
  int tw = ctx->text_width(font, str, -1);
  mu_push_clip_rect(ctx, rect);
  pos.y = rect.y + (rect.h - ctx->text_height(font)) / 2;
  if (opt & MU_OPT_ALIGNCENTER) {
    pos.x = rect.x + (rect.w - tw) / 2;
  } else if (opt & MU_OPT_ALIGNRIGHT) {
    pos.x = rect.x + rect.w - tw - ctx->style->padding;
  } else {
    pos.x = rect.x + ctx->style->padding;
  }
  mu_draw_text(ctx, font, str, -1, pos, ctx->style->colors[colorid]);
  mu_pop_clip_rect(ctx);
}


static int update_control(mu_Context *ctx, mu_Id id, mu_Rect rect, int opt) {
  int mouseover = in_hover_root(ctx) && rect_overlaps_vec2(rect, ctx->mouse_pos);
  if (ctx->focus == id) { ctx->updated_focus = id; }
  if (opt & MU_OPT_NOINTERACT) { return 0; }
  if (mouseover && !ctx->mouse_down) { ctx->hover = id; }

  if (ctx->focus == id) {
    if (ctx->mouse_pressed && !mouseover) { mu_set_focus(ctx, 0); }
    if (!ctx->mouse_down && ~opt & MU_OPT_HOLDFOCUS) { mu_set_focus(ctx, 0); }
  }

  if (ctx->hover == id) {
    if (ctx->mouse_pressed) {
      mu_set_focus(ctx, id);
    } else if (!mouseover) {
      ctx->hover = 0;
    }
  }

  return ctx->focus == id;
}


void mu_text(mu_Context *ctx, const char *text) {
  const char *start, *end, *p = text;
  int width = -1;
  mu_Font font = ctx->style->font;
  mu_Color color = ctx->style->colors[MU_COLOR_TEXT];
  mu_layout_begin_column(ctx);
  mu_layout_row(ctx, 1, &width, ctx->text_height(font));
  while (*p) {
    mu_Rect r = mu_layout_next(ctx);
    int w = 0;
    start = end = p;
    do {
      const char* word = p;
      while (*p && *p != ' ' && *p != '\n') { p++; }
      w += ctx->text_width(font, word, p - word);
      if (w > r.w && end != start) { break; }
      w += ctx->text_width(font, p, 1);
      end = p++;
    } while (*end && *end != '\n');
    mu_draw_text(ctx, font, start, end - start, mu_vec2(r.x, r.y), color);
    p = end;
    while (*p == ' ' || *p == '\n') { p++; }
  }
  mu_layout_end_column(ctx);
}


void mu_label(mu_Context *ctx, const char *text) {
  mu_draw_control_text(ctx, text, mu_layout_next(ctx), MU_COLOR_TEXT, 0);
}


int mu_button_ex(mu_Context *ctx, const char *label, int icon, int opt) {
  int res = 0;
  mu_Id id = label ? mu_get_id(ctx, label, strlen(label))
                   : mu_get_id(ctx, &icon, sizeof(icon));
  mu_Rect r = mu_layout_next(ctx);
  update_control(ctx, id, r, opt);
  /* handle click */
  if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->hover == id) {
    res |= MU_RES_SUBMIT;
  }
  /* draw */
  mu_draw_control_frame(ctx, id, r, MU_COLOR_BUTTON, opt);
  if (label) { mu_draw_control_text(ctx, label, r, MU_COLOR_TEXT, opt); }
  if (icon) { mu_draw_icon(ctx, icon, r, ctx->style->colors[MU_COLOR_TEXT]); }
  return res;
}


int mu_button(mu_Context *ctx, const char *label) {
  return mu_button_ex(ctx, label, 0, MU_OPT_ALIGNCENTER);
}


int mu_checkbox(mu_Context *ctx, const char *label, int *state) {
  int res = 0;
  mu_Id id = mu_get_id(ctx, &state, sizeof(state));
  mu_Rect r = mu_layout_next(ctx);
  mu_Rect box = mu_rect(r.x, r.y, r.h, r.h);
  update_control(ctx, id, r, 0);
  /* handle click */
  if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->hover == id) {
    res |= MU_RES_CHANGE;
    *state = !*state;
  }
  /* draw */
  mu_draw_control_frame(ctx, id, box, MU_COLOR_BASE, 0);
  if (*state) {
    mu_draw_icon(ctx, MU_ICON_CHECK, box, ctx->style->colors[MU_COLOR_TEXT]);
  }
  r.x += box.w + ctx->style->spacing;
  r.w -= box.w + ctx->style->spacing;
  mu_draw_control_text(ctx, label, r, MU_COLOR_TEXT, 0);
  return res;
}


int mu_textbox_raw(mu_Context *ctx, char *buf, int bufsz, mu_Id id, mu_Rect r,
  int opt)
{
  int res = 0;
  update_control(ctx, id, r, opt | MU_OPT_HOLDFOCUS);

  if (ctx->focus == id) {
    /* handle text input */
    int len = strlen(buf);
    int n = mu_min(bufsz - len - 1, (int) strlen(ctx->input_text));
    if (n > 0) {
      memcpy(buf + len, ctx->input_text, n);
      len += n;
      buf[len] = '\0';
      res |= MU_RES_CHANGE;
    }
    /* handle backspace */
    if (ctx->key_pressed & MU_KEY_BACKSPACE && len > 0) {
      /* skip utf-8 continuation bytes */
      while ((buf[--len] & 0xc0) == 0x80 && len > 0);
      buf[len] = '\0';
      res |= MU_RES_CHANGE;
    }
    /* handle return */
    if (ctx->key_pressed & MU_KEY_RETURN) {
      mu_set_focus(ctx, 0);
      res |= MU_RES_SUBMIT;
    }
  }

  /* draw */
  mu_draw_control_frame(ctx, id, r, MU_COLOR_BASE, opt);
  if (ctx->focus == id) {
    mu_Color color = ctx->style->colors[MU_COLOR_TEXT];
    mu_Font font = ctx->style->font;
    int textw = ctx->text_width(font, buf, -1);
    int texth = ctx->text_height(font);
    int ofx = r.w - ctx->style->padding - textw - 1;
    int textx = r.x + mu_min(ofx, ctx->style->padding);
    int texty = r.y + (r.h - texth) / 2;
    mu_push_clip_rect(ctx, r);
    mu_draw_text(ctx, font, buf, -1, mu_vec2(textx, texty), color);
    mu_draw_rect(ctx, mu_rect(textx + textw, texty, 1, texth), color);
    mu_pop_clip_rect(ctx);
  } else {
    mu_draw_control_text(ctx, buf, r, MU_COLOR_TEXT, opt);
  }

  return res;
}


static int number_textbox(mu_Context *ctx, mu_Real *value, mu_Rect r, mu_Id id) {
  if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->key_down & MU_KEY_SHIFT &&
      ctx->hover == id
  ) {
    ctx->number_edit = id;
    sprintf(ctx->number_edit_buf, MU_REAL_FMT, *value);
  }
  if (ctx->number_edit == id) {
    int res = mu_textbox_raw(
      ctx, ctx->number_edit_buf, sizeof(ctx->number_edit_buf), id, r, 0);
    if (res & MU_RES_SUBMIT || ctx->focus != id) {
      *value = strtod(ctx->number_edit_buf, NULL);
      ctx->number_edit = 0;
    } else {
      return 1;
    }
  }
  return 0;
}


int mu_textbox_ex(mu_Context *ctx, char *buf, int bufsz, int opt) {
  mu_Id id = mu_get_id(ctx, &buf, sizeof(buf));
  mu_Rect r = mu_layout_next(ctx);
  return mu_textbox_raw(ctx, buf, bufsz, id, r, opt);
}


int mu_textbox(mu_Context *ctx, char *buf, int bufsz) {
  return mu_textbox_ex(ctx, buf, bufsz, 0);
}


int mu_slider_ex(mu_Context *ctx, mu_Real *value, mu_Real low, mu_Real high,
  mu_Real step, const char *fmt, int opt)
{
  char buf[MU_MAX_FMT];
  mu_Rect thumb;
  int x, w, res = 0;
  mu_Real last = *value, v = last;
  mu_Id id = mu_get_id(ctx, &value, sizeof(value));
  mu_Rect base = mu_layout_next(ctx);

  if (!fmt) { fmt = MU_SLIDER_FMT; }

  /* handle text input mode */
  if (number_textbox(ctx, &v, base, id)) { return res; }

  /* handle normal mode */
  update_control(ctx, id, base, opt);

  /* handle input */
  if (ctx->focus == id &&
      (ctx->mouse_down | ctx->mouse_pressed) == MU_MOUSE_LEFT)
  {
    v = low + (ctx->mouse_pos.x - base.x) * (high - low) / base.w;
    if (step) { v = (((int)((v - low) / step + 0.5f)) * step) + low; }
  }
  /* clamp and store value */
  *value = v = mu_clamp(v, low, high);
  if (last != v) { res |= MU_RES_CHANGE; }

  /* draw base */
  mu_draw_control_frame(ctx, id, base, MU_COLOR_BASE, opt);
  /* draw thumb */
  w = ctx->style->thumb_size;
  x = (int)((v - low) * (base.w - w) / (high - low));
  thumb = mu_rect(base.x + x, base.y, w, base.h);
  mu_draw_control_frame(ctx, id, thumb, MU_COLOR_BUTTON, opt);
  /* draw text */
  sprintf(buf, fmt, (double)v);
  mu_draw_control_text(ctx, buf, base, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);

  return res;
}


int mu_slider(mu_Context *ctx, mu_Real *value, mu_Real low, mu_Real high) {
  return mu_slider_ex(ctx, value, low, high, 0, MU_SLIDER_FMT, MU_OPT_ALIGNCENTER);
}


int mu_number_ex(mu_Context *ctx, mu_Real *value, mu_Real step,
  const char *fmt, int opt)
{
  char buf[MU_MAX_FMT];
  int res = 0;
  mu_Id id = mu_get_id(ctx, &value, sizeof(value));
  mu_Rect base = mu_layout_next(ctx);
  mu_Real last = *value;

  if (!fmt) { fmt = MU_SLIDER_FMT; }

  /* handle text input mode */
  if (number_textbox(ctx, value, base, id)) { return res; }

  /* handle normal mode */
  update_control(ctx, id, base, opt);

  /* handle input */
  if (ctx->focus == id &&
      (ctx->mouse_down | ctx->mouse_pressed) == MU_MOUSE_LEFT)
  {
    *value += ctx->mouse_delta.x * step;
  }
  /* set return value */
  if (*value != last) { res |= MU_RES_CHANGE; }

  /* draw base */
  mu_draw_control_frame(ctx, id, base, MU_COLOR_BASE, opt);
  /* draw text */
  sprintf(buf, fmt, (double)*value);
  mu_draw_control_text(ctx, buf, base, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);

  return res;
}


int mu_number(mu_Context *ctx, mu_Real *value, mu_Real step) {
  return mu_number_ex(ctx, value, step, MU_SLIDER_FMT, MU_OPT_ALIGNCENTER);
}


static int header(mu_Context *ctx, const char *label, int istreenode, int opt) {
  mu_Rect r;
  int active, expanded;
  mu_Id id = mu_get_id(ctx, label, strlen(label));
  mu_layout_row(ctx, 1, NULL, 0);

  active = (ctx->focus == id);
  expanded = (opt & MU_OPT_EXPANDED);
  r = mu_layout_next(ctx);
  update_control(ctx, id, r, 0);

  /* handle click */
  if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->hover == id) {
    active = !active;
  }

  /* draw */
  if (istreenode) {
    if (ctx->hover == id) { ctx->draw_frame(ctx, r, MU_COLOR_BUTTONHOVER); }
  } else {
    mu_draw_control_frame(ctx, id, r, MU_COLOR_BUTTON, 0);
  }
  mu_draw_icon(
    ctx, expanded ? MU_ICON_EXPANDED : MU_ICON_COLLAPSED,
    mu_rect(r.x, r.y, r.h, r.h), ctx->style->colors[MU_COLOR_TEXT]);
  r.x += r.h - ctx->style->padding;
  r.w -= r.h - ctx->style->padding;
  mu_draw_control_text(ctx, label, r, MU_COLOR_TEXT, 0);

  return expanded ? MU_RES_ACTIVE : 0;
}


int mu_header(mu_Context *ctx, const char *label) {
  return header(ctx, label, 0, 0);
}


int mu_header_ex(mu_Context *ctx, const char *label, int opt) {
  return header(ctx, label, 0, opt);
}


/*============================================================================
** window, panel
**============================================================================*/

static void scrollbars(mu_Context *ctx, mu_Container *cnt, mu_Rect *body) {
  int sz = ctx->style->scrollbar_size;
  mu_Vec2 cs = cnt->content_size;
  cs.x += ctx->style->padding * 2;
  cs.y += ctx->style->padding * 2;
  mu_push_clip_rect(ctx, *body);

  if (cs.y > cnt->body.h) {
    /* handle scroll input */
    mu_Id id = mu_get_id(ctx, "!scrollbar_y", 12);
    mu_Rect base = *body;
    base.x = body->x + body->w;
    base.w = sz;
    update_control(ctx, id, base, 0);
    if (ctx->focus == id && ctx->mouse_down == MU_MOUSE_LEFT) {
      cnt->scroll.y += ctx->mouse_delta.y * cs.y / base.h;
    }
    /* clamp scroll */
    cnt->scroll.y = mu_clamp(cnt->scroll.y, 0, cs.y - cnt->body.h);

    /* draw base and thumb */
    ctx->draw_frame(ctx, base, MU_COLOR_SCROLLBASE);
    int h = mu_max(sz, base.h * cnt->body.h / cs.y);
    int y = cnt->scroll.y * (base.h - h) / (cs.y - cnt->body.h);
    mu_Rect thumb = mu_rect(base.x, base.y + y, sz, h);
    ctx->draw_frame(ctx, thumb, MU_COLOR_SCROLLTHUMB);
    /* shrink body by scrollbar size */
    body->w -= sz;
  }

  if (cs.x > cnt->body.w) {
    /* handle scroll input */
    mu_Id id = mu_get_id(ctx, "!scrollbar_x", 12);
    mu_Rect base = *body;
    base.y = body->y + body->h;
    base.h = sz;
    update_control(ctx, id, base, 0);
    if (ctx->focus == id && ctx->mouse_down == MU_MOUSE_LEFT) {
      cnt->scroll.x += ctx->mouse_delta.x * cs.x / base.w;
    }
    /* clamp scroll */
    cnt->scroll.x = mu_clamp(cnt->scroll.x, 0, cs.x - cnt->body.w);

    /* draw base and thumb */
    ctx->draw_frame(ctx, base, MU_COLOR_SCROLLBASE);
    int w = mu_max(sz, base.w * cnt->body.w / cs.x);
    int x = cnt->scroll.x * (base.w - w) / (cs.x - cnt->body.w);
    mu_Rect thumb = mu_rect(base.x + x, base.y, w, sz);
    ctx->draw_frame(ctx, thumb, MU_COLOR_SCROLLTHUMB);
    /* shrink body by scrollbar size */
    body->h -= sz;
  }

  mu_pop_clip_rect(ctx);
}


static void push_container_body(
  mu_Context *ctx, mu_Container *cnt, mu_Rect body, int opt
) {
  if (~opt & MU_OPT_NOSCROLL) { scrollbars(ctx, cnt, &body); }
  push_layout(ctx, expand_rect(body, -ctx->style->padding), cnt->scroll);
  cnt->body = body;
}


static void begin_root_container(mu_Context *ctx, mu_Container *cnt) {
  ctx->container_stack.items[ctx->container_stack.idx++] = cnt;
  /* push container to roots list and push a null command */
  ctx->root_list.items[ctx->root_list.idx++] = cnt;
  cnt->head = push_jump(ctx, NULL);
  /* set as hover root if the mouse is overlapping this container and it has a
  ** higher zindex than the current hover root */
  if (rect_overlaps_vec2(cnt->rect, ctx->mouse_pos) &&
      (!ctx->next_hover_root || cnt->zindex > ctx->next_hover_root->zindex)
  ) {
    ctx->next_hover_root = cnt;
  }
  /* clipping is reset for root containers */
  ctx->clip_stack.items[ctx->clip_stack.idx++] = expand_rect(cnt->rect, 4);
}


static void end_root_container(mu_Context *ctx) {
  /* push tail 'jump' to this container; this will be overwritten in
  ** mu_end() with the destination of the next root container */
  mu_Container *cnt = mu_get_current_container(ctx);
  cnt->tail = push_jump(ctx, NULL);
  pop_container(ctx);
}


int mu_begin_window_ex(mu_Context *ctx, const char *title, mu_Rect rect, int opt) {
  mu_Rect body;
  mu_Id id = mu_get_id(ctx, title, strlen(title));
  mu_Container *cnt = get_container(ctx, id, opt);
  if (!cnt || !cnt->open) { return 0; }
  ctx->id_stack.items[ctx->id_stack.idx++] = id;

  if (cnt->rect.w == 0) { cnt->rect = rect; }
  begin_root_container(ctx, cnt);
  rect = cnt->rect;
  body = cnt->rect;

  /* draw frame */
  if (~opt & MU_OPT_NOFRAME) {
    ctx->draw_frame(ctx, rect, MU_COLOR_WINDOWBG);
  }

  /* do title bar */
  if (~opt & MU_OPT_NOTITLE) {
    mu_Rect tr = rect;
    tr.h = ctx->style->title_height;
    ctx->draw_frame(ctx, tr, MU_COLOR_TITLEBG);

    /* do title text */
    if (~opt & MU_OPT_NOTITLE) {
      mu_Id tid = mu_get_id(ctx, "!title", 6);
      update_control(ctx, tid, tr, opt);
      mu_draw_control_text(ctx, title, tr, MU_COLOR_TITLETEXT, opt);
      if (tid == ctx->focus && ctx->mouse_down == MU_MOUSE_LEFT) {
        cnt->rect.x += ctx->mouse_delta.x;
        cnt->rect.y += ctx->mouse_delta.y;
      }
      body.y += tr.h;
      body.h -= tr.h;
    }

    /* do `close` button */
    if (~opt & MU_OPT_NOCLOSE) {
      mu_Id cid = mu_get_id(ctx, "!close", 6);
      mu_Rect r = mu_rect(tr.x + tr.w - tr.h, tr.y, tr.h, tr.h);
      tr.w -= r.w;
      mu_draw_icon(ctx, MU_ICON_CLOSE, r, ctx->style->colors[MU_COLOR_TITLETEXT]);
      update_control(ctx, cid, r, opt);
      if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->hover == cid) {
        cnt->open = 0;
      }
    }
  }

  push_container_body(ctx, cnt, body, opt);

  /* do `resize` handle */
  if (~opt & MU_OPT_NORESIZE) {
    int sz = ctx->style->title_height;
    mu_Id rid = mu_get_id(ctx, "!resize", 7);
    mu_Rect r = mu_rect(rect.x + rect.w - sz, rect.y + rect.h - sz, sz, sz);
    update_control(ctx, rid, r, opt);
    if (rid == ctx->focus && ctx->mouse_down == MU_MOUSE_LEFT) {
      cnt->rect.w = mu_max(96, cnt->rect.w + ctx->mouse_delta.x);
      cnt->rect.h = mu_max(64, cnt->rect.h + ctx->mouse_delta.y);
    }
  }

  /* resize to content size */
  if (opt & MU_OPT_AUTOSIZE) {
    mu_Rect r = get_layout(ctx)->body;
    cnt->rect.w = cnt->content_size.x + (cnt->rect.w - r.w);
    cnt->rect.h = cnt->content_size.y + (cnt->rect.h - r.h);
  }

  /* close if this is a popup window and elsewhere was clicked */
  if (opt & MU_OPT_POPUP && ctx->mouse_pressed && ctx->hover_root != cnt) {
    cnt->open = 0;
  }

  mu_push_clip_rect(ctx, cnt->body);
  return MU_RES_ACTIVE;
}


int mu_begin_window(mu_Context *ctx, const char *title, mu_Rect rect) {
  return mu_begin_window_ex(ctx, title, rect, 0);
}


void mu_end_window(mu_Context *ctx) {
  mu_pop_clip_rect(ctx);
  end_root_container(ctx);
}


void mu_open_popup(mu_Context *ctx, const char *name) {
  mu_Container *cnt = mu_get_container(ctx, name);
  /* set as hover root so popup isn't closed in begin_window_ex() */
  ctx->next_hover_root = cnt;
  ctx->hover_root = cnt;
  cnt->rect = mu_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, 1, 1);
  cnt->open = 1;
  mu_bring_to_front(ctx, cnt);
}


int mu_begin_popup(mu_Context *ctx, const char *name) {
  int opt = MU_OPT_POPUP | MU_OPT_AUTOSIZE | MU_OPT_NORESIZE |
            MU_OPT_NOSCROLL | MU_OPT_NOTITLE | MU_OPT_CLOSED;
  return mu_begin_window_ex(ctx, name, mu_rect(0, 0, 0, 0), opt);
}


void mu_end_popup(mu_Context *ctx) {
  mu_end_window(ctx);
}


void mu_begin_panel_ex(mu_Context *ctx, const char *name, int opt) {
  mu_Container *cnt;
  mu_Id id = mu_get_id(ctx, name, strlen(name));
  cnt = get_container(ctx, id, opt);
  cnt->rect = mu_layout_next(ctx);
  if (~opt & MU_OPT_NOFRAME) {
    ctx->draw_frame(ctx, cnt->rect, MU_COLOR_PANELBG);
  }
  ctx->container_stack.items[ctx->container_stack.idx++] = cnt;
  push_container_body(ctx, cnt, cnt->rect, opt);
  mu_push_clip_rect(ctx, cnt->body);
}


void mu_begin_panel(mu_Context *ctx, const char *name) {
  mu_begin_panel_ex(ctx, name, 0);
}


void mu_end_panel(mu_Context *ctx) {
  mu_pop_clip_rect(ctx);
  pop_container(ctx);
}
