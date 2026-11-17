#include "pickers.h"

// ---------------------------------------------------------------- spinner
//
// One window with up to three fields side by side. Up/Down change the active
// field, Select moves on (and confirms on the last field), Back steps back.

#define SPIN_MAX_FIELDS 3

typedef void (*SpinFormat)(int32_t value, char *buf, size_t cap);

typedef struct {
  int32_t value;
  int32_t min;
  int32_t max;
  bool wrap;
  uint8_t pad;        // zero-pad plain numbers to this many digits
  SpinFormat format;  // NULL = plain number
  const char *caption;
} SpinField;

typedef struct Spinner Spinner;
struct Spinner {
  Window *window;
  Layer *layer;
  char title[28];
  SpinField fields[SPIN_MAX_FIELDS];
  int count;
  int active;
  const char *separator;
  void (*adjust)(Spinner *s);
  void (*describe)(Spinner *s, char *buf, size_t cap);
  void (*finish)(Spinner *s);
  union {
    NumberPicked number;
    TimePicked time;
    DatePicked date;
    DurationPicked duration;
  } done;
  void *ctx;
};

static void prv_field_text(const SpinField *f, char *buf, size_t cap) {
  if (f->format) {
    f->format(f->value, buf, cap);
  } else if (f->pad == 2) {
    snprintf(buf, cap, "%02d", (int)f->value);
  } else {
    snprintf(buf, cap, "%d", (int)f->value);
  }
}

static void prv_triangle(GContext *g, int cx, int cy, bool up) {
  for (int i = 0; i < 7; i++) {
    int half = up ? i : 6 - i;
    graphics_draw_line(g, GPoint(cx - half, cy + i), GPoint(cx + half, cy + i));
  }
}

