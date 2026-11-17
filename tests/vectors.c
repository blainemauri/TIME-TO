// Prints test vectors from the C core for tests/check_js.js to compare with
// src/common/ttrows.js:  R <tab> rule <tab> from_day <tab> next_day|none
//                        A <tab> row  <tab> after    <tab> next (0 = none)
#include "../src/c/core/tt_core.h"

#include <stdio.h>
#include <string.h>

static uint32_t s_rng = 987654321u;
static uint32_t rnd(uint32_t n) {
  s_rng = s_rng * 1664525u + 1013904223u;
  return (s_rng >> 8) % n;
}

static TTRule random_rule(void) {
  TTRule r;
  memset(&r, 0, sizeof(r));
  r.kind = (uint8_t)rnd(TT_RULE_KIND_COUNT);
  r.interval = (uint16_t)(rnd(3) == 0 ? 1 + rnd(5) : 1);
  r.dow_mask = (uint8_t)(1 + rnd(127));
  r.mday = (int8_t)((int)rnd(63) - 31);
  r.nth = (int8_t)((int)rnd(11) - 5);
  r.wday = (uint8_t)rnd(7);
  r.month = (uint8_t)(1 + rnd(12));
  r.yday = (uint16_t)(1 + rnd(366));
  if (r.kind == TT_RULE_ONCE || rnd(2)) {
    r.date = tt_civil_from_days(tt_days_from_civil(2026, 1, 1) + (int32_t)rnd(900));
  }
  tt_rule_normalize(&r);
  return r;
}

int main(void) {
  char text[TT_ROW_MAX];
  for (int i = 0; i < 3000; i++) {
    TTRule r = random_rule();
    tt_rule_format(&r, text, sizeof(text));
    int32_t from = tt_days_from_civil(2026, 1, 1) + (int32_t)rnd(1200);
    int32_t next = tt_rule_next_day(&r, from);
    if (next == TT_NO_DAY) {
      printf("R\t%s\t%d\tnone\n", text, (int)from);
    } else {
      printf("R\t%s\t%d\t%d\n", text, (int)from, (int)next);
    }
  }
  for (int i = 0; i < 2000; i++) {
    TTAlarm a;
    tt_alarm_init(&a);
    a.id = 1 + rnd(100000);
    a.rule = random_rule();
    a.minute = (uint16_t)rnd(1440);
    if (rnd(3) == 0) {
      a.every = (uint16_t)(5 + rnd(300));
      a.last = (uint16_t)(rnd(2) ? 1439 : a.minute + rnd(1440 - a.minute));
    }
    if (rnd(4) == 0) {
      a.until = tt_civil_from_days(tt_days_from_civil(2026, 6, 1) + (int32_t)rnd(700));
    }
    tt_alarm_format(&a, text, sizeof(text));
    TTAlarm back;
    tt_alarm_parse(text, &back);
    time_t after = tt_local_to_epoch(tt_days_from_civil(2026, 1, 1) + (int32_t)rnd(1000),
                                     (int)rnd(1440)) + (time_t)rnd(60);
    printf("A\t%s\t%ld\t%ld\n", text, (long)after, (long)tt_alarm_next_occurrence(&back, after));
  }
  return 0;
}
