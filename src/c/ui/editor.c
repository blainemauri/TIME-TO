// Creating and editing an alarm.
//
// Edits are made to a private draft. Leaving with Back saves the draft
// (except a new alarm nobody touched); "Discard changes" throws it away.
#include "../buzz.h"
#include "../main.h"
#include "../sched.h"
#include "../store.h"
#include "pickers.h"
#include "ring.h"
#include "screens.h"

enum {
  F_TIME,
  F_REPEAT,
  F_MESSAGE,
  F_SNOOZE_A,
  F_SNOOZE_B,
  F_BUZZ,
  F_STRENGTH,
  F_RAMP,
  F_SOUND,
  F_EVERY,
  F_EVERY_UNTIL,
  F_END,
  F_COLOR,
  F_FINISHED,
  F_ON,
  F_SAVE,
  F_PREVIEW,
  F_DISCARD,
  F_DELETE,
  F_COUNT
};

typedef struct {
  ListWindow *lw;
  TTAlarm draft;
  uint32_t id;       // 0 while it is a new alarm
  bool touched;      // anything changed at all
  bool discard;      // leave without saving
  bool auto_date;    // a new one-off whose date follows the time
  bool confirm_delete;
  uint8_t fields[F_COUNT];
  int count;
} Editor;

// ---------------------------------------------------------------- choices

typedef struct {
  const char *name;
  uint32_t rgb;
} NamedColor;

static const NamedColor s_colors[] = {
    {"None", 0},          {"Red", 0xff0000},    {"Orange", 0xff5500}, {"Yellow", 0xffff00},
    {"Green", 0x00aa00},  {"Mint", 0x55ffaa},   {"Teal", 0x00aaaa},   {"Blue", 0x0055ff},
    {"Purple", 0xaa00ff}, {"Pink", 0xff55aa},   {"Grey", 0xaaaaaa},
};

