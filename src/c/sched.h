// Scheduling: working out what rings next, keeping the watch's wakeups in
// step with that, and the things you can do to an alarm (snooze, done, skip).
#pragma once

#include <pebble.h>

#include "core/tt_core.h"

typedef struct {
  time_t when;
  uint8_t slot;     // store_alarm(slot)
  uint8_t snoozed;  // this entry is a snoozed alarm coming back
} TTOccurrence;

typedef enum {
  SCHED_DONE_REPEATS,   // finished for now, it will ring again
  SCHED_DONE_OFF,       // no more occurrences: left in the list, switched off
  SCHED_DONE_ARCHIVED,  // no more occurrences: moved to the archive
  SCHED_DONE_DELETED,   // no more occurrences: deleted
} SchedDone;

//! Recompute every alarm's next ring time. Call after anything changes.
void sched_refresh(void);
//! Replace the app's wakeups with the next few ring times.
void sched_schedule_wakeups(void);
//! The earliest alarm that should be ringing right now, or NULL.
TTAlarm *sched_due(time_t now);
//! The earliest ring time still to come, 0 if none.
time_t sched_next_time(void);

//! Snooze length for button 0 (top) or 1 (bottom), falling back to the defaults.
uint32_t sched_snooze_minutes(const TTAlarm *a, int which);
void sched_snooze(TTAlarm *a, uint32_t minutes);
//! Finish the current ring. The alarm may be gone afterwards (see SchedDone).
SchedDone sched_done(TTAlarm *a);
//! Nobody answered after all the retries: stop ringing, mark it missed.
void sched_give_up(TTAlarm *a);
void sched_set_on(TTAlarm *a, bool on);
//! Skip the next occurrence (or cancel a pending snooze).
void sched_skip_next(TTAlarm *a);
void sched_unskip(TTAlarm *a);
bool sched_is_skipping(const TTAlarm *a);
//! Call after creating or editing an alarm: restarts its schedule from now.
void sched_edited(TTAlarm *a);

//! Upcoming rings across all alarms in time order. Returns how many were written.
int sched_agenda(TTOccurrence *out, int max);