static void prv_spinner_update(Layer *layer, GContext *g) {
  Spinner *s = *(Spinner **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);
  ui_draw_title_bar(g, b, s->title);

  GFont font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  char text[SPIN_MAX_FIELDS][16];
  int width[SPIN_MAX_FIELDS];
  const int gap = 8;
  int sep_w = 0;
  if (s->separator && s->separator[0]) {
    sep_w = graphics_text_layout_get_content_size(s->separator, font, GRect(0, 0, 60, 40),
                                                  GTextOverflowModeFill, GTextAlignmentLeft)
                .w;
  }
  int total = 0;
  for (int i = 0; i < s->count; i++) {
    prv_field_text(&s->fields[i], text[i], sizeof(text[i]));
    width[i] = graphics_text_layout_get_content_size(text[i], font, GRect(0, 0, 180, 40),
                                                     GTextOverflowModeFill, GTextAlignmentLeft)
                   .w +
               12;
    total += width[i];
    if (i) {
      total += sep_w ? sep_w + 4 : gap;
    }
  }

  const int box_h = 44;
  const int y = UI_TITLE_H + (b.size.h - UI_TITLE_H) / 2 - box_h / 2 - 22;
  int x = (b.size.w - total) / 2;
  for (int i = 0; i < s->count; i++) {
    if (i) {
      if (sep_w) {
        graphics_context_set_text_color(g, GColorBlack);
        graphics_draw_text(g, s->separator, font, GRect(x + 2, y + 1, sep_w, box_h),
                           GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        x += sep_w + 4;
      } else {
        x += gap;
      }
    }
    GRect box = GRect(x, y, width[i], box_h);
    const bool active = (i == s->active);
    if (active) {
      graphics_context_set_fill_color(g, UI_ACCENT);
      graphics_fill_rect(g, box, 6, GCornersAll);
      graphics_context_set_stroke_color(g, GColorBlack);
      prv_triangle(g, x + width[i] / 2, y - 12, true);
      prv_triangle(g, x + width[i] / 2, y + box_h + 5, false);
    }
    graphics_context_set_text_color(g, active ? UI_ON_ACCENT : GColorBlack);
    graphics_draw_text(g, text[i], font, GRect(x, y + 1, width[i], box_h),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    if (s->fields[i].caption) {
      graphics_context_set_text_color(g, UI_DIM);
      graphics_draw_text(g, s->fields[i].caption, small,
                         GRect(x - 20, y + box_h + 12, width[i] + 40, 22),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    x += width[i];
  }

  if (s->describe) {
    char line[48] = "";
    s->describe(s, line, sizeof(line));
    graphics_context_set_text_color(g, GColorBlack);
    graphics_draw_text(g, line, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(4, b.size.h - 38, b.size.w - 8, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void prv_spin_change(Spinner *s, int delta) {
  SpinField *f = &s->fields[s->active];
  int32_t v = f->value + delta;
  const int32_t span = f->max - f->min + 1;
  if (f->wrap) {
    while (v > f->max) {
      v -= span;
    }
    while (v < f->min) {
      v += span;
    }
  } else {
    if (v > f->max) {
      v = f->max;
    }
    if (v < f->min) {
      v = f->min;
    }
  }
  f->value = v;
  if (s->adjust) {
    s->adjust(s);
  }
  layer_mark_dirty(s->layer);
}

static int prv_spin_step(Spinner *s, ClickRecognizerRef recognizer) {
  const SpinField *f = &s->fields[s->active];
  const int clicks = click_number_of_clicks_counted(recognizer);
  const int32_t span = f->max - f->min;
  if (span > 80 && clicks > 12) {
    return 10;
  }
  if (span > 20 && clicks > 12) {
    return 5;
  }
  return 1;
}

static void prv_spin_up(ClickRecognizerRef recognizer, void *context) {
  Spinner *s = context;
  prv_spin_change(s, prv_spin_step(s, recognizer));
}

static void prv_spin_down(ClickRecognizerRef recognizer, void *context) {
  Spinner *s = context;
  prv_spin_change(s, -prv_spin_step(s, recognizer));
}

static void prv_spin_select(ClickRecognizerRef recognizer, void *context) {
  Spinner *s = context;
  if (s->active + 1 < s->count) {
    s->active++;
    layer_mark_dirty(s->layer);
    return;
  }
  // Hand the result over first: the callback may push its own window, and the
  // spinner then quietly leaves from underneath it.
  s->finish(s);
  window_stack_remove(s->window, false);
}

static void prv_spin_back(ClickRecognizerRef recognizer, void *context) {
  Spinner *s = context;
  if (s->active > 0) {
    s->active--;
    layer_mark_dirty(s->layer);
  } else {
    window_stack_remove(s->window, true);
  }
}

static void prv_spin_clicks(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 90, prv_spin_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 90, prv_spin_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_spin_select);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_spin_back);
}

static void prv_spin_load(Window *window) {
  Spinner *s = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  s->layer = layer_create_with_data(layer_get_bounds(root), sizeof(Spinner *));
  *(Spinner **)layer_get_data(s->layer) = s;
  layer_set_update_proc(s->layer, prv_spinner_update);
  layer_add_child(root, s->layer);
}

static void prv_spin_unload(Window *window) {
  Spinner *s = window_get_user_data(window);
  layer_destroy(s->layer);
  window_destroy(window);
  free(s);
}

static Spinner *prv_spinner_new(const char *title) {
  Spinner *s = malloc(sizeof(Spinner));
  if (!s) {
    return NULL;
  }
  memset(s, 0, sizeof(*s));
  tt_copy(s->title, sizeof(s->title), title);
  s->window = window_create();
  window_set_user_data(s->window, s);
  window_set_background_color(s->window, GColorWhite);
  window_set_click_config_provider_with_context(s->window, prv_spin_clicks, s);
  window_set_window_handlers(s->window, (WindowHandlers){
                                            .load = prv_spin_load,
                                            .unload = prv_spin_unload,
                                        });
  return s;
}

// ---------------------------------------------------------------- number

static void prv_number_finish(Spinner *s) {
  s->done.number(s->ctx, s->fields[0].value);
}

void pick_number(const char *title, int32_t initial, int32_t min, int32_t max, const char *unit,
                 NumberPicked done, void *ctx) {
  Spinner *s = prv_spinner_new(title);
  if (!s) {
    return;
  }
  if (initial < min) {
    initial = min;
  }
  if (initial > max) {
    initial = max;
  }
  s->fields[0] = (SpinField){.value = initial, .min = min, .max = max, .caption = unit};
  s->count = 1;
  s->finish = prv_number_finish;
  s->done.number = done;
  s->ctx = ctx;
  window_stack_push(s->window, true);
}

// ---------------------------------------------------------------- time

static void prv_time_describe(Spinner *s, char *buf, size_t cap) {
  if (clock_is_24h_style()) {
    buf[0] = '\0';
    return;
  }
  tt_format_clock(s->fields[0].value * 60 + s->fields[1].value, false, buf, cap);
}

static void prv_time_finish(Spinner *s) {
  s->done.time(s->ctx, (int)(s->fields[0].value * 60 + s->fields[1].value));
}

void pick_time(const char *title, int minute_of_day, TimePicked done, void *ctx) {
  Spinner *s = prv_spinner_new(title);
  if (!s) {
    return;
  }
  s->fields[0] = (SpinField){.value = minute_of_day / 60, .min = 0, .max = 23, .wrap = true,
                             .pad = 2, .caption = "hour"};
  s->fields[1] = (SpinField){.value = minute_of_day % 60, .min = 0, .max = 59, .wrap = true,
                             .pad = 2, .caption = "min"};
  s->count = 2;
  s->separator = ":";
  s->describe = prv_time_describe;
  s->finish = prv_time_finish;
  s->done.time = done;
  s->ctx = ctx;
  window_stack_push(s->window, true);
}

// ---------------------------------------------------------------- date

static void prv_month_format(int32_t value, char *buf, size_t cap) {
  tt_copy(buf, cap, tt_month_abbr[(value + 11) % 12]);
}

static void prv_date_adjust(Spinner *s) {
  int dim = tt_days_in_month((int)s->fields[2].value, (int)s->fields[1].value);
  s->fields[0].max = dim;
  if (s->fields[0].value > dim) {
    s->fields[0].value = dim;
  }
}

static void prv_date_describe(Spinner *s, char *buf, size_t cap) {
  static const char *const names[7] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                       "Friday", "Saturday", "Sunday"};
  int32_t day = tt_days_from_civil((int)s->fields[2].value, (int)s->fields[1].value,
                                   (int)s->fields[0].value);
  int32_t today;
  tt_epoch_to_local(time(NULL), &today, NULL, NULL);
  int32_t delta = day - today;
  if (delta == 0) {
    snprintf(buf, cap, "%s, today", names[tt_dow(day)]);
  } else if (delta > 0) {
    snprintf(buf, cap, "%s, in %dd", names[tt_dow(day)], (int)delta);
  } else {
    snprintf(buf, cap, "%s, %dd ago", names[tt_dow(day)], (int)-delta);
  }
}

static void prv_date_finish(Spinner *s) {
  TTDate date = {(int16_t)s->fields[2].value, (uint8_t)s->fields[1].value,
                 (uint8_t)s->fields[0].value};
  s->done.date(s->ctx, date);
}

void pick_date(const char *title, TTDate initial, DatePicked done, void *ctx) {
  Spinner *s = prv_spinner_new(title);
  if (!s) {
    return;
  }
  int32_t today;
  tt_epoch_to_local(time(NULL), &today, NULL, NULL);
  TTDate now = tt_civil_from_days(today);
  if (!tt_date_is_set(initial)) {
    initial = now;
  }
  s->fields[0] = (SpinField){.value = initial.d, .min = 1, .max = 31, .wrap = true,
                             .caption = "day"};
  s->fields[1] = (SpinField){.value = initial.m, .min = 1, .max = 12, .wrap = true,
                             .format = prv_month_format, .caption = "month"};
  s->fields[2] = (SpinField){.value = initial.y, .min = now.y - 1, .max = 2099,
                             .caption = "year"};
  if (s->fields[2].value < s->fields[2].min) {
    s->fields[2].min = s->fields[2].value;
  }
  s->count = 3;
  s->adjust = prv_date_adjust;
  s->describe = prv_date_describe;
  s->finish = prv_date_finish;
  s->done.date = done;
  s->ctx = ctx;
  prv_date_adjust(s);
  window_stack_push(s->window, true);
}

// ---------------------------------------------------------------- duration

static const uint32_t s_unit_minutes[] = {1, 60, 1440, 10080};

static void prv_unit_format(int32_t value, char *buf, size_t cap) {
  static const char *const names[] = {"min", "hours", "days", "weeks"};
  tt_copy(buf, cap, names[value & 3]);
}

static uint32_t prv_duration_minutes(Spinner *s) {
  uint32_t minutes = (uint32_t)s->fields[0].value * s_unit_minutes[s->fields[1].value & 3];
  return minutes > TT_SNOOZE_MAX_MIN ? TT_SNOOZE_MAX_MIN : minutes;
}

static void prv_duration_describe(Spinner *s, char *buf, size_t cap) {
  ui_format_when(time(NULL) + (time_t)prv_duration_minutes(s) * 60, buf, cap);
}

static void prv_duration_finish(Spinner *s) {
  s->done.duration(s->ctx, prv_duration_minutes(s));
}

void pick_duration(const char *title, uint32_t minutes, DurationPicked done, void *ctx) {
  Spinner *s = prv_spinner_new(title);
  if (!s) {
    return;
  }
  if (minutes == 0) {
    minutes = 5;
  }
  int unit = 0;
  for (int u = 3; u >= 1; u--) {
    if (minutes % s_unit_minutes[u] == 0) {
      unit = u;
      break;
    }
  }
  uint32_t value = minutes / s_unit_minutes[unit];
  if (value > 999) {
    // Too long to count in this unit: round into the next one up.
    while (value > 999 && unit < 3) {
      unit++;
      value = minutes / s_unit_minutes[unit];
    }
    if (value > 999) {
      value = 999;
    }
  }
  s->fields[0] = (SpinField){.value = (int32_t)value, .min = 1, .max = 999};
  s->fields[1] = (SpinField){.value = unit, .min = 0, .max = 3, .wrap = true,
                             .format = prv_unit_format};
  s->count = 2;
  s->describe = prv_duration_describe;
  s->finish = prv_duration_finish;
  s->done.duration = done;
  s->ctx = ctx;
  window_stack_push(s->window, true);
}

// ---------------------------------------------------------------- choice

typedef struct {
  ListWindow *lw;
  char items[PICK_CHOICE_MAX][PICK_CHOICE_LEN];
  int count;
  int selected;
  ChoicePicked done;
  ChoicePicked highlight;
  void *ctx;
} Choice;

static uint16_t prv_choice_rows(void *ctx, uint16_t section) {
  return (uint16_t)((Choice *)ctx)->count;
}

static void prv_choice_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  Choice *c = ctx;
  tt_copy(out->title, sizeof(out->title), c->items[row]);
  if ((int)row == c->selected) {
    out->flags |= LIST_CHECKED;
  }
}

static void prv_choice_select(void *ctx, uint16_t section, uint16_t row) {
  Choice *c = ctx;
  ListWindow *lw = c->lw;
  c->done(c->ctx, row);
  list_window_close(lw);
}

static void prv_choice_highlight(void *ctx, uint16_t section, uint16_t row) {
  Choice *c = ctx;
  if (c->highlight) {
    c->highlight(c->ctx, row);
  }
}

static void prv_choice_destroy(void *ctx) {
  free(ctx);
}

void pick_choice(const char *title, const char *const *items, int count, int selected,
                 ChoicePicked done, ChoicePicked highlight, void *ctx) {
  Choice *c = malloc(sizeof(Choice));
  if (!c) {
    return;
  }
  memset(c, 0, sizeof(*c));
  c->count = count > PICK_CHOICE_MAX ? PICK_CHOICE_MAX : count;
  for (int i = 0; i < c->count; i++) {
    tt_copy(c->items[i], sizeof(c->items[i]), items[i]);
  }
  c->selected = selected;
  c->done = done;
  c->highlight = highlight;
  c->ctx = ctx;
  static const ListCallbacks callbacks = {
      .num_rows = prv_choice_rows,
      .row = prv_choice_row,
      .select = prv_choice_select,
      .highlight = prv_choice_highlight,
      .destroy = prv_choice_destroy,
  };
  c->lw = list_window_push(title, &callbacks, c);
  if (c->lw && selected > 0 && selected < c->count) {
    list_window_select(c->lw, 0, (uint16_t)selected);
  }
}
