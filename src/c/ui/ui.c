#include "ui.h"

// ---------------------------------------------------------------- formatting

void ui_format_clock(int minute_of_day, char *buf, size_t cap) {
  tt_format_clock(minute_of_day, clock_is_24h_style(), buf, cap);
}

void ui_format_day(time_t when, char *buf, size_t cap) {
  int32_t day, today;
  tt_epoch_to_local(when, &day, NULL, NULL);
  tt_epoch_to_local(time(NULL), &today, NULL, NULL);
  if (day == today) {
    snprintf(buf, cap, "Today");
  } else if (day == today + 1) {
    snprintf(buf, cap, "Tomorrow");
  } else if (day == today - 1) {
    snprintf(buf, cap, "Yesterday");
  } else {
    TTDate date = tt_civil_from_days(day);
    TTDate now = tt_civil_from_days(today);
    if (date.y == now.y) {
      snprintf(buf, cap, "%s %d %s", tt_wday_abbr[tt_dow(day)], date.d,
               tt_month_abbr[date.m - 1]);
    } else {
      snprintf(buf, cap, "%d %s %d", date.d, tt_month_abbr[date.m - 1], date.y);
    }
  }
}

void ui_format_when(time_t when, char *buf, size_t cap) {
  char day[24];
  char clock[12];
  int minute;
  tt_epoch_to_local(when, NULL, &minute, NULL);
  ui_format_day(when, day, sizeof(day));
  ui_format_clock(minute, clock, sizeof(clock));
  snprintf(buf, cap, "%s %s", day, clock);
}

GColor ui_color_from_tag(uint32_t tag) {
  if (!tag) {
    return UI_ACCENT;
  }
  return PBL_IF_COLOR_ELSE(GColorFromRGB((tag >> 16) & 0xFF, (tag >> 8) & 0xFF, tag & 0xFF),
                           GColorBlack);
}

void ui_refresh_top(void) {
  Window *top = window_stack_get_top_window();
  if (top) {
    layer_mark_dirty(window_get_root_layer(top));
  }
}

