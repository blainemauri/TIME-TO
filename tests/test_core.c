// Host unit tests for src/c/core. Build and run with tests/run.sh.
#define _POSIX_C_SOURCE 200809L
#include "../src/c/core/tt_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_checks = 0;
static int s_failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    s_checks++;                                                          \
    if (!(cond)) {                                                       \
      s_failures++;                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                    \
  } while (0)

#define CHECK_STR(actual, expected)                                      \
  do {                                                                   \
    s_checks++;                                                          \
    if (strcmp((actual), (expected)) != 0) {                             \
      s_failures++;                                                      \
      printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
    }                                                                    \
  } while (0)

#define CHECK_INT(actual, expected)                                      \
  do {                                                                   \
    s_checks++;                                                          \
    long long a_ = (long long)(actual), e_ = (long long)(expected);     \
    if (a_ != e_) {                                                      \
      s_failures++;                                                      \
      printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #actual, a_, e_); \
    }                                                                    \
  } while (0)

static int32_t D(int y, int m, int d) { return tt_days_from_civil(y, m, d); }

static time_t T(int y, int m, int d, int hh, int mm) {
  return tt_local_to_epoch(D(y, m, d), hh * 60 + mm);
}

static const char *fmt_time(time_t t) {
  static char buf[4][40];
  static int slot = 0;
  char *out = buf[slot++ % 4];
  if (t == 0) {
    strcpy(out, "none");
    return out;
  }
  struct tm *tm = localtime(&t);
  strftime(out, 40, "%Y-%m-%d %H:%M", tm);
  return out;
}

static TTRule rule(const char *text) {
  TTRule r;
  memset(&r, 0, sizeof(r));
  if (!tt_rule_parse(text, &r)) {
    printf("FAIL: rule \"%s\" did not parse\n", text);
    s_failures++;
  }
  return r;
}

static TTAlarm alarm(const char *row) {
  TTAlarm a;
  tt_alarm_init(&a);
  if (!tt_alarm_parse(row, &a)) {
    printf("FAIL: row \"%s\" did not parse\n", row);
    s_failures++;
  }
  return a;
}

// ---------------------------------------------------------------- dates

static void test_dates(void) {
  CHECK_INT(D(1970, 1, 1), 0);
  CHECK_INT(D(2000, 3, 1), 11017);
  CHECK_INT(tt_dow(D(1970, 1, 1)), 3);    // Thursday
  CHECK_INT(tt_dow(D(2026, 11, 17)), 1);  // Tuesday
  CHECK_INT(tt_dow(D(2024, 2, 29)), 3);   // Thursday
  CHECK_INT(tt_days_in_month(2024, 2), 29);
  CHECK_INT(tt_days_in_month(2100, 2), 28);
  CHECK_INT(tt_days_in_month(2000, 2), 29);
  CHECK(tt_date_valid(2028, 2, 29));
  CHECK(!tt_date_valid(2027, 2, 29));
  CHECK(!tt_date_valid(2027, 13, 1));

  // Every day from 1970 to 2199 round-trips and advances the weekday by one.
  int32_t start = D(1970, 1, 1);
  int32_t end = D(2199, 12, 31);
  int bad = 0;
  int prev_dow = tt_dow(start - 1);
  for (int32_t d = start; d <= end; d++) {
    TTDate c = tt_civil_from_days(d);
    if (D(c.y, c.m, c.d) != d || !tt_date_valid(c.y, c.m, c.d)) {
      bad++;
    }
    int dow = tt_dow(d);
    if (dow != (prev_dow + 1) % 7) {
      bad++;
    }
    prev_dow = dow;
  }
  CHECK_INT(bad, 0);
}

// ---------------------------------------------------------------- text

