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

// ---------------------------------------------------------------- text
//
// A menu of ways to fill in text: dictation (on watches with a microphone and
// a phone to transcribe), a few quick phrases, or spelling it out on a
// letter wheel that needs nothing but the watch.

#define TEXT_MAX 96

static const char *const s_phrases[] = {
    "Wake up",       "Take meds",     "Drink water", "Stand up and stretch",
    "Leave now",     "Check the oven", "Call back",  "Bedtime",
};

static const char s_charset[] =
    " abcdefghijklmnopqrstuvwxyz0123456789.,!?'-:/&@#()+ABCDEFGHIJKLMNOPQRSTUVWXYZ";
#define CHARSET_LEN ((int)sizeof(s_charset) - 1)

typedef struct {
  ListWindow *lw;
  char text[TEXT_MAX];
  size_t max_len;
  TextPicked done;
  void *ctx;
  int dictate_row;  // -1 when there is no dictation
#if defined(PBL_MICROPHONE)
  DictationSession *dictation;
#endif
} TextMenu;

typedef struct {
  Window *window;
  Layer *layer;
  TextMenu *menu;
  char text[TEXT_MAX];
  int cursor;  // index into s_charset
} Wheel;

static void prv_text_finish(TextMenu *m, const char *text) {
  // Report first: the callback refreshes the screen underneath.
  m->done(m->ctx, text);
  list_window_close(m->lw);
}

static void prv_wheel_update(Layer *layer, GContext *g) {
  Wheel *w = *(Wheel **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);
  ui_draw_title_bar(g, b, "Spell it");

  // The text so far, with a caret.
  char shown[TEXT_MAX + 2];
  snprintf(shown, sizeof(shown), "%s_", w->text);
  graphics_context_set_text_color(g, GColorBlack);
  graphics_draw_text(g, shown, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(6, UI_TITLE_H + 2, b.size.w - 12, 82), GTextOverflowModeWordWrap,
                     GTextAlignmentLeft, NULL);

  // The wheel: the current character large, neighbours either side.
  const int cy = b.size.h - 78;
  graphics_context_set_fill_color(g, UI_ACCENT);
  graphics_fill_rect(g, GRect(b.size.w / 2 - 22, cy, 44, 44), 6, GCornersAll);
  GFont big = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_24);
  for (int off = -3; off <= 3; off++) {
    int i = ((w->cursor + off) % CHARSET_LEN + CHARSET_LEN) % CHARSET_LEN;
    char c[4];
    if (s_charset[i] == ' ') {
      tt_copy(c, sizeof(c), "_");
    } else {
      c[0] = s_charset[i];
      c[1] = '\0';
    }
    const int x = b.size.w / 2 + off * 28 - 14;
    graphics_context_set_text_color(g, off == 0 ? UI_ON_ACCENT : UI_DIM);
    graphics_draw_text(g, c, off == 0 ? big : small, GRect(x, cy + (off == 0 ? 3 : 8), 28, 34),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  graphics_context_set_text_color(g, UI_DIM);
  graphics_draw_text(g, "Select add  Hold done  Back erase",
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(2, b.size.h - 26, b.size.w - 4, 20), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void prv_wheel_move(Wheel *w, int delta) {
  w->cursor = ((w->cursor + delta) % CHARSET_LEN + CHARSET_LEN) % CHARSET_LEN;
  layer_mark_dirty(w->layer);
}

static void prv_wheel_up(ClickRecognizerRef r, void *context) {
  prv_wheel_move(context, -1);
}

static void prv_wheel_down(ClickRecognizerRef r, void *context) {
  prv_wheel_move(context, 1);
}

static void prv_wheel_add(ClickRecognizerRef r, void *context) {
  Wheel *w = context;
  size_t len = strlen(w->text);
  if (len + 1 < sizeof(w->text) && len < w->menu->max_len) {
    w->text[len] = s_charset[w->cursor];
    w->text[len + 1] = '\0';
    // After the first letter, carry on in lower case.
    if (len == 0 && s_charset[w->cursor] >= 'A' && s_charset[w->cursor] <= 'Z') {
      w->cursor = (int)(strchr(s_charset, s_charset[w->cursor] + 32) - s_charset);
    }
  }
  layer_mark_dirty(w->layer);
}

static void prv_wheel_done(ClickRecognizerRef r, void *context) {
  Wheel *w = context;
  TextMenu *m = w->menu;
  char text[TEXT_MAX];
  tt_copy(text, sizeof(text), w->text);
  window_stack_remove(w->window, false);
  prv_text_finish(m, text);
}

static void prv_wheel_back(ClickRecognizerRef r, void *context) {
  Wheel *w = context;
  size_t len = strlen(w->text);
  if (len == 0) {
    window_stack_remove(w->window, true);
    return;
  }
  w->text[len - 1] = '\0';
  layer_mark_dirty(w->layer);
}

static void prv_wheel_clicks(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 80, prv_wheel_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 80, prv_wheel_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_wheel_add);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, prv_wheel_done, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_wheel_back);
}

static void prv_wheel_load(Window *window) {
  Wheel *w = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  w->layer = layer_create_with_data(layer_get_bounds(root), sizeof(Wheel *));
  *(Wheel **)layer_get_data(w->layer) = w;
  layer_set_update_proc(w->layer, prv_wheel_update);
  layer_add_child(root, w->layer);
}

static void prv_wheel_unload(Window *window) {
  Wheel *w = window_get_user_data(window);
  layer_destroy(w->layer);
  window_destroy(window);
  free(w);
}

static void prv_wheel_show(TextMenu *m) {
  Wheel *w = malloc(sizeof(Wheel));
  if (!w) {
    return;
  }
  memset(w, 0, sizeof(*w));
  w->menu = m;
  tt_copy(w->text, sizeof(w->text), m->text);
  w->cursor = (int)(strchr(s_charset, w->text[0] ? 'a' : 'A') - s_charset);
  w->window = window_create();
  window_set_user_data(w->window, w);
  window_set_background_color(w->window, GColorWhite);
  window_set_click_config_provider_with_context(w->window, prv_wheel_clicks, w);
  window_set_window_handlers(w->window, (WindowHandlers){
                                            .load = prv_wheel_load,
                                            .unload = prv_wheel_unload,
                                        });
  window_stack_push(w->window, true);
}

#if defined(PBL_MICROPHONE)
static void prv_dictated(DictationSession *session, DictationSessionStatus status,
                         char *transcription, void *context) {
  TextMenu *m = context;
  if (status == DictationSessionStatusSuccess && transcription) {
    char text[TEXT_MAX];
    tt_copy(text, sizeof(text), transcription);
    prv_text_finish(m, text);
  } else if (status == DictationSessionStatusFailureConnectivityError ||
             status == DictationSessionStatusFailureNoSpeechDetected ||
             status == DictationSessionStatusFailureDisabled ||
             status == DictationSessionStatusFailureInternalError) {
    ui_toast("Dictation didn't work. Try spelling it.");
  }
}
#endif

enum { TEXT_KEEP = 100, TEXT_SPELL, TEXT_CLEAR };

static int prv_text_row_kind(TextMenu *m, int row) {
  int i = 0;
  if (m->text[0]) {
    if (row == i++) {
      return TEXT_KEEP;
    }
  }
  if (m->dictate_row >= 0 && row == i++) {
    return -1;  // dictate
  }
  if (row == i++) {
    return TEXT_SPELL;
  }
  if (m->text[0]) {
    if (row == i++) {
      return TEXT_CLEAR;
    }
  }
  return row - i;  // a phrase
}

static uint16_t prv_text_rows(void *ctx, uint16_t section) {
  TextMenu *m = ctx;
  return (uint16_t)((m->text[0] ? 2 : 0) + (m->dictate_row >= 0 ? 1 : 0) + 1 +
                    (int)ARRAY_LENGTH(s_phrases));
}

static void prv_text_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  TextMenu *m = ctx;
  const int kind = prv_text_row_kind(m, row);
  switch (kind) {
    case TEXT_KEEP:
      tt_copy(out->title, sizeof(out->title), "Edit current");
      tt_copy(out->subtitle, sizeof(out->subtitle), m->text);
      break;
    case -1:
      tt_copy(out->title, sizeof(out->title), "Dictate");
      tt_copy(out->subtitle, sizeof(out->subtitle), "Speak the message");
      break;
    case TEXT_SPELL:
      tt_copy(out->title, sizeof(out->title), m->text[0] ? "Spell new" : "Spell it");
      tt_copy(out->subtitle, sizeof(out->subtitle), "Letter by letter");
      break;
    case TEXT_CLEAR:
      tt_copy(out->title, sizeof(out->title), "Clear");
      out->flags |= LIST_DANGER;
      break;
    default:
      if (kind >= 0 && kind < (int)ARRAY_LENGTH(s_phrases)) {
        tt_copy(out->title, sizeof(out->title), s_phrases[kind]);
      }
      break;
  }
}

