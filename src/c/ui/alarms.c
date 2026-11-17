// The agenda, the list of all alarms, and the per-alarm action menu.
#include "../main.h"
#include "../sched.h"
#include "../store.h"
#include "../widget/tt_widget.h"
#include "ring.h"
#include "screens.h"

const uint32_t snooze_presets[SNOOZE_PRESET_COUNT] = {0,  1,  3,  5,   10,  15,  20,
                                                      30, 45, 60, 120, 480, 1440};

void alarm_title(const TTAlarm *a, char *buf, size_t cap) {
  tt_copy(buf, cap, a->msg[0] ? a->msg : "Alarm");
}

void alarm_summary(const TTAlarm *a, char *buf, size_t cap) {
  char clock[12];
  char rule[40];
  ui_format_clock(a->minute, clock, sizeof(clock));
  tt_rule_describe(&a->rule, rule, sizeof(rule));
  if (a->every) {
    char every[12];
    tt_format_duration(a->every, every, sizeof(every));
    snprintf(buf, cap, "%s +%s  %s", clock, every, rule);
  } else {
    snprintf(buf, cap, "%s  %s", clock, rule);
  }
}

// ---------------------------------------------------------------- agenda

#define AGENDA_MAX 30

static ListWindow *s_agenda;
static TTOccurrence s_occ[AGENDA_MAX];
static int s_occ_count;
static uint8_t s_day_start[AGENDA_MAX + 1];  // first occurrence index of each day
static int s_day_count;

static void prv_agenda_appear(void *ctx) {
  sched_refresh();
  s_occ_count = sched_agenda(s_occ, AGENDA_MAX);
  s_day_count = 0;
  int32_t last_day = INT32_MIN;
  for (int i = 0; i < s_occ_count; i++) {
    int32_t day;
    tt_epoch_to_local(s_occ[i].when, &day, NULL, NULL);
    if (day != last_day) {
      s_day_start[s_day_count++] = (uint8_t)i;
      last_day = day;
    }
  }
  s_day_start[s_day_count] = (uint8_t)s_occ_count;
}

static uint16_t prv_agenda_sections(void *ctx) {
  return (uint16_t)s_day_count;
}

static uint16_t prv_agenda_rows(void *ctx, uint16_t section) {
  if (section >= s_day_count) {
    return 0;
  }
  return (uint16_t)(s_day_start[section + 1] - s_day_start[section]);
}

static void prv_agenda_header(void *ctx, uint16_t section, char *buf, size_t cap) {
  if (section < s_day_count) {
    ui_format_day(s_occ[s_day_start[section]].when, buf, cap);
  }
}

static void prv_agenda_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  if (section >= s_day_count) {
    return;
  }
  const TTOccurrence *occ = &s_occ[s_day_start[section] + row];
  TTAlarm *a = store_alarm(occ->slot);
  if (!a) {
    return;
  }
  char clock[12];
  char until[20];
  int minute;
  tt_epoch_to_local(occ->when, NULL, &minute, NULL);
  ui_format_clock(minute, clock, sizeof(clock));
  tt_format_relative((int32_t)(occ->when - time(NULL)), until, sizeof(until));
  alarm_title(a, out->title, sizeof(out->title));
  snprintf(out->subtitle, sizeof(out->subtitle), "%s  %s%s", clock, until,
           occ->snoozed ? "  snoozed" : "");
  out->tag = a->color;
}

static void prv_agenda_select(void *ctx, uint16_t section, uint16_t row) {
  if (section >= s_day_count) {
    return;
  }
  TTAlarm *a = store_alarm(s_occ[s_day_start[section] + row].slot);
  if (a) {
    alarm_actions_show(a->id);
  }
}

static void prv_agenda_destroy(void *ctx) {
  s_agenda = NULL;
}

void agenda_show(void) {
  if (s_agenda) {
    return;
  }
  static const ListCallbacks callbacks = {
      .num_sections = prv_agenda_sections,
      .num_rows = prv_agenda_rows,
      .header = prv_agenda_header,
      .row = prv_agenda_row,
      .select = prv_agenda_select,
      .appear = prv_agenda_appear,
      .destroy = prv_agenda_destroy,
      .empty_text = "Nothing coming up",
  };
  prv_agenda_appear(NULL);
  s_agenda = list_window_push("Agenda", &callbacks, NULL);
}

// ---------------------------------------------------------------- all alarms

static ListWindow *s_list;
static uint8_t s_slots[TT_MAX_ALARMS];
static int s_slot_count;

