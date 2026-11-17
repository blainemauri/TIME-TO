// Shared look and the list window every menu-like screen is built from.
#pragma once

#include <pebble.h>

#include "../core/tt_core.h"

#define UI_TITLE_H 24

#define UI_ACCENT PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack)
#define UI_ON_ACCENT PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite)
#define UI_DIM PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define UI_DANGER PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack)

#define LIST_DIM 0x01        // greyed out (an alarm that is switched off)
#define LIST_CHECKED 0x02    // draw a ticked box at the right
#define LIST_UNCHECKED 0x04  // draw an empty box at the right
#define LIST_CENTER 0x08     // centred title, for action rows
#define LIST_DANGER 0x10     // red title, for destructive actions

typedef struct {
  char title[48];
  char subtitle[64];
  char right[14];  // short right-aligned value
  uint8_t flags;   // LIST_*
  uint32_t tag;    // colour tag (0x1RRGGBB), 0 = none
} ListRow;

typedef struct ListWindow ListWindow;

typedef struct {
  uint16_t (*num_sections)(void *ctx);  // optional, default 1
  uint16_t (*num_rows)(void *ctx, uint16_t section);
  void (*header)(void *ctx, uint16_t section, char *buf, size_t cap);  // optional
  void (*row)(void *ctx, uint16_t section, uint16_t row, ListRow *out);
  void (*select)(void *ctx, uint16_t section, uint16_t row);
  void (*select_long)(void *ctx, uint16_t section, uint16_t row);  // optional
  void (*highlight)(void *ctx, uint16_t section, uint16_t row);    // optional
  void (*appear)(void *ctx);   // optional: called each time the window comes to the front
  void (*destroy)(void *ctx);  // optional: called once when the window is gone
  // Optional custom cells: a height of 0 / a draw returning false means "the default".
  int16_t (*row_height)(void *ctx, uint16_t section, uint16_t row);
  bool (*draw)(void *ctx, GContext *g, GRect bounds, uint16_t section, uint16_t row,
               bool highlighted);
  const char *empty_text;  // optional: shown when the list has no rows
} ListCallbacks;

ListWindow *list_window_push(const char *title, const ListCallbacks *callbacks, void *ctx);
void list_window_reload(ListWindow *lw);
void list_window_close(ListWindow *lw);
void list_window_select(ListWindow *lw, uint16_t section, uint16_t row);
void list_window_set_title(ListWindow *lw, const char *title);

//! The orange strip with a title on the left and the time on the right.
void ui_draw_title_bar(GContext *g, GRect bounds, const char *title);
//! A message that shows for a moment and removes itself.
void ui_toast(const char *text);
//! Clock time honouring the watch's 12/24h setting.
void ui_format_clock(int minute_of_day, char *buf, size_t cap);
//! "Today 07:30", "Tomorrow 07:30", "Thu 19 Nov 07:30".
void ui_format_when(time_t when, char *buf, size_t cap);
//! "Today", "Tomorrow", "Thu 19 Nov".
void ui_format_day(time_t when, char *buf, size_t cap);
GColor ui_color_from_tag(uint32_t tag);
//! Redraw whatever window is on top (used by the minute tick).
void ui_refresh_top(void);
//! Stored data changed behind the screens' backs (the phone sent something):
//! rebuild the list that is currently showing.
void ui_data_changed(void);