static void test_text(void) {
  char buf[64];
  uint32_t mins = 0;
  uint16_t clock = 0;

  tt_format_duration(3, buf, sizeof(buf));
  CHECK_STR(buf, "3m");
  tt_format_duration(90, buf, sizeof(buf));
  CHECK_STR(buf, "1h 30m");
  tt_format_duration(117 * 60, buf, sizeof(buf));
  CHECK_STR(buf, "117h");
  tt_format_duration(2880, buf, sizeof(buf));
  CHECK_STR(buf, "2d");
  tt_format_duration(60, buf, sizeof(buf));
  CHECK_STR(buf, "1h");

  CHECK(tt_parse_duration("3", &mins) && mins == 3);
  CHECK(tt_parse_duration("30m", &mins) && mins == 30);
  CHECK(tt_parse_duration("117h", &mins) && mins == 7020);
  CHECK(tt_parse_duration("1h30m", &mins) && mins == 90);
  CHECK(tt_parse_duration("1h 30m", &mins) && mins == 90);
  CHECK(tt_parse_duration("2 days", &mins) && mins == 2880);
  CHECK(tt_parse_duration("1w", &mins) && mins == 10080);
  CHECK(tt_parse_duration("90 minutes", &mins) && mins == 90);
  CHECK(!tt_parse_duration("", &mins));
  CHECK(!tt_parse_duration("soon", &mins));
  CHECK(tt_parse_duration("9999d", &mins) && mins == TT_SNOOZE_MAX_MIN);

  tt_format_relative(0, buf, sizeof(buf));
  CHECK_STR(buf, "now");
  tt_format_relative(5 * 60, buf, sizeof(buf));
  CHECK_STR(buf, "in 5m");
  tt_format_relative(3 * 3600 + 12 * 60 + 40, buf, sizeof(buf));
  CHECK_STR(buf, "in 3h 12m");
  tt_format_relative(2 * 86400 + 4 * 3600, buf, sizeof(buf));
  CHECK_STR(buf, "in 2d 4h");
  tt_format_relative(-300, buf, sizeof(buf));
  CHECK_STR(buf, "5m ago");
  tt_format_relative(40 * 86400, buf, sizeof(buf));
  CHECK_STR(buf, "in 40d");

  tt_format_clock(7 * 60 + 5, true, buf, sizeof(buf));
  CHECK_STR(buf, "07:05");
  tt_format_clock(0, false, buf, sizeof(buf));
  CHECK_STR(buf, "12:00am");
  tt_format_clock(13 * 60 + 30, false, buf, sizeof(buf));
  CHECK_STR(buf, "1:30pm");
  CHECK(tt_parse_clock("07:30", &clock) && clock == 450);
  CHECK(tt_parse_clock("7:30pm", &clock) && clock == 1170);
  CHECK(tt_parse_clock("12am", &clock) && clock == 0);
  CHECK(tt_parse_clock("0730", &clock) && clock == 450);
  CHECK(tt_parse_clock("23:59", &clock) && clock == 1439);
  CHECK(!tt_parse_clock("24:00", &clock));
  CHECK(!tt_parse_clock("time", &clock));

  TTDate date;
  CHECK(tt_parse_date("2026-12-25", &date) && date.y == 2026 && date.m == 12 && date.d == 25);
  CHECK(!tt_parse_date("2027-02-29", &date));
  CHECK(!tt_parse_date("tomorrow", &date));
  tt_format_date(date, buf, sizeof(buf));
  CHECK_STR(buf, "2026-12-25");

  // CSV fields.
  const char *row = "a, b ,\"c,d\",\"say \"\"hi\"\"\",,last";
  const char *p = row;
  CHECK(tt_csv_field(&p, buf, sizeof(buf)));
  CHECK_STR(buf, "a");
  CHECK(tt_csv_field(&p, buf, sizeof(buf)));
  CHECK_STR(buf, "b");
  CHECK(tt_csv_field(&p, buf, sizeof(buf)));
  CHECK_STR(buf, "c,d");
  CHECK(tt_csv_field(&p, buf, sizeof(buf)));
  CHECK_STR(buf, "say \"hi\"");
  CHECK(tt_csv_field(&p, buf, sizeof(buf)));
  CHECK_STR(buf, "");
  CHECK(tt_csv_field(&p, buf, sizeof(buf)));
  CHECK_STR(buf, "last");
  CHECK(!tt_csv_field(&p, buf, sizeof(buf)));

  char out[64];
  int n = tt_csv_put(out, sizeof(out), 0, "plain", false);
  n = tt_csv_put(out, sizeof(out), n, "a,b", true);
  n = tt_csv_put(out, sizeof(out), n, "q\"q", true);
  CHECK_STR(out, "plain,\"a,b\",\"q\"\"q\"");
  // Never writes past a tiny buffer.
  char tiny[6];
  tt_csv_put(tiny, sizeof(tiny), 0, "a,very,long,field", false);
  CHECK(strlen(tiny) < sizeof(tiny));
}