static const uint16_t s_every_presets[] = {0, 5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 360};
static const uint8_t s_strengths[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

static void prv_color_name(uint32_t tag, char *buf, size_t cap) {
  for (size_t i = 1; i < ARRAY_LENGTH(s_colors); i++) {
    if ((0x1000000u | s_colors[i].rgb) == tag) {
      tt_copy(buf, cap, s_colors[i].name);
      return;
    }
  }
  if (tag) {
    snprintf(buf, cap, "#%06lx", (unsigned long)(tag & 0xFFFFFF));
  } else {
    tt_copy(buf, cap, "None");
  }
}

static void prv_snooze_text(uint32_t minutes, int which, char *buf, size_t cap) {
  char d[16];
  if (minutes == 0) {
    const TTSettings *s = store_settings();
    tt_format_duration(which == 0 ? s->snooze_a : s->snooze_b, d, sizeof(d));
    snprintf(buf, cap, "Default (%s)", d);
  } else {
    tt_format_duration(minutes, buf, cap);
  }
}

static const char *prv_done_name(uint8_t flags) {
  if (flags & TT_F_DONE_KEEP) {
    return "Switch off";
  }
  if (flags & TT_F_DONE_ARCHIVE) {
    return "Archive";
  }
  if (flags & TT_F_DONE_DELETE) {
    return "Delete";
  }
  return "Default";
}

// ---------------------------------------------------------------- fields

static void prv_build(Editor *e) {
  e->count = 0;
  const TTAlarm *a = &e->draft;
  for (int f = 0; f < F_COUNT; f++) {
    if (f == F_SOUND && !buzz_has_speaker()) {
      continue;
    }
    if (f == F_EVERY_UNTIL && !a->every) {
      continue;
    }
    if (f == F_END && a->rule.kind == TT_RULE_ONCE) {
      continue;
    }
    if ((f == F_DELETE || f == F_ON) && e->id == 0) {
      continue;
    }
    e->fields[e->count++] = (uint8_t)f;
  }
}

static void prv_changed(Editor *e) {
  e->touched = true;
  e->confirm_delete = false;
  prv_build(e);
  list_window_reload(e->lw);
}

//! A new one-off follows its time: today if that is still to come, else tomorrow.
static void prv_follow_time(Editor *e) {
  if (!e->auto_date || e->draft.rule.kind != TT_RULE_ONCE) {
    return;
  }
  int32_t today;
  int minute;
  tt_epoch_to_local(time(NULL), &today, &minute, NULL);
  e->draft.rule.date = tt_civil_from_days(e->draft.minute > minute ? today : today + 1);
}

static uint16_t prv_rows(void *ctx, uint16_t section) {
  return (uint16_t)((Editor *)ctx)->count;
}

static void prv_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  Editor *e = ctx;
  const TTAlarm *a = &e->draft;
  if (row >= e->count) {
    return;
  }
  char buf[40];
  switch (e->fields[row]) {
    case F_TIME:
      tt_copy(out->title, sizeof(out->title), "Time");
      ui_format_clock(a->minute, out->right, sizeof(out->right));
      if (a->on && tt_alarm_next_occurrence(a, time(NULL))) {
        ui_format_when(tt_alarm_next_occurrence(a, time(NULL)), buf, sizeof(buf));
        snprintf(out->subtitle, sizeof(out->subtitle), "Next: %s", buf);
      }
      break;
    case F_REPEAT:
      tt_copy(out->title, sizeof(out->title), "When");
      if (a->rule.kind == TT_RULE_ONCE && tt_date_is_set(a->rule.date)) {
        ui_format_day(tt_local_to_epoch(tt_date_to_days(a->rule.date), 720), buf, sizeof(buf));
        snprintf(out->subtitle, sizeof(out->subtitle), "Once, %s", buf);
      } else {
        tt_rule_describe(&a->rule, out->subtitle, sizeof(out->subtitle));
      }
      break;
    case F_MESSAGE:
      tt_copy(out->title, sizeof(out->title), "Message");
      tt_copy(out->subtitle, sizeof(out->subtitle), a->msg[0] ? a->msg : "(none)");
      break;
    case F_SNOOZE_A:
      tt_copy(out->title, sizeof(out->title), "Snooze (up)");
      prv_snooze_text(a->snooze_a, 0, out->subtitle, sizeof(out->subtitle));
      break;
    case F_SNOOZE_B:
      tt_copy(out->title, sizeof(out->title), "Snooze (down)");
      prv_snooze_text(a->snooze_b, 1, out->subtitle, sizeof(out->subtitle));
      break;
    case F_BUZZ:
      tt_copy(out->title, sizeof(out->title), "Buzz");
      if (a->vibe[0]) {
        tt_copy(out->subtitle, sizeof(out->subtitle), a->vibe);
      } else {
        snprintf(out->subtitle, sizeof(out->subtitle), "Default (%s)",
                 store_pattern_resolve(NULL)->name);
      }
      break;
    case F_STRENGTH:
      tt_copy(out->title, sizeof(out->title), "Strength");
      if (a->strength) {
        snprintf(out->right, sizeof(out->right), "%d%%", a->strength);
      } else {
        snprintf(out->right, sizeof(out->right), "def %d%%", store_settings()->strength);
      }
      break;
    case F_RAMP:
      tt_copy(out->title, sizeof(out->title), "Build up");
      tt_copy(out->subtitle, sizeof(out->subtitle), "Start soft, get stronger");
      out->flags |= (a->flags & TT_F_RAMP) ? LIST_CHECKED : LIST_UNCHECKED;
      break;
    case F_SOUND:
      tt_copy(out->title, sizeof(out->title), "Sound");
      if (a->sound[0]) {
        tt_copy(out->subtitle, sizeof(out->subtitle),
                strcmp(a->sound, "none") == 0 ? "Silent" : a->sound);
      } else {
        snprintf(out->subtitle, sizeof(out->subtitle), "Default (%s)",
                 strcmp(store_settings()->sound, "none") == 0 ? "silent"
                                                              : store_settings()->sound);
      }
      break;
    case F_EVERY:
      tt_copy(out->title, sizeof(out->title), "Repeat in day");
      if (a->every) {
        tt_format_duration(a->every, buf, sizeof(buf));
        snprintf(out->subtitle, sizeof(out->subtitle), "Every %s", buf);
      } else {
        tt_copy(out->subtitle, sizeof(out->subtitle), "Off");
      }
      break;
    case F_EVERY_UNTIL:
      tt_copy(out->title, sizeof(out->title), "Repeat until");
      ui_format_clock(a->last, out->right, sizeof(out->right));
      break;
    case F_END:
      tt_copy(out->title, sizeof(out->title), "Last day");
      if (tt_date_is_set(a->until)) {
        tt_format_date(a->until, out->subtitle, sizeof(out->subtitle));
      } else {
        tt_copy(out->subtitle, sizeof(out->subtitle), "Forever");
      }
      break;
    case F_COLOR:
      tt_copy(out->title, sizeof(out->title), "Colour");
      prv_color_name(a->color, out->right, sizeof(out->right));
      out->tag = a->color;
      break;
    case F_FINISHED:
      tt_copy(out->title, sizeof(out->title), "When finished");
      tt_copy(out->subtitle, sizeof(out->subtitle), prv_done_name(a->flags));
      break;
    case F_ON:
      tt_copy(out->title, sizeof(out->title), "Switched on");
      out->flags |= a->on ? LIST_CHECKED : LIST_UNCHECKED;
      break;
    case F_SAVE:
      tt_copy(out->title, sizeof(out->title), "Save");
      out->flags |= LIST_CENTER;
      break;
    case F_PREVIEW:
      tt_copy(out->title, sizeof(out->title), "Preview");
      out->flags |= LIST_CENTER;
      break;
    case F_DISCARD:
      tt_copy(out->title, sizeof(out->title), e->id ? "Discard changes" : "Cancel");
      out->flags |= LIST_CENTER;
      break;
    case F_DELETE:
      tt_copy(out->title, sizeof(out->title), e->confirm_delete ? "Really delete?" : "Delete");
      out->flags |= LIST_CENTER | LIST_DANGER;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- pickers

static void prv_time_picked(void *ctx, int minute) {
  Editor *e = ctx;
  e->draft.minute = (uint16_t)minute;
  if (e->draft.every && e->draft.last < minute) {
    e->draft.last = 1439;
  }
  prv_follow_time(e);
  prv_changed(e);
}

static void prv_message_picked(void *ctx, const char *text) {
  Editor *e = ctx;
  tt_copy(e->draft.msg, sizeof(e->draft.msg), text);
  prv_changed(e);
}

static int s_snooze_which;

static void prv_snooze_custom(void *ctx, uint32_t minutes) {
  Editor *e = ctx;
  if (s_snooze_which == 0) {
    e->draft.snooze_a = minutes;
  } else {
    e->draft.snooze_b = minutes;
  }
  prv_changed(e);
}

static void prv_snooze_choice(void *ctx, int index) {
  Editor *e = ctx;
  if (index >= SNOOZE_PRESET_COUNT) {
    uint32_t current = s_snooze_which == 0 ? e->draft.snooze_a : e->draft.snooze_b;
    pick_duration("Snooze for", current ? current : 117 * 60, prv_snooze_custom, e);
    return;
  }
  prv_snooze_custom(e, snooze_presets[index]);
}

static void prv_pick_snooze(Editor *e, int which) {
  static char labels[SNOOZE_PRESET_COUNT + 1][32];
  static const char *items[SNOOZE_PRESET_COUNT + 1];
  const uint32_t current = which == 0 ? e->draft.snooze_a : e->draft.snooze_b;
  int selected = SNOOZE_PRESET_COUNT;
  for (int i = 0; i < SNOOZE_PRESET_COUNT; i++) {
    prv_snooze_text(snooze_presets[i], which, labels[i], sizeof(labels[i]));
    items[i] = labels[i];
    if (snooze_presets[i] == current) {
      selected = i;
    }
  }
  if (selected == SNOOZE_PRESET_COUNT && current) {
    char d[16];
    tt_format_duration(current, d, sizeof(d));
    snprintf(labels[SNOOZE_PRESET_COUNT], sizeof(labels[0]), "Custom (%s)", d);
  } else {
    tt_copy(labels[SNOOZE_PRESET_COUNT], sizeof(labels[0]), "Custom...");
  }
  items[SNOOZE_PRESET_COUNT] = labels[SNOOZE_PRESET_COUNT];
  s_snooze_which = which;
  pick_choice(which == 0 ? "Up snoozes for" : "Down snoozes for", items,
              SNOOZE_PRESET_COUNT + 1, selected, prv_snooze_choice, NULL, e);
}

// Buzz pattern: the list plays each pattern as you scroll past it.
static void prv_buzz_highlight(void *ctx, int index) {
  Editor *e = ctx;
  const TTPattern *p = index == 0 ? store_pattern_resolve(NULL) : store_pattern(index - 1);
  if (p) {
    buzz_preview_pattern(p, e->draft.strength);
  }
}

static void prv_buzz_picked(void *ctx, int index) {
  Editor *e = ctx;
  buzz_stop();
  if (index == 0) {
    e->draft.vibe[0] = '\0';
  } else if (store_pattern(index - 1)) {
    tt_copy(e->draft.vibe, sizeof(e->draft.vibe), store_pattern(index - 1)->name);
  }
  prv_changed(e);
}

static void prv_pick_buzz(Editor *e) {
  static const char *items[TT_MAX_PATTERNS + 1];
  items[0] = "Default";
  int selected = 0;
  const int n = store_pattern_count();
  for (int i = 0; i < n; i++) {
    items[i + 1] = store_pattern(i)->name;
    if (strcmp(store_pattern(i)->name, e->draft.vibe) == 0) {
      selected = i + 1;
    }
  }
  pick_choice("Buzz", items, n + 1, selected, prv_buzz_picked, prv_buzz_highlight, e);
}

static void prv_strength_highlight(void *ctx, int index) {
  Editor *e = ctx;
  buzz_preview_pattern(store_pattern_resolve(e->draft.vibe), s_strengths[index]);
}

static void prv_strength_picked(void *ctx, int index) {
  Editor *e = ctx;
  buzz_stop();
  e->draft.strength = s_strengths[index];
  prv_changed(e);
}

static void prv_pick_strength(Editor *e) {
  static char labels[ARRAY_LENGTH(s_strengths)][20];
  static const char *items[ARRAY_LENGTH(s_strengths)];
  int selected = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(s_strengths); i++) {
    if (s_strengths[i] == 0) {
      snprintf(labels[i], sizeof(labels[i]), "Default (%d%%)", store_settings()->strength);
    } else {
      snprintf(labels[i], sizeof(labels[i]), "%d%%", s_strengths[i]);
    }
    items[i] = labels[i];
    if (s_strengths[i] == e->draft.strength) {
      selected = (int)i;
    }
  }
  pick_choice("Strength", items, ARRAY_LENGTH(s_strengths), selected, prv_strength_picked,
              prv_strength_highlight, e);
}

static void prv_sound_highlight(void *ctx, int index) {
  if (index >= 2 && store_tune(index - 2)) {
    buzz_preview_tune(store_tune(index - 2));
  } else {
    buzz_stop();
  }
}

static void prv_sound_picked(void *ctx, int index) {
  Editor *e = ctx;
  buzz_stop();
  if (index == 0) {
    e->draft.sound[0] = '\0';
  } else if (index == 1) {
    tt_copy(e->draft.sound, sizeof(e->draft.sound), "none");
  } else if (store_tune(index - 2)) {
    tt_copy(e->draft.sound, sizeof(e->draft.sound), store_tune(index - 2)->name);
  }
  prv_changed(e);
}

static void prv_pick_sound(Editor *e) {
  static const char *items[TT_MAX_TUNES + 2];
  items[0] = "Default";
  items[1] = "Silent";
  int selected = strcmp(e->draft.sound, "none") == 0 ? 1 : 0;
  const int n = store_tune_count();
  for (int i = 0; i < n; i++) {
    items[i + 2] = store_tune(i)->name;
    if (strcmp(store_tune(i)->name, e->draft.sound) == 0) {
      selected = i + 2;
    }
  }
  pick_choice("Sound", items, n + 2, selected, prv_sound_picked, prv_sound_highlight, e);
}

static void prv_every_custom(void *ctx, int32_t minutes) {
  Editor *e = ctx;
  e->draft.every = (uint16_t)minutes;
  if (e->draft.last <= e->draft.minute) {
    e->draft.last = 1439;
  }
  prv_changed(e);
}

static void prv_every_picked(void *ctx, int index) {
  if (index >= (int)ARRAY_LENGTH(s_every_presets)) {
    Editor *e = ctx;
    pick_number("Every (minutes)", e->draft.every ? e->draft.every : 25, 1, 720, "minutes",
                prv_every_custom, e);
    return;
  }
  prv_every_custom(ctx, s_every_presets[index]);
}

static void prv_pick_every(Editor *e) {
  static char labels[ARRAY_LENGTH(s_every_presets) + 1][20];
  static const char *items[ARRAY_LENGTH(s_every_presets) + 1];
  int selected = (int)ARRAY_LENGTH(s_every_presets);
  for (size_t i = 0; i < ARRAY_LENGTH(s_every_presets); i++) {
    if (s_every_presets[i] == 0) {
      tt_copy(labels[i], sizeof(labels[i]), "Off");
    } else {
      char d[12];
      tt_format_duration(s_every_presets[i], d, sizeof(d));
      snprintf(labels[i], sizeof(labels[i]), "Every %s", d);
    }
    items[i] = labels[i];
    if (s_every_presets[i] == e->draft.every) {
      selected = (int)i;
    }
  }
  tt_copy(labels[ARRAY_LENGTH(s_every_presets)], sizeof(labels[0]), "Custom...");
  items[ARRAY_LENGTH(s_every_presets)] = labels[ARRAY_LENGTH(s_every_presets)];
  pick_choice("Repeat in the day", items, ARRAY_LENGTH(s_every_presets) + 1, selected,
              prv_every_picked, NULL, e);
}

static void prv_every_until_picked(void *ctx, int minute) {
  Editor *e = ctx;
  e->draft.last = (uint16_t)(minute < e->draft.minute ? 1439 : minute);
  prv_changed(e);
}

static void prv_end_date_picked(void *ctx, TTDate date) {
  Editor *e = ctx;
  e->draft.until = date;
  prv_changed(e);
}

static void prv_end_picked(void *ctx, int index) {
  Editor *e = ctx;
  if (index == 0) {
    e->draft.until = TT_DATE_NONE;
    prv_changed(e);
  } else {
    pick_date("Last day", e->draft.until, prv_end_date_picked, e);
  }
}

static void prv_color_picked(void *ctx, int index) {
  Editor *e = ctx;
  e->draft.color = index == 0 ? 0 : (0x1000000u | s_colors[index].rgb);
  prv_changed(e);
}

static void prv_pick_color(Editor *e) {
  static const char *items[ARRAY_LENGTH(s_colors)];
  int selected = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(s_colors); i++) {
    items[i] = s_colors[i].name;
    if (i && e->draft.color == (0x1000000u | s_colors[i].rgb)) {
      selected = (int)i;
    }
  }
  pick_choice("Colour", items, ARRAY_LENGTH(s_colors), selected, prv_color_picked, NULL, e);
}

