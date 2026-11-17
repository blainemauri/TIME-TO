// Buzz patterns and sounds, and the composer that records a pattern from
// how long you hold the Select button down.
#include "../buzz.h"
#include "../main.h"
#include "../store.h"
#include "pickers.h"
#include "screens.h"

// ---------------------------------------------------------------- composer

#define REC_MAX_SEGS (TT_PATTERN_MAX_SEGS - 1)  // must end on a buzz
#define REC_MAX_MS 30000                        // the whole pattern
#define REC_PREVIEW_STRENGTH 100

typedef struct {
  Window *window;
  Layer *layer;
  TTPattern pattern;
  bool holding;
  uint64_t edge_ms;  // when the button last went down or up
  uint64_t start_ms;
  AppTimer *tick;
  bool saved;
} Composer;

static uint64_t prv_now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint64_t)s * 1000 + ms;
}

static uint32_t prv_total(const Composer *c) {
  return tt_pattern_total_ms(&c->pattern);
}

static void prv_composer_tick(void *data) {
  Composer *c = data;
  c->tick = NULL;
  layer_mark_dirty(c->layer);
  if (c->holding) {
    c->tick = app_timer_register(100, prv_composer_tick, c);
  }
}

static void prv_composer_update(Layer *layer, GContext *g) {
  Composer *c = *(Composer **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);
  ui_draw_title_bar(g, b, "Record a buzz");
  const int w = b.size.w - 16;

  // How-to, until the first press.
  GFont body = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  graphics_context_set_text_color(g, GColorBlack);
  if (c->pattern.n == 0 && !c->holding) {
    graphics_draw_text(g, "Hold SELECT to buzz, let go to pause. Tap out your rhythm.", body,
                       GRect(8, UI_TITLE_H + 6, w, 110), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
  } else {
    // The pattern as a strip of bars, scaled to fit, with the live press growing.
    uint32_t total = prv_total(c);
    uint32_t live = 0;
    if (c->holding) {
      live = (uint32_t)(prv_now_ms() - c->edge_ms);
    }
    uint32_t span = total + live;
    if (span < 2000) {
      span = 2000;
    }
    const int y = UI_TITLE_H + 22;
    const int h = 36;
    graphics_context_set_stroke_color(g, UI_DIM);
    graphics_draw_rect(g, GRect(8, y - 2, w, h + 4));
    graphics_context_set_fill_color(g, UI_ACCENT);
    uint32_t at = 0;
    for (int i = 0; i < c->pattern.n; i++) {
      const uint32_t len = c->pattern.seg[i];
      if (i % 2 == 0) {
        int x0 = 8 + (int)((uint64_t)at * w / span);
        int x1 = 8 + (int)((uint64_t)(at + len) * w / span);
        graphics_fill_rect(g, GRect(x0, y, x1 - x0 < 1 ? 1 : x1 - x0, h), 0, GCornerNone);
      }
      at += len;
    }
    if (c->holding) {
      // The silence before this press is already known: it is now - the last release.
      graphics_context_set_fill_color(g, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack));
      int x0 = 8 + (int)((uint64_t)at * w / span);
      int x1 = 8 + (int)((uint64_t)(at + live) * w / span);
      graphics_fill_rect(g, GRect(x0, y, x1 - x0 < 1 ? 1 : x1 - x0, h), 0, GCornerNone);
    }
    char info[48];
    snprintf(info, sizeof(info), "%d buzz%s, %lu.%lus", (c->pattern.n + 1) / 2,
             (c->pattern.n + 1) / 2 == 1 ? "" : "es", (unsigned long)(total / 1000),
             (unsigned long)((total % 1000) / 100));
    graphics_draw_text(g, info, body, GRect(8, y + h + 8, w, 30), GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
    if (c->pattern.n >= REC_MAX_SEGS || total >= REC_MAX_MS) {
      graphics_draw_text(g, "That's the most a pattern can hold.", small,
                         GRect(8, y + h + 36, w, 40), GTextOverflowModeWordWrap,
                         GTextAlignmentLeft, NULL);
    }
  }

  // What the other buttons do.
  graphics_context_set_text_color(g, UI_DIM);
  graphics_draw_text(g, "UP play   hold UP restart\nDOWN save   BACK leave", small,
                     GRect(8, b.size.h - 46, w, 44), GTextOverflowModeWordWrap,
                     GTextAlignmentLeft, NULL);
}

static bool prv_full(const Composer *c) {
  return c->pattern.n >= REC_MAX_SEGS || prv_total(c) >= REC_MAX_MS;
}

static void prv_add_seg(Composer *c, uint64_t ms) {
  if (ms > TT_PATTERN_MAX_MS) {
    ms = TT_PATTERN_MAX_MS;
  }
  if (ms < 20) {
    ms = 20;  // anything shorter the motor cannot show anyway
  }
  if (c->pattern.n < TT_PATTERN_MAX_SEGS) {
    c->pattern.seg[c->pattern.n++] = (uint16_t)ms;
  }
}

static void prv_press(ClickRecognizerRef r, void *context) {
  Composer *c = context;
  if (c->holding || prv_full(c)) {
    return;
  }
  const uint64_t now = prv_now_ms();
  if (c->pattern.n > 0) {
    prv_add_seg(c, now - c->edge_ms);  // the pause since the last buzz
  } else {
    c->start_ms = now;
  }
  c->holding = true;
  c->edge_ms = now;
  buzz_stop();
  static const uint32_t hold[] = {TT_PATTERN_MAX_MS};
  vibes_enqueue_custom_pattern((VibePattern){.durations = hold, .num_segments = 1});
  if (!c->tick) {
    c->tick = app_timer_register(100, prv_composer_tick, c);
  }
  layer_mark_dirty(c->layer);
}

static void prv_release(ClickRecognizerRef r, void *context) {
  Composer *c = context;
  if (!c->holding) {
    return;
  }
  vibes_cancel();
  const uint64_t now = prv_now_ms();
  prv_add_seg(c, now - c->edge_ms);
  c->holding = false;
  c->edge_ms = now;
  layer_mark_dirty(c->layer);
}

static void prv_play(ClickRecognizerRef r, void *context) {
  Composer *c = context;
  if (c->pattern.n && !c->holding) {
    buzz_preview_pattern(&c->pattern, REC_PREVIEW_STRENGTH);
  }
}

static void prv_restart(ClickRecognizerRef r, void *context) {
  Composer *c = context;
  if (c->holding) {
    return;
  }
  buzz_stop();
  c->pattern.n = 0;
  vibes_short_pulse();
  layer_mark_dirty(c->layer);
}

static void prv_save(ClickRecognizerRef r, void *context) {
  Composer *c = context;
  if (c->holding) {
    return;
  }
  if (c->pattern.n == 0) {
    ui_toast("Record something first");
    return;
  }
  // Patterns end on a buzz; a trailing pause is never stored.
  if (c->pattern.n % 2 == 0) {
    c->pattern.n--;
  }
  store_pattern_unique_name(c->pattern.name, sizeof(c->pattern.name));
  if (!store_pattern_put(&c->pattern)) {
    ui_toast("No room for more patterns");
    return;
  }
  c->saved = true;
  main_changed();
  char msg[40];
  snprintf(msg, sizeof(msg), "Saved as\n%s", c->pattern.name);
  window_stack_remove(c->window, false);
  ui_toast(msg);
}

static void prv_composer_clicks(void *context) {
  window_raw_click_subscribe(BUTTON_ID_SELECT, prv_press, prv_release, context);
  window_single_click_subscribe(BUTTON_ID_UP, prv_play);
  window_long_click_subscribe(BUTTON_ID_UP, 700, prv_restart, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_save);
}

static void prv_composer_load(Window *window) {
  Composer *c = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  c->layer = layer_create_with_data(layer_get_bounds(root), sizeof(Composer *));
  *(Composer **)layer_get_data(c->layer) = c;
  layer_set_update_proc(c->layer, prv_composer_update);
  layer_add_child(root, c->layer);
}

static void prv_composer_unload(Window *window) {
  Composer *c = window_get_user_data(window);
  if (c->tick) {
    app_timer_cancel(c->tick);
  }
  vibes_cancel();
  buzz_stop();
  layer_destroy(c->layer);
  window_destroy(window);
  free(c);
}

void composer_show(void) {
  if (store_pattern_count() >= TT_MAX_PATTERNS) {
    ui_toast("No room for more patterns. Delete one first.");
    return;
  }
  Composer *c = malloc(sizeof(Composer));
  if (!c) {
    return;
  }
  memset(c, 0, sizeof(*c));
  c->window = window_create();
  window_set_user_data(c->window, c);
  window_set_background_color(c->window, GColorWhite);
  window_set_click_config_provider_with_context(c->window, prv_composer_clicks, c);
  window_set_window_handlers(c->window, (WindowHandlers){
                                            .load = prv_composer_load,
                                            .unload = prv_composer_unload,
                                        });
  window_stack_push(c->window, true);
}

// ---------------------------------------------------------------- list

enum { SEC_PATTERNS, SEC_SOUNDS };

static ListWindow *s_patterns;
static char s_target[TT_NAME_MAX];  // the pattern or tune an action menu is about
static bool s_target_is_tune;

static uint16_t prv_sections(void *ctx) {
  return buzz_has_speaker() ? 2 : 1;
}

static void prv_header(void *ctx, uint16_t section, char *buf, size_t cap) {
  tt_copy(buf, cap, section == SEC_PATTERNS ? "Buzz patterns" : "Sounds");
}

static uint16_t prv_rows(void *ctx, uint16_t section) {
  if (section == SEC_PATTERNS) {
    return (uint16_t)(store_pattern_count() + 1);
  }
  return (uint16_t)store_tune_count();
}

static void prv_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  const TTSettings *s = store_settings();
  if (section == SEC_PATTERNS) {
    if (row == 0) {
      tt_copy(out->title, sizeof(out->title), "Record new");
      tt_copy(out->right, sizeof(out->right), "+");
      return;
    }
    const TTPattern *p = store_pattern(row - 1);
    if (!p) {
      return;
    }
    tt_copy(out->title, sizeof(out->title), p->name);
    const uint32_t total = tt_pattern_total_ms(p);
    snprintf(out->subtitle, sizeof(out->subtitle), "%d buzz%s, %lu.%lus%s", (p->n + 1) / 2,
             (p->n + 1) / 2 == 1 ? "" : "es", (unsigned long)(total / 1000),
             (unsigned long)((total % 1000) / 100),
             strcmp(p->name, s->vibe) == 0 ? "  default" : "");
    return;
  }
  const TTTune *t = store_tune(row);
  if (!t) {
    return;
  }
  tt_copy(out->title, sizeof(out->title), t->name);
  snprintf(out->subtitle, sizeof(out->subtitle), "%d notes%s", t->n,
           strcmp(t->name, s->sound) == 0 ? "  default" : "");
}