//! Sort key: alarms that will ring come first (soonest first), then the ones
//! that are on but have nothing left, then the ones switched off by time of day.
static bool prv_before(const TTAlarm *a, const TTAlarm *b) {
  const int ga = a->next ? 0 : (a->on ? 1 : 2);
  const int gb = b->next ? 0 : (b->on ? 1 : 2);
  if (ga != gb) {
    return ga < gb;
  }
  if (ga == 0) {
    return a->next < b->next;
  }
  return a->minute < b->minute;
}

static void prv_list_appear(void *ctx) {
  sched_refresh();
  s_slot_count = 0;
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    if (!a) {
      continue;
    }
    int i = s_slot_count++;
    while (i > 0 && prv_before(a, store_alarm(s_slots[i - 1]))) {
      s_slots[i] = s_slots[i - 1];
      i--;
    }
    s_slots[i] = (uint8_t)slot;
  }
}

static uint16_t prv_list_rows(void *ctx, uint16_t section) {
  return (uint16_t)(s_slot_count + 1);
}

static void prv_list_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  if (row == 0) {
    tt_copy(out->title, sizeof(out->title), "New alarm");
    tt_copy(out->right, sizeof(out->right), "+");
    return;
  }
  TTAlarm *a = store_alarm(s_slots[row - 1]);
  if (!a) {
    return;
  }
  alarm_title(a, out->title, sizeof(out->title));
  alarm_summary(a, out->subtitle, sizeof(out->subtitle));
  out->tag = a->color;
  if (!a->on) {
    out->flags |= LIST_DIM;
    tt_copy(out->right, sizeof(out->right), a->missed ? "missed" : "off");
  } else if (a->snooze_until) {
    tt_copy(out->right, sizeof(out->right), "zz");
  } else if (sched_is_skipping(a)) {
    tt_copy(out->right, sizeof(out->right), "skip");
  } else if (a->next) {
    tt_widget_format_until(a->next, out->right, sizeof(out->right));
  } else {
    tt_copy(out->right, sizeof(out->right), "ended");
  }
}

static void prv_list_select(void *ctx, uint16_t section, uint16_t row) {
  if (row == 0) {
    editor_show(0);
    return;
  }
  TTAlarm *a = store_alarm(s_slots[row - 1]);
  if (a) {
    alarm_actions_show(a->id);
  }
}

static void prv_list_select_long(void *ctx, uint16_t section, uint16_t row) {
  if (row == 0) {
    editor_show(0);
    return;
  }
  TTAlarm *a = store_alarm(s_slots[row - 1]);
  if (a) {
    sched_set_on(a, !a->on);
    main_changed();
    vibes_short_pulse();
    list_window_reload(s_list);
  }
}

static void prv_list_destroy(void *ctx) {
  s_list = NULL;
}

void alarms_show(void) {
  if (s_list) {
    return;
  }
  static const ListCallbacks callbacks = {
      .num_rows = prv_list_rows,
      .row = prv_list_row,
      .select = prv_list_select,
      .select_long = prv_list_select_long,
      .appear = prv_list_appear,
      .destroy = prv_list_destroy,
  };
  prv_list_appear(NULL);
  s_list = list_window_push("Alarms", &callbacks, NULL);
}

// ---------------------------------------------------------------- actions

enum {
  ACT_TOGGLE,
  ACT_EDIT,
  ACT_SKIP,
  ACT_DUPLICATE,
  ACT_PREVIEW,
  ACT_ARCHIVE,
  ACT_DELETE,
};

typedef struct {
  ListWindow *lw;
  uint32_t id;
  uint8_t acts[8];
  int count;
  bool confirm_delete;
} Actions;

static void prv_actions_build(Actions *x) {
  TTAlarm *a = store_alarm_by_id(x->id);
  x->count = 0;
  if (!a) {
    return;
  }
  x->acts[x->count++] = ACT_TOGGLE;
  x->acts[x->count++] = ACT_EDIT;
  if (a->on && (a->next || sched_is_skipping(a))) {
    x->acts[x->count++] = ACT_SKIP;
  }
  x->acts[x->count++] = ACT_DUPLICATE;
  x->acts[x->count++] = ACT_PREVIEW;
  x->acts[x->count++] = ACT_ARCHIVE;
  x->acts[x->count++] = ACT_DELETE;
}

static void prv_actions_appear(void *ctx) {
  Actions *x = ctx;
  prv_actions_build(x);
  TTAlarm *a = store_alarm_by_id(x->id);
  if (a && x->lw) {
    char title[28];
    alarm_title(a, title, sizeof(title));
    list_window_set_title(x->lw, title);
  }
}

