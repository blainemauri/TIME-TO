// Recurrence rules: which local days an alarm selects.
//
// Canonical text is  kind[=spec][/interval][@anchor]  for example
//   once=2026-12-25        one specific date
//   daily                  every day
//   daily/3@2026-11-20     every 3 days counted from the anchor date
//   weekly=mo.we.fr        chosen weekdays
//   weekly=tu/2@2026-11-17 every other week
//   monthly=15             day of the month (monthly=-1 is the last day)
//   nth=2tu                the 2nd Tuesday of the month (nth=-1fr the last Friday)
//   yearly=03-14           a date every year
//   yday=256               the 256th day of the year
// The anchor is also a start date: nothing fires before it.
#include "tt_internal.h"

#include <string.h>

static const char *const s_kind_name[TT_RULE_KIND_COUNT] = {
    "once", "daily", "weekly", "monthly", "nth", "yearly", "yday"};

static int32_t prv_floor_div(int32_t a, int32_t b) {
  int32_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) {
    q--;
  }
  return q;
}

static int32_t prv_mod(int32_t a, int32_t b) {
  int32_t r = a % b;
  return r < 0 ? r + b : r;
}

void tt_rule_normalize(TTRule *r) {
  if (r->kind >= TT_RULE_KIND_COUNT) {
    r->kind = TT_RULE_DAILY;
  }
  if (r->interval < 1) {
    r->interval = 1;
  }
  r->dow_mask &= TT_DOW_ALL;
  if (r->kind == TT_RULE_WEEKLY && r->dow_mask == 0) {
    r->dow_mask = TT_DOW_ALL;
  }
  if (r->mday == 0) {
    r->mday = 1;
  }
  if (r->mday > 31) {
    r->mday = 31;
  }
  if (r->mday < -31) {
    r->mday = -31;
  }
  if (r->nth == 0) {
    r->nth = 1;
  }
  if (r->nth > 5) {
    r->nth = 5;
  }
  if (r->nth < -5) {
    r->nth = -5;
  }
  if (r->wday > 6) {
    r->wday = 0;
  }
  if (r->month < 1 || r->month > 12) {
    r->month = 1;
  }
  if (r->kind == TT_RULE_YEARLY) {
    if (r->mday < 1) {
      r->mday = 1;
    }
    int dim = tt_days_in_month(2024, r->month);  // a leap year, so Feb 29 survives
    if (r->mday > dim) {
      r->mday = (int8_t)dim;
    }
  }
  if (r->yday < 1) {
    r->yday = 1;
  }
  if (r->yday > 366) {
    r->yday = 366;
  }
  if (tt_date_is_set(r->date) && !tt_date_valid(r->date.y, r->date.m, r->date.d)) {
    r->date = TT_DATE_NONE;
  }
}

// ---------------------------------------------------------------- matching

//! The day of month a MONTHLY/NTH/YEARLY rule picks in (y, m), or 0 if none.
static int prv_day_in_month(const TTRule *r, int y, int m) {
  int dim = tt_days_in_month(y, m);
  if (r->kind == TT_RULE_NTH) {
    int32_t first = tt_days_from_civil(y, m, 1);
    int first_dow = tt_dow(first);
    if (r->nth > 0) {
      int d = 1 + prv_mod(r->wday - first_dow, 7) + (r->nth - 1) * 7;
      return d <= dim ? d : 0;
    }
    int last_dow = tt_dow(first + dim - 1);
    int d = dim - prv_mod(last_dow - r->wday, 7) + (r->nth + 1) * 7;
    return d >= 1 ? d : 0;
  }
  // MONTHLY and YEARLY: a day number, clamped so "31" means the last day of
  // short months and Feb 29 falls back to Feb 28.
  if (r->mday > 0) {
    return r->mday <= dim ? r->mday : dim;
  }
  int d = dim + 1 + r->mday;
  return d >= 1 ? d : 1;
}