// ---------------------------------------------------------------- rules

static void check_roundtrip(const char *text, const char *describe) {
  char buf[64];
  TTRule r = rule(text);
  tt_rule_format(&r, buf, sizeof(buf));
  CHECK_STR(buf, text);
  tt_rule_describe(&r, buf, sizeof(buf));
  CHECK_STR(buf, describe);
}

static void test_rule_text(void) {
  check_roundtrip("once=2026-12-25", "Fri 25 Dec 2026");
  check_roundtrip("daily", "Every day");
  check_roundtrip("daily/3@2026-11-20", "Every 3 days");
  check_roundtrip("weekly=mo.we.fr", "Mon Wed Fri");
  check_roundtrip("weekly=mo.tu.we.th.fr", "Weekdays");
  check_roundtrip("weekly=sa.su", "Weekends");
  check_roundtrip("weekly=tu/2@2026-11-17", "Every 2 wks: Tue");
  check_roundtrip("monthly=15", "15th of each month");
  check_roundtrip("monthly=-1", "Last day of month");
  check_roundtrip("monthly=1/3@2026-12-01", "1st, every 3 mo");
  check_roundtrip("nth=2tu", "2nd Tue monthly");
  check_roundtrip("nth=-1fr", "Last Fri monthly");
  check_roundtrip("yearly=03-14", "14 Mar yearly");
  check_roundtrip("yday=256", "Day 256 of year");

  // Hand-typed shorthands normalise to the canonical form.
  char buf[64];
  TTRule r = rule("2026-12-25");
  tt_rule_format(&r, buf, sizeof(buf));
  CHECK_STR(buf, "once=2026-12-25");
  r = rule("weekdays");
  tt_rule_format(&r, buf, sizeof(buf));
  CHECK_STR(buf, "weekly=mo.tu.we.th.fr");
  r = rule("weekends");
  tt_rule_format(&r, buf, sizeof(buf));
  CHECK_STR(buf, "weekly=sa.su");
  r = rule("WEEKLY=MoWeFr");
  tt_rule_format(&r, buf, sizeof(buf));
  CHECK_STR(buf, "weekly=mo.we.fr");
  r = rule("weekly");
  tt_rule_format(&r, buf, sizeof(buf));
  CHECK_STR(buf, "weekly=mo.tu.we.th.fr.sa.su");

  TTRule junk;
  CHECK(!tt_rule_parse("", &junk));
  CHECK(!tt_rule_parse("sometimes", &junk));
  CHECK(!tt_rule_parse("once", &junk));
  CHECK(!tt_rule_parse("once=2027-02-30", &junk));
  CHECK(!tt_rule_parse("monthly=0", &junk));
  CHECK(!tt_rule_parse("monthly=40", &junk));
  CHECK(!tt_rule_parse("nth=6mo", &junk));
  CHECK(!tt_rule_parse("nth=2xx", &junk));
  CHECK(!tt_rule_parse("yearly=13-01", &junk));
  CHECK(!tt_rule_parse("yearly=02-30", &junk));
  CHECK(!tt_rule_parse("yday=367", &junk));
  CHECK(!tt_rule_parse("daily/0", &junk));
  CHECK(!tt_rule_parse("weekly=mo.xx", &junk));
}

