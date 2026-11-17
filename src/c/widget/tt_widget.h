// tt_widget: draws "the next few alarms" into any rectangle.
//
// This pair of files depends on nothing but <pebble.h>, so a watchface can
// copy them in as they are. Time To uses them on its own home screen; a
// watchface fills TTWidgetItem from the `alarms/upcoming` plugin source that
// Time To serves on the phone (see docs/WIDGETS.md and examples/face).
#pragma once

#include <pebble.h>

#define TT_WIDGET_NAME_MAX 32
#define TT_WIDGET_MAX_ITEMS 5

typedef struct {
  time_t when;                     // when it rings, epoch seconds (0 = unused slot)
  char name[TT_WIDGET_NAME_MAX];   // the alarm's message
  bool snoozed;                    // a snoozed alarm coming back
} TTWidgetItem;

typedef struct {
  GColor text;        // names
  GColor accent;      // times
  GFont time_font;    // NULL = Gothic 18 bold
  GFont name_font;    // NULL = Gothic 18
  int16_t row_height; // 0 = 22
  bool relative;      // "in 3h" instead of clock times for the first entry
  const char *empty;  // shown when there is nothing coming up (NULL = "No alarms")
} TTWidgetStyle;

//! Sensible defaults: black text, accent-coloured times.
TTWidgetStyle tt_widget_default_style(void);

//! Draw up to `max_rows` upcoming alarms, soonest first, inside `bounds`.
//! Items whose time has passed are skipped, so a stale list degrades cleanly.
void tt_widget_draw(GContext *ctx, GRect bounds, const TTWidgetItem *items, int count,
                    int max_rows, const TTWidgetStyle *style);

//! Height needed for `rows` rows with this style.
int16_t tt_widget_height(int rows, const TTWidgetStyle *style);

//! "07:30", "Tue 07:30" or "19 Nov" depending on how far away `when` is.
void tt_widget_format_when(time_t when, char *buf, size_t cap);
//! "now", "5m", "3h 12m", "2d".
void tt_widget_format_until(time_t when, char *buf, size_t cap);
