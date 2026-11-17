#include "glance.h"

#include <pebble.h>

#include "sched.h"
#include "store.h"

#define GLANCE_SLICES 6

static TTOccurrence s_occ[GLANCE_SLICES];
static int s_count;

static void prv_reload(AppGlanceReloadSession *session, size_t limit, void *context) {
  static char text[160];
  // One slice per upcoming ring. Each expires when its alarm goes off, so the
  // launcher keeps showing the right "next" without the app running.
  for (int i = 0; i < s_count && (size_t)i + 1 < limit; i++) {
    TTAlarm *a = store_alarm(s_occ[i].slot);
    if (!a) {
      continue;
    }
    snprintf(text, sizeof(text),
             "{time_until(%ld)|format(>=1d:'in %%fdd %%Hh',>=1M:'in %%aR','now')}: %s", (long)s_occ[i].when,
             a->msg[0] ? a->msg : "Alarm");
    const AppGlanceSlice slice = {
        .layout = {.icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text},
        .expiration_time = s_occ[i].when,
    };
    app_glance_add_slice(session, slice);
  }
  if (limit > 0) {
    const int on = store_alarm_count_on();
    if (on) {
      snprintf(text, sizeof(text), "%d alarm%s on", on, on == 1 ? "" : "s");
    } else {
      snprintf(text, sizeof(text), "No alarms on");
    }
    const AppGlanceSlice slice = {
        .layout = {.icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text},
        .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
    };
    app_glance_add_slice(session, slice);
  }
}

void glance_update(void) {
  s_count = sched_agenda(s_occ, GLANCE_SLICES);
  app_glance_reload(prv_reload, NULL);
}