static void test_rule_days(void) {
  TTRule r;

  r = rule("once=2026-12-25");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 12, 25));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 12, 25)), D(2026, 12, 25));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 12, 26)), TT_NO_DAY);

  r = rule("daily");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 11, 17));

  r = rule("daily/3@2026-11-20");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 11, 20));  // not before the anchor
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 21)), D(2026, 11, 23));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 23)), D(2026, 11, 23));
  CHECK_INT(tt_rule_next_day(&r, D(2027, 1, 1)), D(2027, 1, 1));  // 42 days after the anchor

  r = rule("weekly=mo.we.fr");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 11, 18));  // Tue -> Wed
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 21)), D(2026, 11, 23));  // Sat -> Mon

  r = rule("weekly=tu/2@2026-11-17");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 11, 17));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 18)), D(2026, 12, 1));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 12, 2)), D(2026, 12, 15));

  r = rule("monthly=31");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 11, 30));  // clamps to a 30-day month
  CHECK_INT(tt_rule_next_day(&r, D(2027, 2, 1)), D(2027, 2, 28));
  CHECK_INT(tt_rule_next_day(&r, D(2028, 2, 1)), D(2028, 2, 29));

  r = rule("monthly=-1");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 11, 30));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 12, 1)), D(2026, 12, 31));
  r = rule("monthly=-3");
  CHECK_INT(tt_rule_next_day(&r, D(2027, 2, 1)), D(2027, 2, 26));

  r = rule("monthly=15/3@2026-12-01");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 1)), D(2026, 12, 15));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 12, 16)), D(2027, 3, 15));

  r = rule("nth=2tu");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 1)), D(2026, 11, 10));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 11)), D(2026, 12, 8));
  r = rule("nth=-1fr");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 1)), D(2026, 11, 27));
  r = rule("nth=5mo");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 1)), D(2026, 11, 30));
  CHECK_INT(tt_rule_next_day(&r, D(2026, 12, 1)), D(2027, 3, 29));  // next month with 5 Mondays
  r = rule("nth=1mo");
  CHECK_INT(tt_rule_next_day(&r, D(2027, 2, 1)), D(2027, 2, 1));
  r = rule("nth=-5su");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 1)), D(2026, 11, 1));  // 5 Sundays in Nov 2026

  r = rule("yearly=03-14");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2027, 3, 14));
  CHECK_INT(tt_rule_next_day(&r, D(2027, 3, 14)), D(2027, 3, 14));
  r = rule("yearly=02-29");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2027, 2, 28));
  CHECK_INT(tt_rule_next_day(&r, D(2027, 3, 1)), D(2028, 2, 29));
  r = rule("yearly=02-29/4@2028-01-01");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2028, 2, 29));
  CHECK_INT(tt_rule_next_day(&r, D(2028, 3, 1)), D(2032, 2, 29));

  r = rule("yday=256");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2027, 9, 13));
  CHECK_INT(tt_rule_next_day(&r, D(2028, 1, 1)), D(2028, 9, 12));  // leap year shifts it
  r = rule("yday=366");
  CHECK_INT(tt_rule_next_day(&r, D(2026, 11, 17)), D(2026, 12, 31));
  CHECK_INT(tt_rule_next_day(&r, D(2028, 1, 1)), D(2028, 12, 31));
}

