// Editing when an alarm repeats.
#include "pickers.h"
#include "screens.h"

enum {
  R_KIND,
  R_DATE,       // once: the date
  R_IN_DAYS,    // once: "in N days"
  R_DAY0,       // weekly: 7 rows, Monday..Sunday
  R_MDAY = R_DAY0 + 7,
  R_FROM_END,   // monthly: count the day from the end of the month
  R_NTH,
  R_WDAY,
  R_MONTH,
  R_YMDAY,
  R_YDAY,
  R_INTERVAL,
  R_START,
  R_COUNT
};

// The kinds as offered, in the order offered.
typedef struct {
  const char *name;
  uint8_t kind;
  uint8_t preset;  // weekly presets: 0 = pick days, else the day mask
} KindChoice;

static const KindChoice s_kinds[] = {
    {"Once", TT_RULE_ONCE, 0},
    {"Every day", TT_RULE_DAILY, 0},
    {"Weekdays", TT_RULE_WEEKLY, TT_DOW_WEEKDAYS},
    {"Weekends", TT_RULE_WEEKLY, TT_DOW_WEEKEND},
    {"Days of the week", TT_RULE_WEEKLY, 0},
    {"Day of the month", TT_RULE_MONTHLY, 0},
    {"Nth weekday of month", TT_RULE_NTH, 0},
    {"Once a year", TT_RULE_YEARLY, 0},
    {"Day of the year", TT_RULE_YDAY, 0},
};

typedef struct {
  ListWindow *lw;
  TTRule *rule;
  uint8_t rows[R_COUNT];
  int count;
} Repeat;

static const char *const s_wday_names[7] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                            "Friday", "Saturday", "Sunday"};

static int32_t prv_today(void) {
  int32_t today;
  tt_epoch_to_local(time(NULL), &today, NULL, NULL);
  return today;
}

static void prv_build(Repeat *r) {
  const TTRule *rule = r->rule;
  r->count = 0;
  r->rows[r->count++] = R_KIND;
  switch (rule->kind) {
    case TT_RULE_ONCE:
      r->rows[r->count++] = R_DATE;
      r->rows[r->count++] = R_IN_DAYS;
      return;
    case TT_RULE_WEEKLY:
      for (int d = 0; d < 7; d++) {
        r->rows[r->count++] = (uint8_t)(R_DAY0 + d);
      }
      break;
    case TT_RULE_MONTHLY:
      r->rows[r->count++] = R_MDAY;
      r->rows[r->count++] = R_FROM_END;
      break;
    case TT_RULE_NTH:
      r->rows[r->count++] = R_NTH;
      r->rows[r->count++] = R_WDAY;
      break;
    case TT_RULE_YEARLY:
      r->rows[r->count++] = R_MONTH;
      r->rows[r->count++] = R_YMDAY;
      break;
    case TT_RULE_YDAY:
      r->rows[r->count++] = R_YDAY;
      break;
    default:
      break;
  }
  r->rows[r->count++] = R_INTERVAL;
  r->rows[r->count++] = R_START;
}

static void prv_refresh(Repeat *r) {
  tt_rule_normalize(r->rule);
  prv_build(r);
  list_window_reload(r->lw);
}

static int prv_kind_index(const TTRule *rule) {
  for (size_t i = 0; i < ARRAY_LENGTH(s_kinds); i++) {
    if (s_kinds[i].kind != rule->kind) {
      continue;
    }
    if (rule->kind != TT_RULE_WEEKLY || s_kinds[i].preset == 0 ||
        s_kinds[i].preset == rule->dow_mask) {
      if (rule->kind == TT_RULE_WEEKLY && s_kinds[i].preset == 0 &&
          (rule->dow_mask == TT_DOW_WEEKDAYS || rule->dow_mask == TT_DOW_WEEKEND)) {
        continue;
      }
      return (int)i;
    }
  }
  return 0;
}

static const char *prv_unit(const TTRule *rule, bool plural) {
  switch (rule->kind) {
    case TT_RULE_DAILY:
      return plural ? "days" : "day";
    case TT_RULE_WEEKLY:
      return plural ? "weeks" : "week";
    case TT_RULE_MONTHLY:
    case TT_RULE_NTH:
      return plural ? "months" : "month";
    default:
      return plural ? "years" : "year";
  }
}