int32_t tt_rule_next_day(const TTRule *r, int32_t from) {
  const bool anchored = tt_date_is_set(r->date);
  const int32_t anchor = anchored ? tt_date_to_days(r->date) : 0;
  const int32_t n = r->interval < 1 ? 1 : r->interval;

  if (r->kind == TT_RULE_ONCE) {
    return (anchored && anchor >= from) ? anchor : TT_NO_DAY;
  }
  int32_t start = (anchored && anchor > from) ? anchor : from;

  switch (r->kind) {
    case TT_RULE_DAILY: {
      if (n <= 1) {
        return start;
      }
      int32_t k = prv_floor_div(start - anchor + n - 1, n);
      return anchor + k * n;
    }
    case TT_RULE_WEEKLY: {
      uint8_t mask = (r->dow_mask & TT_DOW_ALL) ? (r->dow_mask & TT_DOW_ALL) : TT_DOW_ALL;
      int32_t base_monday = anchor - tt_dow(anchor);
      int32_t d = start;
      for (int guard = 0; guard < 40; guard++) {
        int32_t week = prv_floor_div(d - base_monday, 7);
        int32_t phase = prv_mod(week, n);
        if (phase != 0) {
          d = base_monday + (week + (n - phase)) * 7;  // Monday of the next week that counts
          continue;
        }
        if (mask & (1 << tt_dow(d))) {
          return d;
        }
        d++;
      }
      return TT_NO_DAY;
    }
    case TT_RULE_MONTHLY:
    case TT_RULE_NTH: {
      TTDate s = tt_civil_from_days(start);
      int32_t mi = s.y * 12 + (s.m - 1);
      int32_t ami = anchored ? (r->date.y * 12 + (r->date.m - 1)) : 0;
      mi += prv_mod(ami - mi, n);  // first month index >= mi that is in phase
      for (int guard = 0; guard < 120; guard++, mi += n) {
        int y = mi / 12;
        int m = mi % 12 + 1;
        if (y > 2199) {
          break;
        }
        int dd = prv_day_in_month(r, y, m);
        if (dd == 0) {
          continue;
        }
        int32_t cand = tt_days_from_civil(y, m, dd);
        if (cand >= start) {
          return cand;
        }
      }
      return TT_NO_DAY;
    }
    case TT_RULE_YEARLY:
    case TT_RULE_YDAY: {
      TTDate s = tt_civil_from_days(start);
      int32_t y = s.y;
      int32_t ay = anchored ? r->date.y : 0;
      y += prv_mod(ay - y, n);
      for (int guard = 0; guard < 6; guard++, y += n) {
        if (y > 2199) {
          break;
        }
        int32_t cand;
        if (r->kind == TT_RULE_YEARLY) {
          cand = tt_days_from_civil(y, r->month, prv_day_in_month(r, y, r->month));
        } else {
          int len = tt_is_leap(y) ? 366 : 365;
          int yd = r->yday > len ? len : r->yday;
          cand = tt_days_from_civil(y, 1, 1) + yd - 1;
        }
        if (cand >= start) {
          return cand;
        }
      }
      return TT_NO_DAY;
    }
    default:
      return TT_NO_DAY;
  }
}

bool tt_rule_matches(const TTRule *r, int32_t day) {
  return tt_rule_next_day(r, day) == day;
}

// ---------------------------------------------------------------- text

static int prv_day_code(const char *s) {
  for (int i = 0; i < 7; i++) {
    if (tt_lower(s[0]) == tt_wday_code[i][0] && tt_lower(s[1]) == tt_wday_code[i][1]) {
      return i;
    }
  }
  return -1;
}

bool tt_rule_parse(const char *text, TTRule *out) {
  TTRule r;
  memset(&r, 0, sizeof(r));
  r.interval = 1;
  r.mday = 1;
  r.nth = 1;
  r.month = 1;
  r.yday = 1;

  const char *s = text;
  while (*s == ' ') {
    s++;
  }
  if (*s == '\0') {
    return false;
  }

  // Shorthands that are easy to type by hand.
  if (*s >= '0' && *s <= '9') {
    r.kind = TT_RULE_ONCE;
    if (!tt_parse_date(s, &r.date)) {
      return false;
    }
    *out = r;
    return true;
  }
  int len;
  if ((len = tt_starts(s, "weekdays"))) {
    r.kind = TT_RULE_WEEKLY;
    r.dow_mask = TT_DOW_WEEKDAYS;
    s += len;
  } else if ((len = tt_starts(s, "weekends"))) {
    r.kind = TT_RULE_WEEKLY;
    r.dow_mask = TT_DOW_WEEKEND;
    s += len;
  } else {
    int kind = -1;
    for (int i = 0; i < TT_RULE_KIND_COUNT; i++) {
      if ((len = tt_starts(s, s_kind_name[i]))) {
        kind = i;
        s += len;
        break;
      }
    }
    if (kind < 0) {
      return false;
    }
    r.kind = (uint8_t)kind;
    if (*s == '=') {
      s++;
      int32_t v;
      switch (r.kind) {
        case TT_RULE_ONCE: {
          if (!tt_parse_date(s, &r.date)) {
            return false;
          }
          while (*s && *s != '/' && *s != '@') {
            s++;
          }
          break;
        }
        case TT_RULE_WEEKLY: {
          while (*s && *s != '/' && *s != '@') {
            int code = prv_day_code(s);
            if (code >= 0) {
              r.dow_mask |= (uint8_t)(1 << code);
              s += 2;
            } else if (*s == '.' || *s == '+' || *s == ' ') {
              s++;
            } else {
              return false;
            }
          }
          break;
        }
        case TT_RULE_MONTHLY:
          if (!tt_scan_int(&s, &v) || v == 0 || v > 31 || v < -31) {
            return false;
          }
          r.mday = (int8_t)v;
          break;
        case TT_RULE_NTH: {
          if (!tt_scan_int(&s, &v) || v == 0 || v > 5 || v < -5) {
            return false;
          }
          r.nth = (int8_t)v;
          int code = prv_day_code(s);
          if (code < 0) {
            return false;
          }
          r.wday = (uint8_t)code;
          s += 2;
          break;
        }
        case TT_RULE_YEARLY: {
          int32_t m, d;
          if (!tt_scan_int(&s, &m) || *s != '-') {
            return false;
          }
          s++;
          if (!tt_scan_int(&s, &d) || m < 1 || m > 12 || d < 1 ||
              d > tt_days_in_month(2024, m)) {
            return false;
          }
          r.month = (uint8_t)m;
          r.mday = (int8_t)d;
          break;
        }
        case TT_RULE_YDAY:
          if (!tt_scan_int(&s, &v) || v < 1 || v > 366) {
            return false;
          }
          r.yday = (uint16_t)v;
          break;
        default:
          break;
      }
    } else if (r.kind == TT_RULE_ONCE) {
      return false;  // "once" needs a date
    }
  }

  if (*s == '/') {
    s++;
    int32_t v;
    if (!tt_scan_int(&s, &v) || v < 1 || v > 65535) {
      return false;
    }
    r.interval = (uint16_t)v;
  }
  if (*s == '@') {
    s++;
    if (r.kind != TT_RULE_ONCE && !tt_parse_date(s, &r.date)) {
      return false;
    }
  }
  tt_rule_normalize(&r);
  *out = r;
  return true;
}