static void prv_text_select(void *ctx, uint16_t section, uint16_t row) {
  TextMenu *m = ctx;
  const int kind = prv_text_row_kind(m, row);
  switch (kind) {
    case TEXT_KEEP:
      prv_wheel_show(m);
      break;
    case -1:
#if defined(PBL_MICROPHONE)
      if (!m->dictation) {
        m->dictation = dictation_session_create(TEXT_MAX, prv_dictated, m);
      }
      if (m->dictation) {
        dictation_session_enable_confirmation(m->dictation, true);
        dictation_session_start(m->dictation);
      } else {
        ui_toast("Dictation isn't available");
      }
#endif
      break;
    case TEXT_SPELL: {
      char saved[TEXT_MAX];
      tt_copy(saved, sizeof(saved), m->text);
      m->text[0] = '\0';
      prv_wheel_show(m);
      tt_copy(m->text, sizeof(m->text), saved);
      break;
    }
    case TEXT_CLEAR:
      prv_text_finish(m, "");
      break;
    default:
      if (kind >= 0 && kind < (int)ARRAY_LENGTH(s_phrases)) {
        prv_text_finish(m, s_phrases[kind]);
      }
      break;
  }
}

static void prv_text_destroy(void *ctx) {
  TextMenu *m = ctx;
#if defined(PBL_MICROPHONE)
  if (m->dictation) {
    dictation_session_destroy(m->dictation);
  }
#endif
  free(m);
}

void pick_text(const char *title, const char *initial, size_t max_len, TextPicked done,
               void *ctx) {
  TextMenu *m = malloc(sizeof(TextMenu));
  if (!m) {
    return;
  }
  memset(m, 0, sizeof(*m));
  tt_copy(m->text, sizeof(m->text), initial);
  m->max_len = max_len < TEXT_MAX - 1 ? max_len : TEXT_MAX - 1;
  m->done = done;
  m->ctx = ctx;
  m->dictate_row = PBL_IF_MICROPHONE_ELSE(0, -1);
  static const ListCallbacks callbacks = {
      .num_rows = prv_text_rows,
      .row = prv_text_row,
      .select = prv_text_select,
      .destroy = prv_text_destroy,
  };
  m->lw = list_window_push(title, &callbacks, m);
}
