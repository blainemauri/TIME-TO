#include "sched.h"

#include "store.h"

#define MAX_WAKEUPS 8
#define WAKEUP_WINDOW 60  // the firmware keeps an app's wakeups a minute apart

void sched_refresh(void) {
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    if (a) {
      a->next = tt_alarm_next_fire(a);
    }
  }
}

TTAlarm *sched_due(time_t now) {
  TTAlarm *best = NULL;
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    if (a && a->next && a->next <= now && (!best || a->next < best->next)) {
      best = a;
    }
  }
  return best;
}

time_t sched_next_time(void) {
  time_t best = 0;
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    if (a && a->next && (!best || a->next < best)) {
      best = a->next;
    }
  }
  return best;
}

void sched_schedule_wakeups(void) {
  static time_t times[TT_MAX_ALARMS];
  static uint32_t ids[TT_MAX_ALARMS];
  int n = 0;
  const time_t now = time(NULL);

  // Collect the ring times, sorted soonest first (insertion sort, n <= 64).
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    if (!a || !a->next) {
      continue;
    }
    int i = n++;
    while (i > 0 && times[i - 1] > a->next) {
      times[i] = times[i - 1];
      ids[i] = ids[i - 1];
      i--;
    }
    times[i] = a->next;
    ids[i] = a->id;
  }

  wakeup_cancel_all();
  int scheduled = 0;
  time_t last = 0;
  for (int i = 0; i < n && scheduled < MAX_WAKEUPS; i++) {
    time_t t = times[i];
    if (t <= now + 2) {
      t = now + 15;  // overdue and unanswered: come straight back
    }
    if (last && t < last + WAKEUP_WINDOW) {
      continue;  // the previous wakeup already covers this one
    }
    for (int attempt = 0; attempt < 4; attempt++) {
      WakeupId id = wakeup_schedule(t, (int32_t)ids[i], true);
      if (id >= 0) {
        scheduled++;
        last = t;
        break;
      }
      if (id == E_OUT_OF_RESOURCES) {
        scheduled = MAX_WAKEUPS;
        break;
      }
      APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup at %d refused (%d)", (int)t, (int)id);
      t += WAKEUP_WINDOW;  // slot taken: a minute late beats never
    }
  }
}

uint32_t sched_snooze_minutes(const TTAlarm *a, int which) {
  const TTSettings *s = store_settings();
  uint32_t m = which == 0 ? a->snooze_a : a->snooze_b;
  if (m == 0) {
    m = which == 0 ? s->snooze_a : s->snooze_b;
  }
  return m ? m : 1;
}

void sched_snooze(TTAlarm *a, uint32_t minutes) {
  const time_t now = time(NULL);
  a->snooze_until = now + (time_t)minutes * 60;
  a->handled_until = now;
  a->missed = 0;
  store_alarm_save_state(a);
  sched_refresh();
}

SchedDone sched_done(TTAlarm *a) {
  const time_t now = time(NULL);
  a->snooze_until = 0;
  a->auto_snoozes = 0;
  a->missed = 0;
  a->handled_until = now;
  SchedDone result = SCHED_DONE_REPEATS;
  if (tt_alarm_finished(a, now)) {
    uint8_t action = store_settings()->done_action;
    if (a->flags & TT_F_DONE_KEEP) {
      action = TT_DONE_KEEP;
    } else if (a->flags & TT_F_DONE_ARCHIVE) {
      action = TT_DONE_ARCHIVE;
    } else if (a->flags & TT_F_DONE_DELETE) {
      action = TT_DONE_DELETE;
    }
    if (action == TT_DONE_ARCHIVE && store_alarm_archive(a)) {
      result = SCHED_DONE_ARCHIVED;
    } else if (action == TT_DONE_DELETE) {
      store_alarm_delete(a);
      result = SCHED_DONE_DELETED;
    } else {
      a->on = false;
      store_alarm_save(a);
      result = SCHED_DONE_OFF;
    }
  } else {
    store_alarm_save_state(a);
  }
  sched_refresh();
  return result;
}

void sched_give_up(TTAlarm *a) {
  const time_t now = time(NULL);
  a->snooze_until = 0;
  a->auto_snoozes = 0;
  a->handled_until = now;
  a->missed = 1;
  if (tt_alarm_finished(a, now)) {
    a->on = false;  // never throw away something nobody saw
    store_alarm_save(a);
  } else {
    store_alarm_save_state(a);
  }
  sched_refresh();
}

void sched_set_on(TTAlarm *a, bool on) {
  const time_t now = time(NULL);
  a->on = on;
  a->snooze_until = 0;
  a->skip_until = 0;
  a->auto_snoozes = 0;
  a->missed = 0;
  a->handled_until = now;
  if (on && a->rule.kind == TT_RULE_ONCE && tt_alarm_finished(a, now)) {
    // Switching a spent one-off back on means "the next time it is that time".
    int32_t today;
    int minute;
    tt_epoch_to_local(now, &today, &minute, NULL);
    a->rule.date = tt_civil_from_days(minute < a->minute ? today : today + 1);
    a->until = TT_DATE_NONE;
  }
  store_alarm_save(a);
  sched_refresh();
}

void sched_skip_next(TTAlarm *a) {
  const time_t now = time(NULL);
  if (a->snooze_until) {
    a->snooze_until = 0;
    a->handled_until = now;
  } else {
    time_t after = a->handled_until > a->skip_until ? a->handled_until : a->skip_until;
    time_t next = tt_alarm_next_occurrence(a, after);
    if (next) {
      a->skip_until = next;
    }
  }
  store_alarm_save_state(a);
  sched_refresh();
}

void sched_unskip(TTAlarm *a) {
  a->skip_until = 0;
  store_alarm_save_state(a);
  sched_refresh();
}

bool sched_is_skipping(const TTAlarm *a) {
  return a->on && !a->snooze_until && a->skip_until > time(NULL);
}

void sched_edited(TTAlarm *a) {
  a->snooze_until = 0;
  a->skip_until = 0;
  a->auto_snoozes = 0;
  a->missed = 0;
  a->handled_until = time(NULL);
  if (a->every && a->last < a->minute) {
    a->last = 1439;
  }
  tt_rule_normalize(&a->rule);
  store_alarm_save(a);
  sched_refresh();
}

int sched_agenda(TTOccurrence *out, int max) {
  static time_t cursor[TT_MAX_ALARMS];
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    cursor[slot] = a ? a->next : 0;
  }
  int n = 0;
  while (n < max) {
    int best = -1;
    for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
      if (cursor[slot] && (best < 0 || cursor[slot] < cursor[best])) {
        best = slot;
      }
    }
    if (best < 0) {
      break;
    }
    TTAlarm *a = store_alarm(best);
    out[n].when = cursor[best];
    out[n].slot = (uint8_t)best;
    out[n].snoozed = a->snooze_until && cursor[best] == a->snooze_until;
    n++;
    time_t after = cursor[best] > a->skip_until ? cursor[best] : a->skip_until;
    cursor[best] = tt_alarm_next_occurrence(a, after);
  }
  return n;
}