int tt_rule_format(const TTRule *r, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  uint8_t kind = r->kind < TT_RULE_KIND_COUNT ? r->kind : TT_RULE_DAILY;
  pos = tt_app(out, cap, pos, s_kind_name[kind]);
  switch (kind) {
    case TT_RULE_ONCE: {
      char date[12];
      tt_format_date(r->date, date, sizeof(date));
      pos = tt_app_char(out, cap, pos, '=');
      return tt_app(out, cap, pos, date);
    }
    case TT_RULE_WEEKLY: {
      uint8_t mask = (r->dow_mask & TT_DOW_ALL) ? (r->dow_mask & TT_DOW_ALL) : TT_DOW_ALL;
      pos = tt_app_char(out, cap, pos, '=');
      bool first = true;
      for (int i = 0; i < 7; i++) {
        if (mask & (1 << i)) {
          if (!first) {
            pos = tt_app_char(out, cap, pos, '.');
          }
          pos = tt_app(out, cap, pos, tt_wday_code[i]);
          first = false;
        }
      }
      break;
    }
    case TT_RULE_MONTHLY:
      pos = tt_app_char(out, cap, pos, '=');
      pos = tt_app_int(out, cap, pos, r->mday);
      break;
    case TT_RULE_NTH:
      pos = tt_app_char(out, cap, pos, '=');
      pos = tt_app_int(out, cap, pos, r->nth);
      pos = tt_app(out, cap, pos, tt_wday_code[r->wday % 7]);
      break;
    case TT_RULE_YEARLY:
      pos = tt_app_char(out, cap, pos, '=');
      pos = tt_app_pad(out, cap, pos, r->month, 2);
      pos = tt_app_char(out, cap, pos, '-');
      pos = tt_app_pad(out, cap, pos, r->mday, 2);
      break;
    case TT_RULE_YDAY:
      pos = tt_app_char(out, cap, pos, '=');
      pos = tt_app_int(out, cap, pos, r->yday);
      break;
    default:
      break;
  }
  if (r->interval > 1) {
    pos = tt_app_char(out, cap, pos, '/');
    pos = tt_app_int(out, cap, pos, r->interval);
  }
  if (tt_date_is_set(r->date)) {
    char date[12];
    tt_format_date(r->date, date, sizeof(date));
    pos = tt_app_char(out, cap, pos, '@');
    pos = tt_app(out, cap, pos, date);
  }
  return pos;
}

static int prv_app_ordinal(char *out, size_t cap, int pos, int n) {
  pos = tt_app_int(out, cap, pos, n);
  int tens = (n / 10) % 10;
  int ones = n % 10;
  const char *suffix = "th";
  if (tens != 1) {
    if (ones == 1) {
      suffix = "st";
    } else if (ones == 2) {
      suffix = "nd";
    } else if (ones == 3) {
      suffix = "rd";
    }
  }
  return tt_app(out, cap, pos, suffix);
}

