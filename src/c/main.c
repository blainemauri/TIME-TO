// Time To: alarms with their own rules, their own snoozes and their own buzz.
#include "main.h"

#include "comm.h"
#include "glance.h"
#include "sched.h"
#include "store.h"
#include "ui/screens.h"
#include "ui/ring.h"
#include "ui/ui.h"

static AppTimer *s_settle_timer;
static AppTimer *s_due_timer;

static void prv_due_timer(void *unused) {
  s_due_timer = NULL;
  main_check_due();
}

//! While the app is open, wake ourselves exactly when the next alarm is due
//! rather than waiting for a wakeup or the minute tick.
static void prv_arm_due_timer(void) {
  if (s_due_timer) {
    app_timer_cancel(s_due_timer);
    s_due_timer = NULL;
  }
  const time_t next = sched_next_time();
  if (!next) {
    return;
  }
  const time_t now = time(NULL);
  int32_t wait = (int32_t)(next - now);
  if (wait < 0) {
    wait = 0;
  }
  if (wait > 3600) {
    wait = 3600;  // re-arm at least hourly; keeps the delay well inside 32 bits
  }
  s_due_timer = app_timer_register((uint32_t)wait * 1000 + 250, prv_due_timer, NULL);
}

void main_check_due(void) {
  sched_refresh();
  if (!ring_showing()) {
    TTAlarm *due = sched_due(time(NULL) + 1);
    if (due) {
      ring_show(due, false);
    }
  }
  prv_arm_due_timer();
}

static void prv_settle(void *unused) {
  s_settle_timer = NULL;
  sched_refresh();
  sched_schedule_wakeups();
  comm_changed();
  main_check_due();
  ui_refresh_top();
}

void main_changed(void) {
  sched_refresh();
  prv_arm_due_timer();
  if (s_settle_timer) {
    app_timer_reschedule(s_settle_timer, 1200);
  } else {
    s_settle_timer = app_timer_register(1200, prv_settle, NULL);
  }
}

static void prv_wakeup(WakeupId id, int32_t cookie) {
  main_check_due();
}

static void prv_tick(struct tm *tick_time, TimeUnits changed) {
  main_check_due();
  ui_refresh_top();
}

static void prv_init(void) {
  store_init();
  sched_refresh();
  comm_init();
  wakeup_service_subscribe(prv_wakeup);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);

  // Woken for an alarm: go straight to it, and leave nothing behind it so
  // that answering it returns to the watchface.
  TTAlarm *due = sched_due(time(NULL) + 1);
  if (launch_reason() == APP_LAUNCH_WAKEUP && due) {
    ring_show(due, false);
  } else {
    home_show();
    if (due) {
      ring_show(due, false);
    }
  }
  prv_arm_due_timer();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  sched_refresh();
  sched_schedule_wakeups();
  glance_update();
  comm_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
