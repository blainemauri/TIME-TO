// An example watchface: the time, the date, and the next alarms from Time To.
//
// The phone half (src/pkjs/index.js) reads Time To's plugin source
// `alarms/upcoming` and sends the next few rings here as lines of
// "epoch<TAB>snoozed<TAB>name". This file keeps them (so they survive a
// restart) and draws them with tt_widget_draw().
#include <pebble.h>

#include "../../../../src/c/widget/tt_widget.h"

#define MAX_ITEMS 3
#define KEY_ITEMS 1

static Window *s_window;
static Layer *s_layer;
static TTWidgetItem s_items[MAX_ITEMS];
static int s_count;

static void prv_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const bool round = PBL_IF_ROUND_ELSE(true, false);
  const int inset = round ? 30 : 8;
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  char clock[8];
  strftime(clock, sizeof(clock), clock_is_24h_style() ? "%H:%M" : "%I:%M", tm);
  const char *shown = (!clock_is_24h_style() && clock[0] == '0') ? clock + 1 : clock;
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, shown, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                     GRect(0, b.size.h / 2 - 78, b.size.w, 50), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  char date[24];
  strftime(date, sizeof(date), "%a %e %b", tm);
  graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  graphics_draw_text(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, b.size.h / 2 - 28, b.size.w, 30), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);

  // The widget: the next alarms, below a thin rule.
  const int top = b.size.h / 2 + 10;
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
  graphics_draw_line(ctx, GPoint(inset, top - 4), GPoint(b.size.w - inset, top - 4));
  TTWidgetStyle style = tt_widget_default_style();
  style.text = GColorWhite;
  style.accent = PBL_IF_COLOR_ELSE(GColorOrange, GColorWhite);
  style.relative = true;
  style.empty = "No alarms coming up";
  tt_widget_draw(ctx, GRect(inset, top, b.size.w - 2 * inset, b.size.h - top - 4), s_items, s_count,
                 MAX_ITEMS, &style);
}

static void prv_tick(struct tm *tick_time, TimeUnits changed) {
  layer_mark_dirty(s_layer);
}

static void prv_save(void) {
  persist_write_int(KEY_ITEMS, s_count);
  for (int i = 0; i < s_count; i++) {
    persist_write_data(KEY_ITEMS + 1 + i, &s_items[i], sizeof(TTWidgetItem));
  }
}

static void prv_load(void) {
  s_count = persist_read_int(KEY_ITEMS);
  if (s_count < 0 || s_count > MAX_ITEMS) {
    s_count = 0;
  }
  for (int i = 0; i < s_count; i++) {
    if (persist_read_data(KEY_ITEMS + 1 + i, &s_items[i], sizeof(TTWidgetItem)) !=
        (int)sizeof(TTWidgetItem)) {
      s_count = i;
      break;
    }
  }
}

//! "epoch\tsnoozed\tname\n" lines -> s_items.
static void prv_parse(const char *text) {
  s_count = 0;
  const char *p = text;
  while (*p && s_count < MAX_ITEMS) {
    TTWidgetItem *item = &s_items[s_count];
    memset(item, 0, sizeof(*item));
    long when = 0;
    while (*p >= '0' && *p <= '9') {
      when = when * 10 + (*p++ - '0');
    }
    if (*p == '\t') {
      p++;
    }
    item->snoozed = (*p == '1');
    while (*p && *p != '\t' && *p != '\n') {
      p++;
    }
    if (*p == '\t') {
      p++;
    }
    size_t n = 0;
    while (*p && *p != '\n') {
      if (n + 1 < sizeof(item->name)) {
        item->name[n++] = *p;
      }
      p++;
    }
    item->name[n] = '\0';
    if (*p == '\n') {
      p++;
    }
    item->when = (time_t)when;
    if (when) {
      s_count++;
    }
  }
}

static void prv_inbox(DictionaryIterator *iter, void *context) {
  Tuple *rows = dict_find(iter, MESSAGE_KEY_ROWS);
  if (rows && rows->type == TUPLE_CSTRING) {
    prv_parse(rows->value->cstring);
    prv_save();
    layer_mark_dirty(s_layer);
  }
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, prv_update);
  layer_add_child(root, s_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_layer);
}

int main(void) {
  prv_load();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = prv_window_load,
                                           .unload = prv_window_unload,
                                       });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);
  app_message_register_inbox_received(prv_inbox);
  app_message_open(512, 64);
  app_event_loop();
  window_destroy(s_window);
}