// An independent, deliberately naive matcher used to cross-check tt_rule_next_day.
static bool naive_match(const TTRule *r, int32_t day) {
  TTDate c = tt_civil_from_days(day);
  int dim = tt_days_in_month(c.y, c.m);
  bool anchored = r->date.y != 0;
  int32_t anchor = anchored ? D(r->date.y, r->date.m, r->date.d) : 0;
  int n = r->interval;
  if (r->kind == TT_RULE_ONCE) {
    return anchored && day == anchor;
  }
  if (anchored && day < anchor) {
    return false;
  }
  switch (r->kind) {
    case TT_RULE_DAILY:
      return ((day - anchor) % n + n) % n == 0;
    case TT_RULE_WEEKLY: {
      // Count weeks by walking Mondays.
      int32_t anchor_monday = anchor;
      while (tt_dow(anchor_monday) != 0) {
        anchor_monday--;
      }
      int32_t monday = day;
      while (tt_dow(monday) != 0) {
        monday--;
      }
      long weeks = (monday - anchor_monday) / 7;
      if (((weeks % n) + n) % n != 0) {
        return false;
      }
      return (r->dow_mask >> tt_dow(day)) & 1;
    }
    case TT_RULE_MONTHLY: {
      long months = (c.y * 12L + c.m) - (anchored ? (r->date.y * 12L + r->date.m) : 1);
      if (((months % n) + n) % n != 0) {
        return false;
      }
      int want = r->mday > 0 ? (r->mday > dim ? dim : r->mday) : dim + 1 + r->mday;
      if (want < 1) {
        want = 1;
      }
      return c.d == want;
    }
    case TT_RULE_NTH: {
      long months = (c.y * 12L + c.m) - (anchored ? (r->date.y * 12L + r->date.m) : 1);
      if (((months % n) + n) % n != 0) {
        return false;
      }
      if (tt_dow(day) != r->wday) {
        return false;
      }
      int from_start = (c.d - 1) / 7 + 1;
      int from_end = (dim - c.d) / 7 + 1;
      return r->nth > 0 ? from_start == r->nth : from_end == -r->nth;
    }
    case TT_RULE_YEARLY: {
      long years = c.y - (anchored ? r->date.y : 0);
      if (((years % n) + n) % n != 0) {
        return false;
      }
      int mdim = tt_days_in_month(c.y, r->month);
      int want = r->mday > mdim ? mdim : r->mday;
      return c.m == r->month && c.d == want;
    }
    case TT_RULE_YDAY: {
      long years = c.y - (anchored ? r->date.y : 0);
      if (((years % n) + n) % n != 0) {
        return false;
      }
      int len = tt_is_leap(c.y) ? 366 : 365;
      int want = r->yday > len ? len : r->yday;
      return day - D(c.y, 1, 1) + 1 == want;
    }
    default:
      return false;
  }
}

static uint32_t s_rng = 12345;
static uint32_t rnd(uint32_t n) {
  s_rng = s_rng * 1664525u + 1013904223u;
  return (s_rng >> 8) % n;
}

static void test_rule_fuzz(void) {
  int mismatches = 0;
  for (int iter = 0; iter < 4000 && mismatches < 5; iter++) {
    TTRule r;
    memset(&r, 0, sizeof(r));
    r.kind = (uint8_t)(1 + rnd(TT_RULE_KIND_COUNT - 1));
    r.interval = (uint16_t)(rnd(3) == 0 ? 1 + rnd(5) : 1);
    r.dow_mask = (uint8_t)(1 + rnd(127));
    r.mday = (int8_t)((int)rnd(63) - 31);
    r.nth = (int8_t)((int)rnd(11) - 5);
    r.wday = (uint8_t)rnd(7);
    r.month = (uint8_t)(1 + rnd(12));
    r.yday = (uint16_t)(1 + rnd(366));
    if (rnd(2)) {
      r.date = tt_civil_from_days(D(2026, 1, 1) + (int32_t)rnd(900));
    }
    tt_rule_normalize(&r);

    int32_t from = D(2026, 1, 1) + (int32_t)rnd(1200);
    int32_t got = tt_rule_next_day(&r, from);
    int32_t want = TT_NO_DAY;
    for (int32_t d = from; d < from + 366 * 12; d++) {
      if (naive_match(&r, d)) {
        want = d;
        break;
      }
    }
    if (got != want) {
      char text[64];
      tt_rule_format(&r, text, sizeof(text));
      TTDate f = tt_civil_from_days(from);
      TTDate g = got == TT_NO_DAY ? TT_DATE_NONE : tt_civil_from_days(got);
      TTDate w = want == TT_NO_DAY ? TT_DATE_NONE : tt_civil_from_days(want);
      printf("FAIL fuzz: %s from %d-%02d-%02d got %d-%02d-%02d want %d-%02d-%02d\n", text, f.y,
             f.m, f.d, g.y, g.m, g.d, w.y, w.m, w.d);
      mismatches++;
    }
    // The canonical text must round-trip to the same rule behaviour.
    char text[64];
    TTRule back;
    tt_rule_format(&r, text, sizeof(text));
    if (!tt_rule_parse(text, &back) || tt_rule_next_day(&back, from) != got) {
      printf("FAIL fuzz round-trip: %s\n", text);
      mismatches++;
    }
  }
  s_checks++;
  if (mismatches) {
    s_failures++;
  }
}

