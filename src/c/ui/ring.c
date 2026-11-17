#include "ring.h"

#include "../buzz.h"
#include "../main.h"
#include "../sched.h"
#include "../store.h"
#include "pickers.h"

#define STRIP_W 56  // the dark strip beside the buttons

typedef struct {
  Window *window;
  Layer *layer;
  uint32_t alarm_id;
  bool preview;
  bool resolved;  // an action was taken; nothing left to do on unload
  bool snoozed;   // this ring is a snooze coming back
  time_t when;    // the time it was due
  AppTimer *timeout;
  char msg[TT_MSG_MAX];
  char snooze_a[10];
  char snooze_b[10];
  uint32_t color;
  TTAlarm preview_alarm;  // a private copy when previewing something unsaved
} Ring;

static Ring *s_ring;

bool ring_showing(void) {
  return s_ring != NULL;
}

static TTAlarm *prv_alarm(Ring *ring) {
  return ring->preview ? &ring->preview_alarm : store_alarm_by_id(ring->alarm_id);
}

//! Snooze lengths as they fit beside a button: "3m", "2h", "1h30", "5d".
static void prv_short_duration(uint32_t minutes, char *buf, size_t cap) {
  if (minutes < 60) {
    snprintf(buf, cap, "%um", (unsigned)minutes);
  } else if (minutes < 48 * 60 || minutes % 1440) {
    if (minutes % 60 && minutes < 600) {
      snprintf(buf, cap, "%uh%02u", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
    } else {
      snprintf(buf, cap, "%uh", (unsigned)((minutes + 30) / 60));
    }
  } else {
    snprintf(buf, cap, "%ud", (unsigned)(minutes / 1440));
  }
}

static void prv_stop_noise(Ring *ring) {
  buzz_stop();
  if (ring->timeout) {
    app_timer_cancel(ring->timeout);
    ring->timeout = NULL;
  }
  if (store_settings()->light) {
    light_enable(false);
  }
}

//! Leave the ring screen, saying what happened unless another alarm is waiting.
static void prv_leave(Ring *ring, const char *toast) {
  ring->resolved = true;
  prv_stop_noise(ring);
  Window *window = ring->window;
  TTAlarm *next = ring->preview ? NULL : sched_due(time(NULL));
  if (next) {
    // Straight on to the next one; the old window goes once the new one is up.
    s_ring = NULL;
    ring_show(next, false);
  } else if (toast) {
    ui_toast(toast);
  }
  window_stack_remove(window, false);
  main_changed();
}

static void prv_do_snooze(Ring *ring, uint32_t minutes, bool automatic) {
  char toast[64];
  char when[32];
  if (ring->preview) {
    prv_leave(ring, "Preview over");
    return;
  }
  TTAlarm *a = prv_alarm(ring);
  if (!a) {
    prv_leave(ring, NULL);
    return;
  }
  if (!automatic) {
    a->auto_snoozes = 0;
  }
  sched_snooze(a, minutes);
  ui_format_when(a->snooze_until, when, sizeof(when));
  snprintf(toast, sizeof(toast), "Snoozed until\n%s", when);
  prv_leave(ring, toast);
}

static void prv_do_done(Ring *ring) {
  if (ring->preview) {
    prv_leave(ring, "Preview over");
    return;
  }
  TTAlarm *a = prv_alarm(ring);
  const char *toast = "Done";
  if (a) {
    switch (sched_done(a)) {
      case SCHED_DONE_ARCHIVED:
        toast = "Done\nArchived";
        break;
      case SCHED_DONE_DELETED:
        toast = "Done\nDeleted";
        break;
      case SCHED_DONE_OFF:
        toast = "Done\nSwitched off";
        break;
      default:
        break;
    }
  }
  prv_leave(ring, toast);
}

static void prv_timeout(void *data) {
  Ring *ring = data;
  ring->timeout = NULL;
  if (ring->preview) {
    prv_leave(ring, "Preview over");
    return;
  }
  TTAlarm *a = prv_alarm(ring);
  if (!a) {
    prv_leave(ring, NULL);
    return;
  }
  const uint8_t max = store_settings()->auto_snooze_max;
  a->auto_snoozes++;
  if (max && a->auto_snoozes >= max) {
    sched_give_up(a);
    prv_leave(ring, "Missed");
  } else {
    prv_do_snooze(ring, sched_snooze_minutes(a, 0), true);
  }
}

// ---------------------------------------------------------------- drawing

static void prv_check_mark(GContext *g, int cx, int cy) {
  graphics_context_set_stroke_width(g, 4);
  graphics_draw_line(g, GPoint(cx - 10, cy), GPoint(cx - 3, cy + 7));
  graphics_draw_line(g, GPoint(cx - 3, cy + 7), GPoint(cx + 11, cy - 8));
  graphics_context_set_stroke_width(g, 1);
}

static void prv_update(Layer *layer, GContext *g) {
  Ring *ring = *(Ring **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);
  const GColor bg = ui_color_from_tag(ring->color);
  const GColor fg = PBL_IF_COLOR_ELSE(gcolor_legible_over(bg), GColorWhite);
  const int content_w = b.size.w - STRIP_W;

  graphics_context_set_fill_color(g, bg);
  graphics_fill_rect(g, b, 0, GCornerNone);

  // Heading: what this is and, if it is late, by how much.
  char heading[32];
  const time_t now = time(NULL);
  if (ring->preview) {
    snprintf(heading, sizeof(heading), "PREVIEW");
  } else if (now - ring->when > 120) {
    char ago[20];
    tt_format_relative((int32_t)(ring->when - now), ago, sizeof(ago));
    snprintf(heading, sizeof(heading), "DUE %s", ago);
  } else {
    snprintf(heading, sizeof(heading), ring->snoozed ? "SNOOZED" : "TIME TO");
  }
  graphics_context_set_text_color(g, fg);
  graphics_draw_text(g, heading, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(8, 2, content_w - 12, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  // The time it was due.
  char clock[12];
  int minute;
  tt_epoch_to_local(ring->when, NULL, &minute, NULL);
  tt_format_clock(minute, true, clock, sizeof(clock));
  const bool h24 = clock_is_24h_style();
  if (!h24) {
    int h = (minute / 60) % 12;
    snprintf(clock, sizeof(clock), "%d:%02d", h ? h : 12, minute % 60);
  }
  graphics_draw_text(g, clock, fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS),
                     GRect(6, 20, content_w - 8, 44), GTextOverflowModeFill, GTextAlignmentLeft,
                     NULL);
  if (!h24) {
    graphics_draw_text(g, minute < 720 ? "AM" : "PM",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(content_w - 30, 40, 28, 22), GTextOverflowModeFill,
                       GTextAlignmentRight, NULL);
  }

  // The message, as large as the space allows.
  const char *msg = ring->msg[0] ? ring->msg : "Alarm";
  GRect box = GRect(8, 68, content_w - 12, b.size.h - 72);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GSize size = graphics_text_layout_get_content_size(msg, font, GRect(0, 0, box.size.w, 400),
                                                     GTextOverflowModeWordWrap,
                                                     GTextAlignmentLeft);
  if (size.h > box.size.h) {
    font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  }
  graphics_draw_text(g, msg, font, box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);

  // Button strip: what Up, Select and Down do.
  GRect strip = GRect(b.size.w - STRIP_W, 0, STRIP_W, b.size.h);
  graphics_context_set_fill_color(g, GColorBlack);
  graphics_fill_rect(g, strip, 0, GCornerNone);
  graphics_context_set_text_color(g, GColorWhite);
  graphics_context_set_stroke_color(g, GColorWhite);
  GFont label = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont value = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  const int cell = b.size.h / 3;
  for (int i = 0; i < 3; i++) {
    const int y = i * cell;
    if (i == 1) {
      prv_check_mark(g, strip.origin.x + STRIP_W / 2, y + cell / 2 - 8);
      graphics_draw_text(g, "DONE", label, GRect(strip.origin.x, y + cell / 2 + 6, STRIP_W, 18),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    } else {
      graphics_draw_text(g, "SNOOZE", label, GRect(strip.origin.x, y + cell / 2 - 26, STRIP_W, 18),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
      graphics_draw_text(g, i == 0 ? ring->snooze_a : ring->snooze_b, value,
                         GRect(strip.origin.x, y + cell / 2 - 14, STRIP_W, 30),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
    if (i) {
      graphics_context_set_stroke_color(g, GColorDarkGray);
      graphics_draw_line(g, GPoint(strip.origin.x + 6, y), GPoint(b.size.w - 6, y));
      graphics_context_set_stroke_color(g, GColorWhite);
    }
  }
}

// ---------------------------------------------------------------- buttons

static void prv_snooze_a(ClickRecognizerRef recognizer, void *context) {
  Ring *ring = context;
  TTAlarm *a = prv_alarm(ring);
  prv_do_snooze(ring, a ? sched_snooze_minutes(a, 0) : 5, false);
}

static void prv_snooze_b(ClickRecognizerRef recognizer, void *context) {
  Ring *ring = context;
  TTAlarm *a = prv_alarm(ring);
  prv_do_snooze(ring, a ? sched_snooze_minutes(a, 1) : 30, false);
}

static void prv_done(ClickRecognizerRef recognizer, void *context) {
  prv_do_done(context);
}

static void prv_custom_picked(void *ctx, uint32_t minutes) {
  // The ring may have been resolved while the picker was open.
  if (s_ring && s_ring == ctx) {
    prv_do_snooze(s_ring, minutes, false);
  }
}

static void prv_custom(ClickRecognizerRef recognizer, void *context) {
  Ring *ring = context;
  TTAlarm *a = prv_alarm(ring);
  if (!a) {
    return;
  }
  const bool top = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP;
  // They are looking at it now: stop the noise while they choose.
  buzz_stop();
  pick_duration("Snooze for", sched_snooze_minutes(a, top ? 0 : 1), prv_custom_picked, ring);
}

static void prv_clicks(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_snooze_a);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_snooze_b);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_done);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_snooze_a);
  window_long_click_subscribe(BUTTON_ID_UP, 600, prv_custom, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 600, prv_custom, NULL);
}

// ---------------------------------------------------------------- window

static void prv_load(Window *window) {
  Ring *ring = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  ring->layer = layer_create_with_data(layer_get_bounds(root), sizeof(Ring *));
  *(Ring **)layer_get_data(ring->layer) = ring;
  layer_set_update_proc(ring->layer, prv_update);
  layer_add_child(root, ring->layer);
}

static void prv_appear(Window *window) {
  Ring *ring = window_get_user_data(window);
  // (Re)start the clock on an unanswered ring, including after a picker closes.
  if (ring->timeout) {
    app_timer_cancel(ring->timeout);
  }
  ring->timeout = app_timer_register((uint32_t)store_settings()->ring_secs * 1000, prv_timeout,
                                     ring);
}

static void prv_unload(Window *window) {
  Ring *ring = window_get_user_data(window);
  if (!ring->resolved) {
    // Closed some other way (the app is being shut): treat it as a snooze so
    // the alarm comes back instead of silently vanishing.
    prv_stop_noise(ring);
    TTAlarm *a = ring->preview ? NULL : prv_alarm(ring);
    if (a && a->next && a->next <= time(NULL)) {
      sched_snooze(a, sched_snooze_minutes(a, 0));
    }
  }
  if (s_ring == ring) {
    s_ring = NULL;
  }
  layer_destroy(ring->layer);
  window_destroy(window);
  free(ring);
}

void ring_show(TTAlarm *alarm, bool preview) {
  if (s_ring || !alarm) {
    return;
  }
  Ring *ring = malloc(sizeof(Ring));
  if (!ring) {
    return;
  }
  memset(ring, 0, sizeof(*ring));
  ring->preview = preview;
  ring->alarm_id = alarm->id;
  if (preview) {
    ring->preview_alarm = *alarm;
  }
  ring->when = (!preview && alarm->next) ? alarm->next : time(NULL);
  ring->snoozed = !preview && alarm->snooze_until != 0;
  ring->color = alarm->color;
  tt_copy(ring->msg, sizeof(ring->msg), alarm->msg);
  prv_short_duration(sched_snooze_minutes(alarm, 0), ring->snooze_a, sizeof(ring->snooze_a));
  prv_short_duration(sched_snooze_minutes(alarm, 1), ring->snooze_b, sizeof(ring->snooze_b));

  ring->window = window_create();
  window_set_user_data(ring->window, ring);
  window_set_click_config_provider_with_context(ring->window, prv_clicks, ring);
  window_set_window_handlers(ring->window, (WindowHandlers){
                                               .load = prv_load,
                                               .appear = prv_appear,
                                               .unload = prv_unload,
                                           });
  s_ring = ring;
  window_stack_push(ring->window, true);
  if (store_settings()->light) {
    light_enable(true);
  }
  buzz_start(alarm);
}