static void prv_ordinal(int n, char *buf, size_t cap) {
  const char *suffix = "th";
  if ((n / 10) % 10 != 1) {
    suffix = n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
  }
  snprintf(buf, cap, "%d%s", n, suffix);
}

static void prv_nth_name(int nth, char *buf, size_t cap) {
  if (nth == -1) {
    tt_copy(buf, cap, "Last");
  } else if (nth < 0) {
    char o[8];
    prv_ordinal(-nth, o, sizeof(o));
    tt_copy(buf, cap, o);
    strncat(buf, " to last", cap - strlen(buf) - 1);
  } else {
    prv_ordinal(nth, buf, cap);
  }
}

// ---------------------------------------------------------------- rows

static uint16_t prv_rows(void *ctx, uint16_t section) {
  return (uint16_t)((Repeat *)ctx)->count;
}

static void prv_row(void *ctx, uint16_t section, uint16_t index, ListRow *out) {
  Repeat *r = ctx;
  const TTRule *rule = r->rule;
  if (index >= r->count) {
    return;
  }
  const int row = r->rows[index];
  char buf[24];
  if (row >= R_DAY0 && row < R_DAY0 + 7) {
    const int d = row - R_DAY0;
    tt_copy(out->title, sizeof(out->title), s_wday_names[d]);
    out->flags |= (rule->dow_mask & (1 << d)) ? LIST_CHECKED : LIST_UNCHECKED;
    return;
  }
  switch (row) {
    case R_KIND:
      tt_copy(out->title, sizeof(out->title), s_kinds[prv_kind_index(rule)].name);
      tt_rule_describe(rule, out->subtitle, sizeof(out->subtitle));
      break;
    case R_DATE:
      tt_copy(out->title, sizeof(out->title), "Date");
      if (tt_date_is_set(rule->date)) {
        tt_rule_describe(rule, out->subtitle, sizeof(out->subtitle));
      } else {
        tt_copy(out->subtitle, sizeof(out->subtitle), "Not set");
      }
      break;
    case R_IN_DAYS: {
      tt_copy(out->title, sizeof(out->title), "In ... days");
      if (tt_date_is_set(rule->date)) {
        int32_t delta = tt_date_to_days(rule->date) - prv_today();
        if (delta == 0) {
          tt_copy(out->right, sizeof(out->right), "today");
        } else if (delta > 0) {
          snprintf(out->right, sizeof(out->right), "%ldd", (long)delta);
        }
      }
      break;
    }
    case R_MDAY:
      tt_copy(out->title, sizeof(out->title), "Day");
      prv_ordinal(rule->mday < 0 ? -rule->mday : rule->mday, out->right, sizeof(out->right));
      break;
    case R_FROM_END:
      tt_copy(out->title, sizeof(out->title), "Count from end");
      tt_copy(out->subtitle, sizeof(out->subtitle),
              rule->mday < 0 ? (rule->mday == -1 ? "The last day" : "Days before month end")
                             : "From the 1st");
      out->flags |= rule->mday < 0 ? LIST_CHECKED : LIST_UNCHECKED;
      break;
    case R_NTH:
      tt_copy(out->title, sizeof(out->title), "Which");
      prv_nth_name(rule->nth, out->right, sizeof(out->right));
      break;
    case R_WDAY:
      tt_copy(out->title, sizeof(out->title), "Weekday");
      tt_copy(out->right, sizeof(out->right), tt_wday_abbr[rule->wday % 7]);
      break;
    case R_MONTH:
      tt_copy(out->title, sizeof(out->title), "Month");
      tt_copy(out->right, sizeof(out->right), tt_month_abbr[(rule->month + 11) % 12]);
      break;
    case R_YMDAY:
      tt_copy(out->title, sizeof(out->title), "Day");
      snprintf(out->right, sizeof(out->right), "%d", rule->mday);
      break;
    case R_YDAY:
      tt_copy(out->title, sizeof(out->title), "Day of year");
      snprintf(out->right, sizeof(out->right), "%d", rule->yday);
      if (rule->yday <= 365) {
        TTDate d = tt_civil_from_days(tt_days_from_civil(2027, 1, 1) + rule->yday - 1);
        snprintf(out->subtitle, sizeof(out->subtitle), "%d %s (%d %s in leap years)", d.d,
                 tt_month_abbr[d.m - 1],
                 tt_civil_from_days(tt_days_from_civil(2028, 1, 1) + rule->yday - 1).d,
                 tt_month_abbr[tt_civil_from_days(tt_days_from_civil(2028, 1, 1) + rule->yday -
                                                  1).m - 1]);
      } else {
        tt_copy(out->subtitle, sizeof(out->subtitle), "31 Dec in leap years");
      }
      break;
    case R_INTERVAL:
      tt_copy(out->title, sizeof(out->title), "Every");
      if (rule->interval <= 1) {
        snprintf(out->right, sizeof(out->right), "%s", prv_unit(rule, false));
      } else {
        snprintf(out->right, sizeof(out->right), "%d %s", rule->interval, prv_unit(rule, true));
      }
      break;
    case R_START:
      tt_copy(out->title, sizeof(out->title), "Starting");
      if (tt_date_is_set(rule->date)) {
        tt_format_date(rule->date, buf, sizeof(buf));
        tt_copy(out->subtitle, sizeof(out->subtitle), buf);
      } else {
        tt_copy(out->subtitle, sizeof(out->subtitle), "Now");
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- edits

static void prv_kind_picked(void *ctx, int index) {
  Repeat *r = ctx;
  TTRule *rule = r->rule;
  const KindChoice *k = &s_kinds[index];
  const int32_t today = prv_today();
  const TTDate now = tt_civil_from_days(today);
  const uint8_t old_kind = rule->kind;
  rule->kind = k->kind;
  if (old_kind != k->kind) {
    rule->interval = 1;
    // A fresh kind starts from today's values, which is usually what you want.
    rule->date = k->kind == TT_RULE_ONCE ? now : TT_DATE_NONE;
    rule->mday = (int8_t)now.d;
    rule->wday = (uint8_t)tt_dow(today);
    rule->nth = (int8_t)((now.d - 1) / 7 + 1);
    rule->month = now.m;
    rule->yday = (uint16_t)(today - tt_days_from_civil(now.y, 1, 1) + 1);
  }
  if (k->kind == TT_RULE_WEEKLY) {
    if (k->preset) {
      rule->dow_mask = k->preset;
    } else if (old_kind != TT_RULE_WEEKLY || rule->dow_mask == TT_DOW_WEEKDAYS ||
               rule->dow_mask == TT_DOW_WEEKEND) {
      rule->dow_mask = (uint8_t)(1 << tt_dow(today));
    }
  }
  prv_refresh(r);
}

static void prv_date_picked(void *ctx, TTDate date) {
  Repeat *r = ctx;
  r->rule->date = date;
  prv_refresh(r);
}

static void prv_in_days_picked(void *ctx, int32_t days) {
  Repeat *r = ctx;
  r->rule->date = tt_civil_from_days(prv_today() + days);
  prv_refresh(r);
}

static void prv_mday_picked(void *ctx, int32_t day) {
  Repeat *r = ctx;
  r->rule->mday = (int8_t)(r->rule->mday < 0 ? -day : day);
  prv_refresh(r);
}

static void prv_nth_picked(void *ctx, int index) {
  static const int8_t values[] = {1, 2, 3, 4, 5, -1, -2};
  Repeat *r = ctx;
  r->rule->nth = values[index % 7];
  prv_refresh(r);
}

static void prv_wday_picked(void *ctx, int index) {
  Repeat *r = ctx;
  r->rule->wday = (uint8_t)index;
  prv_refresh(r);
}

static void prv_month_picked(void *ctx, int index) {
  Repeat *r = ctx;
  r->rule->month = (uint8_t)(index + 1);
  prv_refresh(r);
}

static void prv_ymday_picked(void *ctx, int32_t day) {
  Repeat *r = ctx;
  r->rule->mday = (int8_t)day;
  prv_refresh(r);
}

static void prv_yday_picked(void *ctx, int32_t day) {
  Repeat *r = ctx;
  r->rule->yday = (uint16_t)day;
  prv_refresh(r);
}

static void prv_interval_picked(void *ctx, int32_t n) {
  Repeat *r = ctx;
  r->rule->interval = (uint16_t)n;
  // Counting "every other week" needs somewhere to count from.
  if (n > 1 && !tt_date_is_set(r->rule->date)) {
    r->rule->date = tt_civil_from_days(prv_today());
  }
  prv_refresh(r);
}

static void prv_start_picked(void *ctx, int index) {
  Repeat *r = ctx;
  if (index == 0) {
    r->rule->date = TT_DATE_NONE;
    if (r->rule->interval > 1) {
      r->rule->date = tt_civil_from_days(prv_today());
    }
    prv_refresh(r);
  } else {
    pick_date("Starting", r->rule->date, prv_date_picked, r);
  }
}

static void prv_select(void *ctx, uint16_t section, uint16_t index) {
  Repeat *r = ctx;
  TTRule *rule = r->rule;
  if (index >= r->count) {
    return;
  }
  const int row = r->rows[index];
  if (row >= R_DAY0 && row < R_DAY0 + 7) {
    const uint8_t bit = (uint8_t)(1 << (row - R_DAY0));
    if ((rule->dow_mask & ~bit) || !(rule->dow_mask & bit)) {
      rule->dow_mask ^= bit;  // never untick the last remaining day
    }
    prv_refresh(r);
    return;
  }
  switch (row) {
    case R_KIND: {
      static const char *items[ARRAY_LENGTH(s_kinds)];
      for (size_t i = 0; i < ARRAY_LENGTH(s_kinds); i++) {
        items[i] = s_kinds[i].name;
      }
      pick_choice("Repeat", items, ARRAY_LENGTH(s_kinds), prv_kind_index(rule), prv_kind_picked,
                  NULL, r);
      break;
    }
    case R_DATE:
      pick_date("Date", rule->date, prv_date_picked, r);
      break;
    case R_IN_DAYS: {
      int32_t delta = tt_date_is_set(rule->date) ? tt_date_to_days(rule->date) - prv_today() : 1;
      pick_number("In how many days", delta < 0 ? 1 : delta, 0, 3650, "days", prv_in_days_picked,
                  r);
      break;
    }
    case R_MDAY:
      pick_number(rule->mday < 0 ? "Days from the end" : "Day of the month",
                  rule->mday < 0 ? -rule->mday : rule->mday, 1, 31,
                  rule->mday < 0 ? "1 = last day" : "31 = last day", prv_mday_picked, r);
      break;
    case R_FROM_END:
      rule->mday = (int8_t)(rule->mday < 0 ? 1 : -1);
      prv_refresh(r);
      break;
    case R_NTH: {
      static const char *items[] = {"1st", "2nd", "3rd", "4th", "5th", "Last", "2nd to last"};
      static const int8_t values[] = {1, 2, 3, 4, 5, -1, -2};
      int selected = 0;
      for (int i = 0; i < 7; i++) {
        if (values[i] == rule->nth) {
          selected = i;
        }
      }
      pick_choice("Which one", items, 7, selected, prv_nth_picked, NULL, r);
      break;
    }
    case R_WDAY:
      pick_choice("Weekday", s_wday_names, 7, rule->wday, prv_wday_picked, NULL, r);
      break;
    case R_MONTH: {
      static const char *const months[12] = {"January", "February", "March",     "April",
                                             "May",     "June",     "July",      "August",
                                             "September", "October", "November", "December"};
      pick_choice("Month", months, 12, rule->month - 1, prv_month_picked, NULL, r);
      break;
    }
    case R_YMDAY:
      pick_number("Day", rule->mday, 1, tt_days_in_month(2024, rule->month), NULL,
                  prv_ymday_picked, r);
      break;
    case R_YDAY:
      pick_number("Day of the year", rule->yday, 1, 366, NULL, prv_yday_picked, r);
      break;
    case R_INTERVAL:
      pick_number("Every", rule->interval, 1, 999, prv_unit(rule, true), prv_interval_picked, r);
      break;
    case R_START: {
      static const char *items[] = {"Now", "Pick a date..."};
      pick_choice("Starting", items, 2, tt_date_is_set(rule->date) ? 1 : 0, prv_start_picked,
                  NULL, r);
      break;
    }
    default:
      break;
  }
}

static void prv_destroy(void *ctx) {
  free(ctx);
}

void repeat_show(TTRule *rule) {
  Repeat *r = malloc(sizeof(Repeat));
  if (!r) {
    return;
  }
  memset(r, 0, sizeof(*r));
  r->rule = rule;
  tt_rule_normalize(rule);
  prv_build(r);
  static const ListCallbacks callbacks = {
      .num_rows = prv_rows,
      .row = prv_row,
      .select = prv_select,
      .destroy = prv_destroy,
  };
  r->lw = list_window_push("When", &callbacks, r);
}