// ---------------------------------------------------------------- alarm rows

static void test_alarm_rows(void) {
  char buf[TT_ROW_MAX];
  TTAlarm a = alarm("17,1,07:30,weekly=mo.we.fr,Wake up,3,30,Heartbeat,80,chime,ra,,,2027-01-31,ff5500");
  CHECK_INT(a.id, 17);
  CHECK(a.on);
  CHECK_INT(a.minute, 450);
  CHECK_INT(a.rule.kind, TT_RULE_WEEKLY);
  CHECK_STR(a.msg, "Wake up");
  CHECK_INT(a.snooze_a, 3);
  CHECK_INT(a.snooze_b, 30);
  CHECK_STR(a.vibe, "Heartbeat");
  CHECK_INT(a.strength, 80);
  CHECK_STR(a.sound, "chime");
  CHECK_INT(a.flags, TT_F_RAMP | TT_F_DONE_ARCHIVE);
  CHECK_INT(a.until.y, 2027);
  CHECK_INT(a.color, 0x1ff5500);
  tt_alarm_format(&a, buf, sizeof(buf));
  CHECK_STR(buf, "17,1,07:30,weekly=mo.we.fr,Wake up,3,30,Heartbeat,80,chime,ra,,,2027-01-31,ff5500");

  // The shortest useful row: just a time.
  a = alarm(",,06:15");
  CHECK_INT(a.id, 0);
  CHECK(a.on);
  CHECK_INT(a.minute, 375);
  CHECK_INT(a.rule.kind, TT_RULE_DAILY);
  tt_alarm_format(&a, buf, sizeof(buf));
  CHECK_STR(buf, "0,1,06:15,daily,,,,,,,,,,,");

  // Quoted message with commas and quotes, long snooze, repeats inside the day.
  a = alarm("4000000000,0,09:00,weekdays,\"Drink water, \"\"now\"\"\",117h,2d,,,none,,120,17:00");
  CHECK_INT(a.id, 4000000000u);
  CHECK(!a.on);
  CHECK_STR(a.msg, "Drink water, \"now\"");
  CHECK_INT(a.snooze_a, 7020);
  CHECK_INT(a.snooze_b, 2880);
  CHECK_INT(a.every, 120);
  CHECK_INT(a.last, 17 * 60);
  tt_alarm_format(&a, buf, sizeof(buf));
  CHECK_STR(buf, "4000000000,0,09:00,weekly=mo.tu.we.th.fr,\"Drink water, \"\"now\"\"\",7020,2880,,,none,,120,17:00,,");
  TTAlarm b = alarm(buf);
  CHECK_STR(b.msg, a.msg);
  CHECK_INT(b.every, 120);

  // A worst-case row always fits and still parses.
  tt_alarm_init(&a);
  a.id = 4294967295u;
  a.rule = rule("weekly=mo.tu.we.th.fr.sa.su/65535@2026-11-17");
  memset(a.msg, '"', sizeof(a.msg) - 1);
  a.msg[sizeof(a.msg) - 1] = '\0';
  a.snooze_a = a.snooze_b = TT_SNOOZE_MAX_MIN;
  strcpy(a.vibe, "0123456789abcdef");
  strcpy(a.sound, "0123456789abcdef");
  a.strength = 100;
  a.flags = TT_F_RAMP | TT_F_DONE_DELETE;
  a.every = 1439;
  a.last = 1438;
  a.minute = 1;
  a.until = (TTDate){2199, 12, 31};
  a.color = 0x1ffffff;
  int len = tt_alarm_format(&a, buf, sizeof(buf));
  CHECK(len < TT_ROW_MAX);
  CHECK_INT((int)strlen(buf), len);
  CHECK(tt_alarm_parse(buf, &b));
  CHECK_INT(b.id, a.id);
  CHECK_INT(b.color, a.color);
  CHECK_INT(b.until.y, 2199);
  CHECK(strlen(b.msg) > 20);

  TTAlarm junk;
  CHECK(!tt_alarm_parse("", &junk));
  CHECK(!tt_alarm_parse(tt_alarm_header, &junk));
  CHECK(!tt_alarm_parse("1,1,25:00,daily", &junk));
  CHECK(!tt_alarm_parse("1,1,07:00,whenever", &junk));
  CHECK(!tt_alarm_parse("x,1,07:00,daily", &junk));
}

