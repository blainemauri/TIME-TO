// The archive: alarms put away for later. Nothing here ever rings; bring one
// back as it was, or use it as a template for a fresh copy.
#include "../main.h"
#include "../sched.h"
#include "../store.h"
#include "pickers.h"
#include "screens.h"

static ListWindow *s_archive;
static int s_target = -1;

static uint16_t prv_rows(void *ctx, uint16_t section) {
  return (uint16_t)store_archive_count();
}

static void prv_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  static TTAlarm a;
  if (!store_archive_get(row, &a)) {
    tt_copy(out->title, sizeof(out->title), "(unreadable)");
    return;
  }
  alarm_title(&a, out->title, sizeof(out->title));
  alarm_summary(&a, out->subtitle, sizeof(out->subtitle));
  out->tag = a.color;
}

static void prv_action(void *ctx, int index) {
  if (s_target < 0 || s_target >= store_archive_count()) {
    return;
  }
  if (index == 0 || index == 1) {
    // 0: move it back. 1: leave it here and make a new alarm from it.
    TTAlarm *a = NULL;
    if (index == 0) {
      a = store_archive_restore(s_target);
    } else {
      static TTAlarm copy;
      if (store_archive_get(s_target, &copy)) {
        copy.id = 0;
        copy.on = true;
        a = store_alarm_add(&copy);
      }
    }
    if (!a) {
      ui_toast("No room for more alarms");
      return;
    }
    // A one-off whose date has gone moves to the next time it can ring, and an
    // end date already behind us is dropped.
    int32_t today;
    int minute;
    tt_epoch_to_local(time(NULL), &today, &minute, NULL);
    if (tt_date_is_set(a->until) && tt_date_to_days(a->until) < today) {
      a->until = TT_DATE_NONE;
    }
    if (a->rule.kind == TT_RULE_ONCE && tt_alarm_finished(a, time(NULL))) {
      a->rule.date = tt_civil_from_days(a->minute > minute ? today : today + 1);
    }
    sched_edited(a);
    main_changed();
    // Straight into the editor, since a revived alarm usually needs a new date.
    editor_show(a->id);
  } else if (index == 2) {
    store_archive_delete(s_target);
    main_changed();
    ui_toast("Deleted for good");
  }
}

static void prv_select(void *ctx, uint16_t section, uint16_t row) {
  static TTAlarm a;
  if (!store_archive_get(row, &a)) {
    return;
  }
  s_target = row;
  static const char *items[] = {"Restore", "Use as template", "Delete forever"};
  char title[28];
  alarm_title(&a, title, sizeof(title));
  pick_choice(title, items, 3, 0, prv_action, NULL, NULL);
}

static void prv_destroy(void *ctx) {
  s_archive = NULL;
}

void archive_show(void) {
  if (s_archive) {
    return;
  }
  static const ListCallbacks callbacks = {
      .num_rows = prv_rows,
      .row = prv_row,
      .select = prv_select,
      .destroy = prv_destroy,
      .empty_text = "Nothing archived.\n\nFinished one-offs land here.",
  };
  s_archive = list_window_push("Archive", &callbacks, NULL);
}
