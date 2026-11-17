#include "../sched.h"
#include "../store.h"
#include "../widget/tt_widget.h"
#include "screens.h"

enum { ROW_NEXT, ROW_NEW, ROW_ALARMS, ROW_PATTERNS, ROW_ARCHIVE, ROW_SETTINGS, ROW_COUNT };

#define CARD_ROWS 3
#define CARD_HEAD 26

static ListWindow *s_home;
static TTWidgetItem s_items[CARD_ROWS];
static int s_item_count;
static int s_missed;

static void prv_appear(void *ctx) {
  static TTOccurrence occ[CARD_ROWS];
  sched_refresh();
  s_item_count = sched_agenda(occ, CARD_ROWS);
  for (int i = 0; i < s_item_count; i++) {
    TTAlarm *a = store_alarm(occ[i].slot);
    s_items[i].when = occ[i].when;
    s_items[i].snoozed = occ[i].snoozed;
    tt_copy(s_items[i].name, sizeof(s_items[i].name), a ? a->msg : "");
  }
  s_missed = 0;
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    TTAlarm *a = store_alarm(slot);
    if (a && a->missed) {
      s_missed++;
    }
  }
}

static uint16_t prv_rows(void *ctx, uint16_t section) {
  return ROW_COUNT;
}

static int16_t prv_row_height(void *ctx, uint16_t section, uint16_t row) {
  if (row != ROW_NEXT) {
    return 0;
  }
  int rows = s_item_count ? s_item_count : 1;
  return (int16_t)(CARD_HEAD + rows * 22 + 6);
}

static bool prv_draw(void *ctx, GContext *g, GRect b, uint16_t section, uint16_t row,
                     bool highlighted) {
  if (row != ROW_NEXT) {
    return false;
  }
  char heading[32];
  if (s_missed) {
    snprintf(heading, sizeof(heading), "COMING UP  (%d missed)", s_missed);
  } else {
    snprintf(heading, sizeof(heading), "COMING UP");
  }
  graphics_context_set_text_color(g, highlighted ? UI_ON_ACCENT : UI_DIM);
  graphics_draw_text(g, heading, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(8, 0, b.size.w - 12, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  TTWidgetStyle style = tt_widget_default_style();
  style.empty = "Nothing scheduled";
  if (highlighted) {
    style.text = UI_ON_ACCENT;
    style.accent = UI_ON_ACCENT;
  }
  tt_widget_draw(g, GRect(8, CARD_HEAD, b.size.w - 14, b.size.h - CARD_HEAD), s_items,
                 s_item_count, CARD_ROWS, &style);
  return true;
}

static void prv_row(void *ctx, uint16_t section, uint16_t row, ListRow *out) {
  switch (row) {
    case ROW_NEW:
      tt_copy(out->title, sizeof(out->title), "New alarm");
      tt_copy(out->right, sizeof(out->right), "+");
      break;
    case ROW_ALARMS:
      tt_copy(out->title, sizeof(out->title), "Alarms");
      snprintf(out->right, sizeof(out->right), "%d on", store_alarm_count_on());
      break;
    case ROW_PATTERNS:
      tt_copy(out->title, sizeof(out->title), "Buzz patterns");
      snprintf(out->right, sizeof(out->right), "%d", store_pattern_count());
      break;
    case ROW_ARCHIVE:
      tt_copy(out->title, sizeof(out->title), "Archive");
      snprintf(out->right, sizeof(out->right), "%d", store_archive_count());
      break;
    case ROW_SETTINGS:
      tt_copy(out->title, sizeof(out->title), "Settings");
      break;
    default:
      break;
  }
}

static void prv_select(void *ctx, uint16_t section, uint16_t row) {
  switch (row) {
    case ROW_NEXT:
      agenda_show();
      break;
    case ROW_NEW:
      editor_show(0);
      break;
    case ROW_ALARMS:
      alarms_show();
      break;
    case ROW_PATTERNS:
      patterns_show();
      break;
    case ROW_ARCHIVE:
      archive_show();
      break;
    case ROW_SETTINGS:
      settings_show();
      break;
    default:
      break;
  }
}

static void prv_destroy(void *ctx) {
  s_home = NULL;
}

void home_show(void) {
  if (s_home) {
    return;
  }
  static const ListCallbacks callbacks = {
      .num_rows = prv_rows,
      .row = prv_row,
      .select = prv_select,
      .appear = prv_appear,
      .destroy = prv_destroy,
      .row_height = prv_row_height,
      .draw = prv_draw,
  };
  prv_appear(NULL);
  s_home = list_window_push("Time To", &callbacks, NULL);
  // Start on "New alarm" so the card above reads as a card, not a selection.
  list_window_select(s_home, 0, ROW_NEW);
}