static uint16_t prv_actions_rows(void *ctx, uint16_t section) {
  return (uint16_t)((Actions *)ctx)->count;
}

static void prv_actions_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  Actions *x = ctx;
  TTAlarm *a = store_alarm_by_id(x->id);
  if (!a || row >= x->count) {
    return;
  }
  switch (x->acts[row]) {
    case ACT_TOGGLE:
      tt_copy(out->title, sizeof(out->title), a->on ? "Turn off" : "Turn on");
      if (a->on && a->next) {
        ui_format_when(a->next, out->subtitle, sizeof(out->subtitle));
      } else {
        alarm_summary(a, out->subtitle, sizeof(out->subtitle));
      }
      break;
    case ACT_EDIT:
      tt_copy(out->title, sizeof(out->title), "Edit");
      break;
    case ACT_SKIP:
      if (a->snooze_until) {
        tt_copy(out->title, sizeof(out->title), "Cancel snooze");
      } else if (sched_is_skipping(a)) {
        tt_copy(out->title, sizeof(out->title), "Don't skip");
      } else {
        tt_copy(out->title, sizeof(out->title), "Skip next");
      }
      break;
    case ACT_DUPLICATE:
      tt_copy(out->title, sizeof(out->title), "Duplicate");
      break;
    case ACT_PREVIEW:
      tt_copy(out->title, sizeof(out->title), "Preview");
      break;
    case ACT_ARCHIVE:
      tt_copy(out->title, sizeof(out->title), "Archive");
      break;
    case ACT_DELETE:
      tt_copy(out->title, sizeof(out->title),
              x->confirm_delete ? "Really delete?" : "Delete");
      out->flags |= LIST_DANGER;
      break;
    default:
      break;
  }
}

static void prv_actions_select(void *ctx, uint16_t section, uint16_t row) {
  Actions *x = ctx;
  TTAlarm *a = store_alarm_by_id(x->id);
  if (!a || row >= x->count) {
    list_window_close(x->lw);
    return;
  }
  ListWindow *lw = x->lw;
  const uint32_t id = x->id;
  switch (x->acts[row]) {
    case ACT_TOGGLE:
      sched_set_on(a, !a->on);
      main_changed();
      list_window_close(lw);
      break;
    case ACT_EDIT:
      list_window_close(lw);
      editor_show(id);
      break;
    case ACT_SKIP:
      if (sched_is_skipping(a)) {
        sched_unskip(a);
      } else {
        sched_skip_next(a);
      }
      main_changed();
      list_window_close(lw);
      break;
    case ACT_DUPLICATE: {
      static TTAlarm copy;
      copy = *a;
      copy.id = 0;
      TTAlarm *made = store_alarm_add(&copy);
      list_window_close(lw);
      if (made) {
        sched_edited(made);
        main_changed();
        editor_show(made->id);
      } else {
        ui_toast("No room for more alarms");
      }
      break;
    }
    case ACT_PREVIEW:
      ring_show(a, true);
      break;
    case ACT_ARCHIVE:
      if (store_alarm_archive(a)) {
        main_changed();
        list_window_close(lw);
        ui_toast("Archived");
      } else {
        ui_toast("Archive is full");
      }
      break;
    case ACT_DELETE:
      if (!x->confirm_delete) {
        x->confirm_delete = true;
        list_window_reload(lw);
      } else {
        store_alarm_delete(a);
        main_changed();
        list_window_close(lw);
        ui_toast("Deleted");
      }
      break;
    default:
      break;
  }
}

static void prv_actions_highlight(void *ctx, uint16_t section, uint16_t row) {
  Actions *x = ctx;
  if (x->confirm_delete) {
    x->confirm_delete = false;  // moving away cancels the "really?" prompt
    list_window_reload(x->lw);
  }
}

static void prv_actions_destroy(void *ctx) {
  free(ctx);
}

void alarm_actions_show(uint32_t alarm_id) {
  TTAlarm *a = store_alarm_by_id(alarm_id);
  if (!a) {
    return;
  }
  Actions *x = malloc(sizeof(Actions));
  if (!x) {
    return;
  }
  memset(x, 0, sizeof(*x));
  x->id = alarm_id;
  prv_actions_build(x);
  static const ListCallbacks callbacks = {
      .num_rows = prv_actions_rows,
      .row = prv_actions_row,
      .select = prv_actions_select,
      .highlight = prv_actions_highlight,
      .appear = prv_actions_appear,
      .destroy = prv_actions_destroy,
      .empty_text = "This alarm is gone",
  };
  char title[28];
  alarm_title(a, title, sizeof(title));
  x->lw = list_window_push(title, &callbacks, x);
}
