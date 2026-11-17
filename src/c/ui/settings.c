// General settings: the defaults every alarm falls back on.
#include "../buzz.h"
#include "../comm.h"
#include "../main.h"
#include "../store.h"
#include "pickers.h"
#include "screens.h"

enum {
  S_SNOOZE_A,
  S_SNOOZE_B,
  S_BUZZ,
  S_STRENGTH,
  S_SOUND,
  S_VOLUME,
  S_RING,
  S_GIVE_UP,
  S_DONE,
  S_LIGHT,
  S_TIMELINE,
  S_SYNC,
  S_ABOUT,
  S_RESET,
  S_COUNT
};

static ListWindow *s_settings;
static uint8_t s_rows[S_COUNT];
static int s_count;
static int s_reset_step;

static const uint16_t s_ring_secs[] = {15, 30, 60, 120, 300, 600};
static const uint8_t s_give_up[] = {0, 1, 2, 3, 5, 10, 20};
static const uint8_t s_strengths[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

static void prv_build(void) {
  s_count = 0;
  for (int i = 0; i < S_COUNT; i++) {
    if ((i == S_SOUND || i == S_VOLUME) && !buzz_has_speaker()) {
      continue;
    }
    s_rows[s_count++] = (uint8_t)i;
  }
}

static void prv_saved(void) {
  store_settings_save();
  main_changed();
  list_window_reload(s_settings);
}

static uint16_t prv_rows(void *ctx, uint16_t section) {
  return (uint16_t)s_count;
}

static void prv_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  const TTSettings *s = store_settings();
  if (row >= s_count) {
    return;
  }
  switch (s_rows[row]) {
    case S_SNOOZE_A:
      tt_copy(out->title, sizeof(out->title), "Snooze (up)");
      tt_format_duration(s->snooze_a, out->right, sizeof(out->right));
      break;
    case S_SNOOZE_B:
      tt_copy(out->title, sizeof(out->title), "Snooze (down)");
      tt_format_duration(s->snooze_b, out->right, sizeof(out->right));
      break;
    case S_BUZZ:
      tt_copy(out->title, sizeof(out->title), "Buzz");
      tt_copy(out->subtitle, sizeof(out->subtitle), store_pattern_resolve(NULL)->name);
      break;
    case S_STRENGTH:
      tt_copy(out->title, sizeof(out->title), "Strength");
      snprintf(out->right, sizeof(out->right), "%d%%", s->strength);
      break;
    case S_SOUND:
      tt_copy(out->title, sizeof(out->title), "Sound");
      tt_copy(out->subtitle, sizeof(out->subtitle),
              strcmp(s->sound, "none") == 0 || !s->sound[0] ? "Silent" : s->sound);
      break;
    case S_VOLUME:
      tt_copy(out->title, sizeof(out->title), "Volume");
      snprintf(out->right, sizeof(out->right), "%d%%", s->volume);
      break;
    case S_RING:
      tt_copy(out->title, sizeof(out->title), "Ring for");
      if (s->ring_secs < 60) {
        snprintf(out->right, sizeof(out->right), "%ds", s->ring_secs);
      } else {
        tt_format_duration(s->ring_secs / 60, out->right, sizeof(out->right));
      }
      tt_copy(out->subtitle, sizeof(out->subtitle), "then snoozes by itself");
      break;
    case S_GIVE_UP:
      tt_copy(out->title, sizeof(out->title), "Give up after");
      if (s->auto_snooze_max) {
        snprintf(out->subtitle, sizeof(out->subtitle), "%d unanswered ring%s",
                 s->auto_snooze_max, s->auto_snooze_max == 1 ? "" : "s");
      } else {
        tt_copy(out->subtitle, sizeof(out->subtitle), "Never: keep ringing");
      }
      break;
    case S_DONE:
      tt_copy(out->title, sizeof(out->title), "Finished one-offs");
      tt_copy(out->subtitle, sizeof(out->subtitle),
              s->done_action == TT_DONE_ARCHIVE  ? "Move to archive"
              : s->done_action == TT_DONE_DELETE ? "Delete"
                                                 : "Switch off");
      break;
    case S_LIGHT:
      tt_copy(out->title, sizeof(out->title), "Light while ringing");
      out->flags |= s->light ? LIST_CHECKED : LIST_UNCHECKED;
      break;
    case S_TIMELINE:
      tt_copy(out->title, sizeof(out->title), "Timeline pins");
      tt_copy(out->subtitle, sizeof(out->subtitle), "Needs the phone");
      out->flags |= s->timeline ? LIST_CHECKED : LIST_UNCHECKED;
      break;
    case S_SYNC:
      tt_copy(out->title, sizeof(out->title), "Sync with phone");
      tt_copy(out->subtitle, sizeof(out->subtitle),
              comm_phone_seen() ? "Phone connected" : "Send everything now");
      break;
    case S_ABOUT:
      tt_copy(out->title, sizeof(out->title), "Storage");
      snprintf(out->subtitle, sizeof(out->subtitle), "%d/%d alarms, %d/%d archived",
               store_alarm_count(), TT_MAX_ALARMS, store_archive_count(), TT_MAX_ARCHIVE);
      break;
    case S_RESET:
      tt_copy(out->title, sizeof(out->title),
              s_reset_step == 0   ? "Erase everything"
              : s_reset_step == 1 ? "Erase all alarms?"
                                  : "Really? No undo");
      out->flags |= LIST_DANGER;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- choices

static int s_which;

static void prv_snooze_custom(void *ctx, uint32_t minutes) {
  TTSettings *s = store_settings();
  if (s_which == 0) {
    s->snooze_a = minutes;
  } else {
    s->snooze_b = minutes;
  }
  prv_saved();
}

static void prv_snooze_picked(void *ctx, int index) {
  // Entry 0 of snooze_presets means "default", which makes no sense here.
  if (index + 1 >= SNOOZE_PRESET_COUNT) {
    const TTSettings *s = store_settings();
    pick_duration("Snooze for", s_which == 0 ? s->snooze_a : s->snooze_b, prv_snooze_custom,
                  NULL);
    return;
  }
  prv_snooze_custom(NULL, snooze_presets[index + 1]);
}

static void prv_pick_snooze(int which) {
  static char labels[SNOOZE_PRESET_COUNT][16];
  static const char *items[SNOOZE_PRESET_COUNT];
  const TTSettings *s = store_settings();
  const uint32_t current = which == 0 ? s->snooze_a : s->snooze_b;
  int selected = SNOOZE_PRESET_COUNT - 1;
  for (int i = 1; i < SNOOZE_PRESET_COUNT; i++) {
    tt_format_duration(snooze_presets[i], labels[i - 1], sizeof(labels[0]));
    items[i - 1] = labels[i - 1];
    if (snooze_presets[i] == current) {
      selected = i - 1;
    }
  }
  tt_copy(labels[SNOOZE_PRESET_COUNT - 1], sizeof(labels[0]), "Custom...");
  items[SNOOZE_PRESET_COUNT - 1] = labels[SNOOZE_PRESET_COUNT - 1];
  s_which = which;
  pick_choice(which == 0 ? "Up snoozes for" : "Down snoozes for", items, SNOOZE_PRESET_COUNT,
              selected, prv_snooze_picked, NULL, NULL);
}

static void prv_buzz_highlight(void *ctx, int index) {
  if (store_pattern(index)) {
    buzz_preview_pattern(store_pattern(index), store_settings()->strength);
  }
}

static void prv_buzz_picked(void *ctx, int index) {
  buzz_stop();
  if (store_pattern(index)) {
    tt_copy(store_settings()->vibe, TT_NAME_MAX, store_pattern(index)->name);
    prv_saved();
  }
}

static void prv_strength_highlight(void *ctx, int index) {
  buzz_preview_pattern(store_pattern_resolve(NULL), s_strengths[index]);
}

static void prv_strength_picked(void *ctx, int index) {
  buzz_stop();
  store_settings()->strength = s_strengths[index];
  prv_saved();
}

static void prv_sound_highlight(void *ctx, int index) {
  if (index > 0 && store_tune(index - 1)) {
    buzz_preview_tune(store_tune(index - 1));
  } else {
    buzz_stop();
  }
}

static void prv_sound_picked(void *ctx, int index) {
  buzz_stop();
  TTSettings *s = store_settings();
  if (index == 0) {
    tt_copy(s->sound, sizeof(s->sound), "none");
  } else if (store_tune(index - 1)) {
    tt_copy(s->sound, sizeof(s->sound), store_tune(index - 1)->name);
  }
  prv_saved();
}

static void prv_volume_picked(void *ctx, int32_t value) {
  store_settings()->volume = (uint8_t)value;
  prv_saved();
  if (store_tune_resolve(NULL)) {
    buzz_preview_tune(store_tune_resolve(NULL));
  }
}

static void prv_ring_picked(void *ctx, int index) {
  store_settings()->ring_secs = s_ring_secs[index];
  prv_saved();
}

static void prv_give_up_picked(void *ctx, int index) {
  store_settings()->auto_snooze_max = s_give_up[index];
  prv_saved();
}

static void prv_done_picked(void *ctx, int index) {
  static const uint8_t actions[] = {TT_DONE_ARCHIVE, TT_DONE_KEEP, TT_DONE_DELETE};
  store_settings()->done_action = actions[index % 3];
  prv_saved();
}

static void prv_select(void *ctx, uint16_t section, uint16_t row) {
  TTSettings *s = store_settings();
  if (row >= s_count) {
    return;
  }
  const int which = s_rows[row];
  if (which != S_RESET) {
    s_reset_step = 0;
  }
  switch (which) {
    case S_SNOOZE_A:
      prv_pick_snooze(0);
      break;
    case S_SNOOZE_B:
      prv_pick_snooze(1);
      break;
    case S_BUZZ: {
      static const char *items[TT_MAX_PATTERNS];
      int selected = 0;
      for (int i = 0; i < store_pattern_count(); i++) {
        items[i] = store_pattern(i)->name;
        if (strcmp(items[i], s->vibe) == 0) {
          selected = i;
        }
      }
      pick_choice("Default buzz", items, store_pattern_count(), selected, prv_buzz_picked,
                  prv_buzz_highlight, NULL);
      break;
    }
    case S_STRENGTH: {
      static char labels[ARRAY_LENGTH(s_strengths)][8];
      static const char *items[ARRAY_LENGTH(s_strengths)];
      int selected = (int)ARRAY_LENGTH(s_strengths) - 1;
      for (size_t i = 0; i < ARRAY_LENGTH(s_strengths); i++) {
        snprintf(labels[i], sizeof(labels[i]), "%d%%", s_strengths[i]);
        items[i] = labels[i];
        if (s_strengths[i] == s->strength) {
          selected = (int)i;
        }
      }
      pick_choice("Default strength", items, ARRAY_LENGTH(s_strengths), selected,
                  prv_strength_picked, prv_strength_highlight, NULL);
      break;
    }
    case S_SOUND: {
      static const char *items[TT_MAX_TUNES + 1];
      items[0] = "Silent";
      int selected = 0;
      for (int i = 0; i < store_tune_count(); i++) {
        items[i + 1] = store_tune(i)->name;
        if (strcmp(items[i + 1], s->sound) == 0) {
          selected = i + 1;
        }
      }
      pick_choice("Default sound", items, store_tune_count() + 1, selected, prv_sound_picked,
                  prv_sound_highlight, NULL);
      break;
    }
    case S_VOLUME:
      pick_number("Volume", s->volume, 0, 100, "%", prv_volume_picked, NULL);
      break;
    case S_RING: {
      static const char *items[] = {"15 seconds", "30 seconds", "1 minute",
                                    "2 minutes",  "5 minutes",  "10 minutes"};
      int selected = 2;
      for (size_t i = 0; i < ARRAY_LENGTH(s_ring_secs); i++) {
        if (s_ring_secs[i] == s->ring_secs) {
          selected = (int)i;
        }
      }
      pick_choice("Ring for", items, ARRAY_LENGTH(s_ring_secs), selected, prv_ring_picked, NULL,
                  NULL);
      break;
    }
    case S_GIVE_UP: {
      static const char *items[] = {"Never",    "1 ring",   "2 rings", "3 rings",
                                    "5 rings",  "10 rings", "20 rings"};
      int selected = 0;
      for (size_t i = 0; i < ARRAY_LENGTH(s_give_up); i++) {
        if (s_give_up[i] == s->auto_snooze_max) {
          selected = (int)i;
        }
      }
      pick_choice("Give up after", items, ARRAY_LENGTH(s_give_up), selected, prv_give_up_picked,
                  NULL, NULL);
      break;
    }
    case S_DONE: {
      static const char *items[] = {"Move to archive", "Switch off", "Delete"};
      int selected = s->done_action == TT_DONE_ARCHIVE ? 0 : s->done_action == TT_DONE_KEEP ? 1 : 2;
      pick_choice("Finished one-offs", items, 3, selected, prv_done_picked, NULL, NULL);
      break;
    }
    case S_LIGHT:
      s->light = !s->light;
      prv_saved();
      break;
    case S_TIMELINE:
      s->timeline = !s->timeline;
      prv_saved();
      break;
    case S_SYNC:
      comm_sync_now();
      ui_toast("Sending to phone");
      break;
    case S_RESET:
      if (++s_reset_step >= 3) {
        s_reset_step = 0;
        store_reset();
        main_changed();
        ui_toast("Erased");
      }
      list_window_reload(s_settings);
      break;
    default:
      break;
  }
}

static void prv_destroy(void *ctx) {
  s_settings = NULL;
  s_reset_step = 0;
}

void settings_show(void) {
  if (s_settings) {
    return;
  }
  prv_build();
  static const ListCallbacks callbacks = {
      .num_rows = prv_rows,
      .row = prv_row,
      .select = prv_select,
      .destroy = prv_destroy,
  };
  s_settings = list_window_push("Settings", &callbacks, NULL);
}