void ui_draw_title_bar(GContext *g, GRect bounds, const char *title) {
  graphics_context_set_fill_color(g, UI_ACCENT);
  graphics_fill_rect(g, GRect(bounds.origin.x, bounds.origin.y, bounds.size.w, UI_TITLE_H), 0,
                     GCornerNone);
  graphics_context_set_text_color(g, UI_ON_ACCENT);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  char clock[12];
  time_t now = time(NULL);
  int minute;
  tt_epoch_to_local(now, NULL, &minute, NULL);
  ui_format_clock(minute, clock, sizeof(clock));
  const int clock_w = 62;
  graphics_draw_text(g, title, font,
                     GRect(bounds.origin.x + 8, bounds.origin.y - 1,
                           bounds.size.w - clock_w - 12, UI_TITLE_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(g, clock, font,
                     GRect(bounds.origin.x + bounds.size.w - clock_w - 6, bounds.origin.y - 1,
                           clock_w, UI_TITLE_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

// ---------------------------------------------------------------- list window

struct ListWindow {
  Window *window;
  MenuLayer *menu;
  Layer *title_layer;
  TextLayer *empty_layer;
  ListCallbacks cb;
  void *ctx;
  char title[28];
};

#define MAX_OPEN_LISTS 12
static ListWindow *s_open[MAX_OPEN_LISTS];

static void prv_register(ListWindow *lw, bool open) {
  for (int i = 0; i < MAX_OPEN_LISTS; i++) {
    if (s_open[i] == (open ? NULL : lw)) {
      s_open[i] = open ? lw : NULL;
      return;
    }
  }
}

static uint16_t prv_sections(ListWindow *lw) {
  return lw->cb.num_sections ? lw->cb.num_sections(lw->ctx) : 1;
}

static uint16_t prv_num_sections(MenuLayer *menu, void *context) {
  return prv_sections(context);
}

static uint16_t prv_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  ListWindow *lw = context;
  return lw->cb.num_rows(lw->ctx, section);
}

static int16_t prv_header_height(MenuLayer *menu, uint16_t section, void *context) {
  ListWindow *lw = context;
  return lw->cb.header ? 22 : 0;
}

static void prv_draw_header(GContext *g, const Layer *cell, uint16_t section, void *context) {
  ListWindow *lw = context;
  GRect b = layer_get_bounds(cell);
  char text[40] = "";
  lw->cb.header(lw->ctx, section, text, sizeof(text));
  graphics_context_set_fill_color(g, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
  graphics_fill_rect(g, b, 0, GCornerNone);
  graphics_context_set_text_color(g, GColorBlack);
  graphics_draw_text(g, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(8, -3, b.size.w - 12, b.size.h), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static int16_t prv_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  ListWindow *lw = context;
  if (lw->cb.row_height) {
    int16_t h = lw->cb.row_height(lw->ctx, index->section, index->row);
    if (h > 0) {
      return h;
    }
  }
  ListRow r;
  memset(&r, 0, sizeof(r));
  lw->cb.row(lw->ctx, index->section, index->row, &r);
  return r.subtitle[0] ? 54 : 40;
}

static void prv_draw_row(GContext *g, const Layer *cell, MenuIndex *index, void *context) {
  ListWindow *lw = context;
  GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  if (lw->cb.draw && lw->cb.draw(lw->ctx, g, b, index->section, index->row, hi)) {
    return;
  }
  ListRow r;
  memset(&r, 0, sizeof(r));
  lw->cb.row(lw->ctx, index->section, index->row, &r);

  GColor fg = hi ? UI_ON_ACCENT : GColorBlack;
  GColor sub = hi ? UI_ON_ACCENT : UI_DIM;
  if (!hi && (r.flags & LIST_DIM)) {
    fg = UI_DIM;
  }
  if (!hi && (r.flags & LIST_DANGER)) {
    fg = UI_DANGER;
  }

  int left = 8;
  int right = b.size.w - 6;
  if (r.tag) {
    graphics_context_set_fill_color(g, ui_color_from_tag(r.tag));
    graphics_fill_rect(g, GRect(0, 3, 5, b.size.h - 6), 0, GCornerNone);
    left = 11;
  }
  if (r.flags & (LIST_CHECKED | LIST_UNCHECKED)) {
    const int size = 18;
    GRect box = GRect(right - size, (b.size.h - size) / 2, size, size);
    graphics_context_set_stroke_color(g, fg);
    graphics_context_set_stroke_width(g, 2);
    graphics_draw_rect(g, box);
    if (r.flags & LIST_CHECKED) {
      graphics_context_set_fill_color(g, fg);
      graphics_fill_rect(g, grect_inset(box, GEdgeInsets(4)), 0, GCornerNone);
    }
    right -= size + 6;
  }

  GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  const bool two_lines = r.subtitle[0] != '\0';
  const int title_y = two_lines ? -2 : 3;

  if (r.right[0]) {
    GFont right_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GSize size = graphics_text_layout_get_content_size(
        r.right, right_font, GRect(0, 0, 110, 24), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentRight);
    graphics_context_set_text_color(g, sub);
    graphics_draw_text(g, r.right, right_font,
                       GRect(right - size.w, title_y + 5, size.w, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    right -= size.w + 6;
  }

  graphics_context_set_text_color(g, fg);
  graphics_draw_text(g, r.title, title_font, GRect(left, title_y, right - left, 30),
                     GTextOverflowModeTrailingEllipsis,
                     (r.flags & LIST_CENTER) ? GTextAlignmentCenter : GTextAlignmentLeft, NULL);
  if (two_lines) {
    graphics_context_set_text_color(g, sub);
    graphics_draw_text(g, r.subtitle, small_font, GRect(left, 26, b.size.w - left - 6, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  ListWindow *lw = context;
  if (lw->cb.select) {
    lw->cb.select(lw->ctx, index->section, index->row);
  }
}

static void prv_select_long(MenuLayer *menu, MenuIndex *index, void *context) {
  ListWindow *lw = context;
  if (lw->cb.select_long) {
    lw->cb.select_long(lw->ctx, index->section, index->row);
  } else if (lw->cb.select) {
    lw->cb.select(lw->ctx, index->section, index->row);
  }
}

static void prv_selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index,
                                  void *context) {
  ListWindow *lw = context;
  if (lw->cb.highlight) {
    lw->cb.highlight(lw->ctx, new_index.section, new_index.row);
  }
}

static void prv_title_update(Layer *layer, GContext *g) {
  ListWindow *lw = *(ListWindow **)layer_get_data(layer);
  ui_draw_title_bar(g, layer_get_bounds(layer), lw->title);
}

static void prv_update_empty(ListWindow *lw) {
  uint32_t total = 0;
  uint16_t sections = prv_sections(lw);
  for (uint16_t s = 0; s < sections; s++) {
    total += lw->cb.num_rows(lw->ctx, s);
  }
  layer_set_hidden(text_layer_get_layer(lw->empty_layer), total != 0 || !lw->cb.empty_text);
}

void list_window_reload(ListWindow *lw) {
  if (!lw || !lw->menu) {
    return;
  }
  menu_layer_reload_data(lw->menu);
  // Keep the selection on a row that still exists.
  MenuIndex index = menu_layer_get_selected_index(lw->menu);
  uint16_t sections = prv_sections(lw);
  if (index.section >= sections) {
    index.section = sections ? sections - 1 : 0;
  }
  uint16_t rows = sections ? lw->cb.num_rows(lw->ctx, index.section) : 0;
  if (rows && index.row >= rows) {
    index.row = rows - 1;
    menu_layer_set_selected_index(lw->menu, index, MenuRowAlignCenter, false);
  }
  prv_update_empty(lw);
  layer_mark_dirty(window_get_root_layer(lw->window));
}

static void prv_window_load(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  lw->title_layer = layer_create_with_data(GRect(0, 0, bounds.size.w, UI_TITLE_H),
                                           sizeof(ListWindow *));
  *(ListWindow **)layer_get_data(lw->title_layer) = lw;
  layer_set_update_proc(lw->title_layer, prv_title_update);
  layer_add_child(root, lw->title_layer);

  GRect menu_frame = GRect(0, UI_TITLE_H, bounds.size.w, bounds.size.h - UI_TITLE_H);
  lw->menu = menu_layer_create(menu_frame);
  menu_layer_set_callbacks(lw->menu, lw,
                           (MenuLayerCallbacks){
                               .get_num_sections = prv_num_sections,
                               .get_num_rows = prv_num_rows,
                               .get_cell_height = prv_cell_height,
                               .get_header_height = prv_header_height,
                               .draw_row = prv_draw_row,
                               .draw_header = prv_draw_header,
                               .select_click = prv_select,
                               .select_long_click = prv_select_long,
                               .selection_changed = prv_selection_changed,
                           });
  menu_layer_set_normal_colors(lw->menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(lw->menu, UI_ACCENT, UI_ON_ACCENT);
  menu_layer_set_click_config_onto_window(lw->menu, window);
  layer_add_child(root, menu_layer_get_layer(lw->menu));

  lw->empty_layer = text_layer_create(GRect(12, UI_TITLE_H + 50, bounds.size.w - 24, 120));
  text_layer_set_font(lw->empty_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(lw->empty_layer, GTextAlignmentCenter);
  text_layer_set_text_color(lw->empty_layer, UI_DIM);
  text_layer_set_background_color(lw->empty_layer, GColorClear);
  text_layer_set_text(lw->empty_layer, lw->cb.empty_text ? lw->cb.empty_text : "");
  layer_add_child(root, text_layer_get_layer(lw->empty_layer));
}

static void prv_window_appear(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  if (lw->cb.appear) {
    lw->cb.appear(lw->ctx);
  }
  list_window_reload(lw);
}

static void prv_window_unload(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  menu_layer_destroy(lw->menu);
  lw->menu = NULL;
  layer_destroy(lw->title_layer);
  text_layer_destroy(lw->empty_layer);
  prv_register(lw, false);
  if (lw->cb.destroy) {
    lw->cb.destroy(lw->ctx);
  }
  window_destroy(window);
  free(lw);
}

ListWindow *list_window_push(const char *title, const ListCallbacks *callbacks, void *ctx) {
  ListWindow *lw = malloc(sizeof(ListWindow));
  if (!lw) {
    return NULL;
  }
  memset(lw, 0, sizeof(*lw));
  lw->cb = *callbacks;
  lw->ctx = ctx;
  tt_copy(lw->title, sizeof(lw->title), title);
  lw->window = window_create();
  window_set_user_data(lw->window, lw);
  window_set_background_color(lw->window, GColorWhite);
  window_set_window_handlers(lw->window, (WindowHandlers){
                                             .load = prv_window_load,
                                             .appear = prv_window_appear,
                                             .unload = prv_window_unload,
                                         });
  prv_register(lw, true);
  window_stack_push(lw->window, true);
  return lw;
}

void ui_data_changed(void) {
  Window *top = window_stack_get_top_window();
  for (int i = 0; i < MAX_OPEN_LISTS; i++) {
    ListWindow *lw = s_open[i];
    if (lw && lw->window == top && lw->menu) {
      if (lw->cb.appear) {
        lw->cb.appear(lw->ctx);
      }
      list_window_reload(lw);
    }
  }
}

void list_window_close(ListWindow *lw) {
  if (lw) {
    window_stack_remove(lw->window, true);
  }
}

void list_window_select(ListWindow *lw, uint16_t section, uint16_t row) {
  if (lw && lw->menu) {
    menu_layer_set_selected_index(lw->menu, MenuIndex(section, row), MenuRowAlignCenter, false);
  }
}

void list_window_set_title(ListWindow *lw, const char *title) {
  if (lw) {
    tt_copy(lw->title, sizeof(lw->title), title);
    if (lw->title_layer) {
      layer_mark_dirty(lw->title_layer);
    }
  }
}

// ---------------------------------------------------------------- toast

typedef struct {
  Window *window;
  TextLayer *text_layer;
  AppTimer *timer;
  char text[80];
} Toast;

static void prv_toast_timeout(void *data) {
  Toast *toast = data;
  toast->timer = NULL;
  window_stack_remove(toast->window, true);
}

static void prv_toast_click(ClickRecognizerRef recognizer, void *context) {
  Toast *toast = context;
  window_stack_remove(toast->window, true);
}

static void prv_toast_clicks(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_toast_click);
  window_single_click_subscribe(BUTTON_ID_UP, prv_toast_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_toast_click);
}

static void prv_toast_load(Window *window) {
  Toast *toast = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GSize size = graphics_text_layout_get_content_size(
      toast->text, font, GRect(0, 0, bounds.size.w - 24, bounds.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter);
  toast->text_layer = text_layer_create(
      GRect(12, (bounds.size.h - size.h) / 2 - 6, bounds.size.w - 24, size.h + 12));
  text_layer_set_font(toast->text_layer, font);
  text_layer_set_text_alignment(toast->text_layer, GTextAlignmentCenter);
  text_layer_set_background_color(toast->text_layer, GColorClear);
  text_layer_set_text_color(toast->text_layer, UI_ON_ACCENT);
  text_layer_set_text(toast->text_layer, toast->text);
  layer_add_child(root, text_layer_get_layer(toast->text_layer));
}

static void prv_toast_unload(Window *window) {
  Toast *toast = window_get_user_data(window);
  if (toast->timer) {
    app_timer_cancel(toast->timer);
  }
  text_layer_destroy(toast->text_layer);
  window_destroy(window);
  free(toast);
}

void ui_toast(const char *text) {
  Toast *toast = malloc(sizeof(Toast));
  if (!toast) {
    return;
  }
  memset(toast, 0, sizeof(*toast));
  tt_copy(toast->text, sizeof(toast->text), text);
  toast->window = window_create();
  window_set_user_data(toast->window, toast);
  window_set_background_color(toast->window, UI_ACCENT);
  window_set_click_config_provider_with_context(toast->window, prv_toast_clicks, toast);
  window_set_window_handlers(toast->window, (WindowHandlers){
                                                .load = prv_toast_load,
                                                .unload = prv_toast_unload,
                                            });
  window_stack_push(toast->window, true);
  toast->timer = app_timer_register(1400, prv_toast_timeout, toast);
}