static void prv_finished_picked(void *ctx, int index) {
  Editor *e = ctx;
  static const uint8_t flags[] = {0, TT_F_DONE_KEEP, TT_F_DONE_ARCHIVE, TT_F_DONE_DELETE};
  e->draft.flags = (uint8_t)((e->draft.flags & ~TT_F_DONE_MASK) | flags[index & 3]);
  prv_changed(e);
}

static void prv_pick_finished(Editor *e) {
  static char first[32];
  const uint8_t action = store_settings()->done_action;
  snprintf(first, sizeof(first), "Default (%s)",
           action == TT_DONE_ARCHIVE ? "archive" : action == TT_DONE_DELETE ? "delete" : "off");
  static const char *items[4];
  items[0] = first;
  items[1] = "Switch off";
  items[2] = "Archive";
  items[3] = "Delete";
  const uint8_t f = e->draft.flags;
  int selected = (f & TT_F_DONE_KEEP) ? 1 : (f & TT_F_DONE_ARCHIVE) ? 2 : (f & TT_F_DONE_DELETE) ? 3 : 0;
  pick_choice("After the last ring", items, 4, selected, prv_finished_picked, NULL, e);
}

// ---------------------------------------------------------------- select

static void prv_select(void *ctx, uint16_t section, uint16_t row) {
  Editor *e = ctx;
  if (row >= e->count) {
    return;
  }
  const uint8_t field = e->fields[row];
  if (field != F_DELETE) {
    e->confirm_delete = false;
  }
  switch (field) {
    case F_TIME:
      pick_time("Time", e->draft.minute, prv_time_picked, e);
      break;
    case F_REPEAT:
      e->touched = true;
      e->auto_date = false;
      repeat_show(&e->draft.rule);
      break;
    case F_MESSAGE:
      pick_text("Message", e->draft.msg, TT_MSG_MAX - 1, prv_message_picked, e);
      break;
    case F_SNOOZE_A:
      prv_pick_snooze(e, 0);
      break;
    case F_SNOOZE_B:
      prv_pick_snooze(e, 1);
      break;
    case F_BUZZ:
      prv_pick_buzz(e);
      break;
    case F_STRENGTH:
      prv_pick_strength(e);
      break;
    case F_RAMP:
      e->draft.flags ^= TT_F_RAMP;
      prv_changed(e);
      break;
    case F_SOUND:
      prv_pick_sound(e);
      break;
    case F_EVERY:
      prv_pick_every(e);
      break;
    case F_EVERY_UNTIL:
      pick_time("Repeat until", e->draft.last >= 1439 ? 1439 : e->draft.last,
                prv_every_until_picked, e);
      break;
    case F_END: {
      static const char *items[] = {"Forever", "Pick a date..."};
      pick_choice("Last day", items, 2, tt_date_is_set(e->draft.until) ? 1 : 0, prv_end_picked,
                  NULL, e);
      break;
    }
    case F_COLOR:
      prv_pick_color(e);
      break;
    case F_FINISHED:
      prv_pick_finished(e);
      break;
    case F_ON:
      e->draft.on = !e->draft.on;
      prv_changed(e);
      break;
    case F_SAVE:
      e->touched = true;  // saving an untouched new alarm is still a save
      list_window_close(e->lw);
      break;
    case F_PREVIEW:
      ring_show(&e->draft, true);
      break;
    case F_DISCARD:
      e->discard = true;
      list_window_close(e->lw);
      break;
    case F_DELETE:
      if (!e->confirm_delete) {
        e->confirm_delete = true;
        list_window_reload(e->lw);
      } else {
        TTAlarm *a = store_alarm_by_id(e->id);
        if (a) {
          store_alarm_delete(a);
          main_changed();
        }
        e->discard = true;
        list_window_close(e->lw);
        ui_toast("Deleted");
      }
      break;
    default:
      break;
  }
}