// ---------------------------------------------------------------- next ring

static void test_next(void) {
  TTAlarm a = alarm("1,1,07:30,daily");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 6, 0))), "2026-11-17 07:30");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 7, 30))), "2026-11-18 07:30");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 7, 29))), "2026-11-17 07:30");

  a = alarm("1,1,23:59,once=2026-12-31");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 6, 0))), "2026-12-31 23:59");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 12, 31, 23, 59))), "none");
  CHECK(tt_alarm_finished(&a, T(2027, 1, 1, 0, 0)));
  CHECK(!tt_alarm_finished(&a, T(2026, 12, 1, 0, 0)));

  // Repeats inside the day: 09:00 then every 2h until 17:00, weekdays only.
  a = alarm("1,1,09:00,weekdays,Water,,,,,,,120,17:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 8, 0))), "2026-11-17 09:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 9, 0))), "2026-11-17 11:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 12, 59))), "2026-11-17 13:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 16, 59))), "2026-11-17 17:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 17, 0))), "2026-11-18 09:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 20, 17, 0))), "2026-11-23 09:00");
  // No end time means "until midnight".
  a = alarm("1,1,22:00,daily,,,,,,,,45");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 22, 50))), "2026-11-17 23:30");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 17, 23, 30))), "2026-11-18 22:00");

  // `until` is the last day it may fire.
  a = alarm("1,1,07:00,daily,,,,,,,,,,2026-11-19");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 19, 6, 0))), "2026-11-19 07:00");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, T(2026, 11, 19, 7, 0))), "none");

  // State: snooze wins, skip and handled push the next ring later, off never rings.
  a = alarm("1,1,07:30,daily");
  a.handled_until = T(2026, 11, 17, 6, 0);
  CHECK_STR(fmt_time(tt_alarm_next_fire(&a)), "2026-11-17 07:30");
  a.skip_until = T(2026, 11, 17, 7, 30);
  CHECK_STR(fmt_time(tt_alarm_next_fire(&a)), "2026-11-18 07:30");
  a.snooze_until = T(2026, 11, 17, 7, 33);
  CHECK_STR(fmt_time(tt_alarm_next_fire(&a)), "2026-11-17 07:33");
  a.on = false;
  CHECK_INT(tt_alarm_next_fire(&a), 0);

  // A 117-hour snooze on a daily alarm silences the days in between.
  a = alarm("1,1,07:30,daily");
  a.handled_until = T(2026, 11, 17, 7, 31);
  a.snooze_until = a.handled_until + 117 * 3600;
  CHECK_STR(fmt_time(tt_alarm_next_fire(&a)), "2026-11-22 04:31");
  a.handled_until = a.snooze_until;  // dismissed when it finally rang
  a.snooze_until = 0;
  CHECK_STR(fmt_time(tt_alarm_next_fire(&a)), "2026-11-22 07:30");
}

static void test_dst(void) {
  const char *tz = getenv("TZ");
  if (!tz || strcmp(tz, "America/New_York") != 0) {
    return;
  }
  // 2027-03-14 02:30 does not exist in New York; the alarm must still ring
  // once that day and resume its normal time the day after.
  TTAlarm a = alarm("1,1,02:30,daily");
  time_t t = tt_alarm_next_occurrence(&a, T(2027, 3, 13, 12, 0));
  CHECK(t > T(2027, 3, 14, 1, 59) && t <= T(2027, 3, 14, 3, 30));
  time_t t2 = tt_alarm_next_occurrence(&a, t);
  CHECK_STR(fmt_time(t2), "2027-03-15 02:30");
  // 2026-11-01 01:30 happens twice; it must ring exactly once.
  time_t f = tt_alarm_next_occurrence(&a, T(2026, 10, 31, 12, 0));
  CHECK_STR(fmt_time(f), "2026-11-01 02:30");
  a = alarm("1,1,01:30,daily");
  f = tt_alarm_next_occurrence(&a, T(2026, 10, 31, 12, 0));
  CHECK_STR(fmt_time(f), "2026-11-01 01:30");
  CHECK_STR(fmt_time(tt_alarm_next_occurrence(&a, f)), "2026-11-02 01:30");
  // The day after a clock change keeps the wall-clock time, not a 24h offset.
  a = alarm("1,1,07:00,daily");
  f = tt_alarm_next_occurrence(&a, T(2027, 3, 13, 7, 0));
  CHECK_STR(fmt_time(f), "2027-03-14 07:00");
  CHECK_INT(f - T(2027, 3, 13, 7, 0), 23 * 3600);
}