static void prv_action(void *ctx, int index) {
  if (s_target_is_tune) {
    TTTune *t = store_tune_find(s_target);
    if (!t) {
      return;
    }
    if (index == 0) {
      buzz_preview_tune(t);
    } else if (index == 1) {
      tt_copy(store_settings()->sound, sizeof(store_settings()->sound), t->name);
      store_settings_save();
      main_changed();
    } else {
      store_tune_delete(s_target);
      main_changed();
    }
    return;
  }
  TTPattern *p = store_pattern_find(s_target);
  if (!p) {
    return;
  }
  if (index == 0) {
    buzz_preview_pattern(p, 0);
  } else if (index == 1) {
    buzz_preview_pattern(p, 40);
  } else if (index == 2) {
    tt_copy(store_settings()->vibe, sizeof(store_settings()->vibe), p->name);
    store_settings_save();
    main_changed();
  } else if (store_pattern_count() > 1) {
    store_pattern_delete(s_target);
    main_changed();
  } else {
    ui_toast("Keep at least one pattern");
  }
}

static void prv_select(void *ctx, uint16_t section, uint16_t row) {
  if (section == SEC_PATTERNS) {
    if (row == 0) {
      composer_show();
      return;
    }
    const TTPattern *p = store_pattern(row - 1);
    if (!p) {
      return;
    }
    tt_copy(s_target, sizeof(s_target), p->name);
    s_target_is_tune = false;
    static const char *items[] = {"Play", "Play softly (40%)", "Make default", "Delete"};
    buzz_preview_pattern(p, 0);
    pick_choice(p->name, items, 4, 0, prv_action, NULL, NULL);
    return;
  }
  const TTTune *t = store_tune(row);
  if (!t) {
    return;
  }
  tt_copy(s_target, sizeof(s_target), t->name);
  s_target_is_tune = true;
  static const char *items[] = {"Play", "Make default", "Delete"};
  buzz_preview_tune(t);
  pick_choice(t->name, items, 3, 0, prv_action, NULL, NULL);
}

static void prv_destroy(void *ctx) {
  s_patterns = NULL;
  buzz_stop();
}

void patterns_show(void) {
  if (s_patterns) {
    return;
  }
  static const ListCallbacks callbacks = {
      .num_sections = prv_sections,
      .header = prv_header,
      .num_rows = prv_rows,
      .row = prv_row,
      .select = prv_select,
      .destroy = prv_destroy,
  };
  s_patterns = list_window_push("Buzz & sound", &callbacks, NULL);
}

void tunes_show(void) {
  patterns_show();
  if (s_patterns && buzz_has_speaker()) {
    list_window_select(s_patterns, SEC_SOUNDS, 0);
  }
}