static void prv_appear(void *ctx) {
  Editor *e = ctx;
  // Back from the repeat editor: the rule may have changed kind.
  tt_rule_normalize(&e->draft.rule);
  prv_build(e);
}

//! Leaving the editor: save unless told not to.
static void prv_destroy(void *ctx) {
  Editor *e = ctx;
  buzz_stop();
  if (!e->discard && e->touched) {
    TTAlarm *a = e->id ? store_alarm_by_id(e->id) : NULL;
    const bool was_new = (a == NULL);
    if (a) {
      const time_t handled = a->handled_until;
      *a = e->draft;
      a->handled_until = handled;
    } else {
      a = store_alarm_add(&e->draft);
    }
    if (a) {
      sched_edited(a);
      main_changed();
      char when[32];
      char msg[48];
      if (a->on && a->next) {
        ui_format_when(a->next, when, sizeof(when));
        snprintf(msg, sizeof(msg), "%s\n%s", was_new ? "Alarm set" : "Saved", when);
      } else {
        snprintf(msg, sizeof(msg), "%s", was_new ? "Alarm saved" : "Saved");
      }
      ui_toast(msg);
    } else {
      ui_toast("No room for more alarms");
    }
  }
  free(e);
}

void editor_show(uint32_t alarm_id) {
  Editor *e = malloc(sizeof(Editor));
  if (!e) {
    return;
  }
  memset(e, 0, sizeof(*e));
  TTAlarm *existing = alarm_id ? store_alarm_by_id(alarm_id) : NULL;
  if (existing) {
    e->draft = *existing;
    e->id = alarm_id;
  } else {
    if (store_alarm_count() >= TT_MAX_ALARMS) {
      free(e);
      ui_toast("No room for more alarms");
      return;
    }
    // A new alarm starts as a one-off an hour from now, on the next 5 minutes.
    tt_alarm_init(&e->draft);
    int minute;
    tt_epoch_to_local(time(NULL), NULL, &minute, NULL);
    e->draft.minute = (uint16_t)(((minute + 60 + 4) / 5 * 5) % 1440);
    e->draft.rule.kind = TT_RULE_ONCE;
    e->auto_date = true;
    prv_follow_time(e);
  }
  prv_build(e);
  static const ListCallbacks callbacks = {
      .num_rows = prv_rows,
      .row = prv_row,
      .select = prv_select,
      .appear = prv_appear,
      .destroy = prv_destroy,
  };
  e->lw = list_window_push(existing ? "Edit alarm" : "New alarm", &callbacks, e);
  if (!existing && e->lw) {
    // A new alarm almost always starts with its time.
    pick_time("Time", e->draft.minute, prv_time_picked, e);
  }
}