// ---------------------------------------------------------------- other rows

static void test_other_rows(void) {
  char buf[TT_ROW_MAX];
  TTPattern p;
  CHECK(tt_pattern_parse("Heartbeat,120 80 120 600", &p));
  CHECK_STR(p.name, "Heartbeat");
  CHECK_INT(p.n, 3);  // the trailing silence is dropped
  CHECK_INT(tt_pattern_total_ms(&p), 320);
  tt_pattern_format(&p, buf, sizeof(buf));
  CHECK_STR(buf, "Heartbeat,120 80 120");
  CHECK(tt_pattern_parse("\"My, pattern\",50000", &p));
  CHECK_STR(p.name, "My, pattern");
  CHECK_INT(p.seg[0], TT_PATTERN_MAX_MS);
  CHECK(!tt_pattern_parse("Empty,", &p));
  CHECK(!tt_pattern_parse("Bad,12 x", &p));
  CHECK(!tt_pattern_parse(",100", &p));
  // Too many segments are cut, never overflowed.
  char many[600] = "Long,";
  for (int i = 0; i < 80; i++) {
    strcat(many, "10 ");
  }
  CHECK(tt_pattern_parse(many, &p));
  CHECK(p.n <= TT_PATTERN_MAX_SEGS && p.n % 2 == 1);

  TTTune t;
  CHECK(tt_tune_parse("Chime,72:150 0:50 76:300", &t));
  CHECK_INT(t.n, 3);
  CHECK_INT(t.note[2].midi, 76);
  CHECK_INT(t.note[2].ms, 300);
  tt_tune_format(&t, buf, sizeof(buf));
  CHECK_STR(buf, "Chime,72:150 0:50 76:300");
  CHECK(!tt_tune_parse("Bad,200:100", &t));
  CHECK(!tt_tune_parse("Bad,60", &t));

  TTSettings s;
  tt_settings_init(&s);
  tt_settings_format(&s, buf, sizeof(buf));
  CHECK_STR(buf, "snooze_a=3;snooze_b=30;vibe=Pulse;strength=100;sound=none;volume=70;ring=60;"
                 "auto=5;done=archive;timeline=1;light=1");
  CHECK(tt_settings_parse("snooze_a=10;snooze_b=117h;vibe=SOS;strength=40;sound=chime;volume=20;"
                          "ring=90;auto=0;done=delete;timeline=0;light=0;future=ignored",
                          &s));
  CHECK_INT(s.snooze_a, 10);
  CHECK_INT(s.snooze_b, 7020);
  CHECK_STR(s.vibe, "SOS");
  CHECK_INT(s.strength, 40);
  CHECK_STR(s.sound, "chime");
  CHECK_INT(s.volume, 20);
  CHECK_INT(s.ring_secs, 90);
  CHECK_INT(s.auto_snooze_max, 0);
  CHECK_INT(s.done_action, TT_DONE_DELETE);
  CHECK(!s.timeline);
  CHECK(!s.light);
  CHECK(!tt_settings_parse("nothing=here", &s));
}

int main(void) {
  tzset();
  test_dates();
  test_text();
  test_rule_text();
  test_rule_days();
  test_rule_fuzz();
  test_alarm_rows();
  test_next();
  test_dst();
  test_other_rows();
  const char *tz = getenv("TZ");
  printf("%s: %d checks, %d failures\n", tz ? tz : "(default TZ)", s_checks, s_failures);
  return s_failures ? 1 : 0;
}