int tt_rule_describe(const TTRule *r, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  const int n = r->interval < 1 ? 1 : r->interval;
  switch (r->kind) {
    case TT_RULE_ONCE: {
      if (!tt_date_is_set(r->date)) {
        return tt_app(out, cap, pos, "Once");
      }
      pos = tt_app(out, cap, pos, tt_wday_abbr[tt_dow(tt_date_to_days(r->date))]);
      pos = tt_app_char(out, cap, pos, ' ');
      pos = tt_app_int(out, cap, pos, r->date.d);
      pos = tt_app_char(out, cap, pos, ' ');
      pos = tt_app(out, cap, pos, tt_month_abbr[(r->date.m + 11) % 12]);
      pos = tt_app_char(out, cap, pos, ' ');
      return tt_app_int(out, cap, pos, r->date.y);
    }
    case TT_RULE_DAILY:
      if (n == 1) {
        return tt_app(out, cap, pos, "Every day");
      }
      pos = tt_app(out, cap, pos, "Every ");
      pos = tt_app_int(out, cap, pos, n);
      return tt_app(out, cap, pos, " days");
    case TT_RULE_WEEKLY: {
      uint8_t mask = (r->dow_mask & TT_DOW_ALL) ? (r->dow_mask & TT_DOW_ALL) : TT_DOW_ALL;
      if (n > 1) {
        pos = tt_app(out, cap, pos, "Every ");
        pos = tt_app_int(out, cap, pos, n);
        pos = tt_app(out, cap, pos, " wks: ");
      }
      if (mask == TT_DOW_ALL) {
        return tt_app(out, cap, pos, n > 1 ? "all days" : "Every day");
      }
      if (mask == TT_DOW_WEEKDAYS) {
        return tt_app(out, cap, pos, "Weekdays");
      }
      if (mask == TT_DOW_WEEKEND) {
        return tt_app(out, cap, pos, "Weekends");
      }
      bool first = true;
      for (int i = 0; i < 7; i++) {
        if (mask & (1 << i)) {
          if (!first) {
            pos = tt_app_char(out, cap, pos, ' ');
          }
          pos = tt_app(out, cap, pos, tt_wday_abbr[i]);
          first = false;
        }
      }
      return pos;
    }
    case TT_RULE_MONTHLY:
      if (r->mday == -1) {
        pos = tt_app(out, cap, pos, "Last day");
      } else if (r->mday < 0) {
        pos = prv_app_ordinal(out, cap, pos, -r->mday);
        pos = tt_app(out, cap, pos, "-last day");
      } else {
        pos = prv_app_ordinal(out, cap, pos, r->mday);
      }
      if (n == 1) {
        return tt_app(out, cap, pos, r->mday < 0 ? " of month" : " of each month");
      }
      pos = tt_app(out, cap, pos, ", every ");
      pos = tt_app_int(out, cap, pos, n);
      return tt_app(out, cap, pos, " mo");
    case TT_RULE_NTH:
      if (r->nth == -1) {
        pos = tt_app(out, cap, pos, "Last");
      } else if (r->nth < 0) {
        pos = prv_app_ordinal(out, cap, pos, -r->nth);
        pos = tt_app(out, cap, pos, "-last");
      } else {
        pos = prv_app_ordinal(out, cap, pos, r->nth);
      }
      pos = tt_app_char(out, cap, pos, ' ');
      pos = tt_app(out, cap, pos, tt_wday_abbr[r->wday % 7]);
      if (n == 1) {
        return tt_app(out, cap, pos, " monthly");
      }
      pos = tt_app(out, cap, pos, ", every ");
      pos = tt_app_int(out, cap, pos, n);
      return tt_app(out, cap, pos, " mo");
    case TT_RULE_YEARLY:
      pos = tt_app_int(out, cap, pos, r->mday);
      pos = tt_app_char(out, cap, pos, ' ');
      pos = tt_app(out, cap, pos, tt_month_abbr[(r->month + 11) % 12]);
      if (n == 1) {
        return tt_app(out, cap, pos, " yearly");
      }
      pos = tt_app(out, cap, pos, ", every ");
      pos = tt_app_int(out, cap, pos, n);
      return tt_app(out, cap, pos, " yrs");
    case TT_RULE_YDAY:
      pos = tt_app(out, cap, pos, "Day ");
      pos = tt_app_int(out, cap, pos, r->yday);
      if (n == 1) {
        return tt_app(out, cap, pos, " of year");
      }
      pos = tt_app(out, cap, pos, ", every ");
      pos = tt_app_int(out, cap, pos, n);
      return tt_app(out, cap, pos, " yrs");
    default:
      return tt_app(out, cap, pos, "?");
  }
}
