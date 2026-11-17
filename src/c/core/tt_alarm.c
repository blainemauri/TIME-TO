// Alarms: the CSV row format and "when does this ring next".
//
// One alarm is one row of text with these columns, in this order:
//   id,on,time,rule,msg,snooze_a,snooze_b,vibe,strength,sound,flags,every,last,until,color
// Only `time` is required; anything left empty takes its default.
#include "tt_internal.h"

#include <string.h>

const char *const tt_alarm_header =
    "id,on,time,rule,msg,snooze_a,snooze_b,vibe,strength,sound,flags,every,last,until,color";

void tt_alarm_init(TTAlarm *a) {
  memset(a, 0, sizeof(*a));
  a->on = true;
  a->minute = 8 * 60;
  a->rule.kind = TT_RULE_DAILY;
  a->rule.interval = 1;
  a->rule.mday = 1;
  a->rule.nth = 1;
  a->rule.month = 1;
  a->rule.yday = 1;
  a->last = 1439;
}

static bool prv_parse_u32(const char *text, uint32_t *out) {
  const char *s = text;
  int32_t v;
  if (!tt_scan_int(&s, &v) || v < 0) {
    return false;
  }
  *out = (uint32_t)v;
  return true;
}

static int prv_hex(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = tt_lower(c);
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

bool tt_alarm_parse(const char *row, TTAlarm *out) {
  TTAlarm a;
  tt_alarm_init(&a);
  char f[TT_ROW_MAX];
  const char *p = row;
  uint32_t v;

  // id (may be empty: the caller hands out a fresh one)
  if (!tt_csv_field(&p, f, sizeof(f))) {
    return false;
  }
  if (f[0]) {
    // ids are unsigned 32-bit; parse by hand so large values survive
    uint32_t id = 0;
    for (const char *s = f; *s; s++) {
      if (*s < '0' || *s > '9') {
        return false;
      }
      id = id * 10 + (uint32_t)(*s - '0');
    }
    a.id = id;
  }
  // on
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    char c = tt_lower(f[0]);
    a.on = !(c == '0' || c == 'n' || c == 'f' || (c == 'o' && tt_lower(f[1]) == 'f'));
  }
  // time (required)
  if (!tt_csv_field(&p, f, sizeof(f)) || !tt_parse_clock(f, &a.minute)) {
    return false;
  }
  // rule
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    if (!tt_rule_parse(f, &a.rule)) {
      return false;
    }
  }
  // msg
  if (tt_csv_field(&p, f, sizeof(f))) {
    tt_copy(a.msg, sizeof(a.msg), f);
  }
  // snooze_a, snooze_b (minutes, or "2h" style)
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    tt_parse_duration(f, &a.snooze_a);
  }
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    tt_parse_duration(f, &a.snooze_b);
  }
  // vibe
  if (tt_csv_field(&p, f, sizeof(f))) {
    tt_copy(a.vibe, sizeof(a.vibe), f);
  }
  // strength
  if (tt_csv_field(&p, f, sizeof(f)) && f[0] && prv_parse_u32(f, &v)) {
    a.strength = (uint8_t)(v > 100 ? 100 : v);
  }
  // sound
  if (tt_csv_field(&p, f, sizeof(f))) {
    tt_copy(a.sound, sizeof(a.sound), f);
  }
  // flags
  if (tt_csv_field(&p, f, sizeof(f))) {
    for (const char *s = f; *s; s++) {
      switch (tt_lower(*s)) {
        case 'r':
          a.flags |= TT_F_RAMP;
          break;
        case 'o':
          a.flags = (uint8_t)((a.flags & ~TT_F_DONE_MASK) | TT_F_DONE_KEEP);
          break;
        case 'a':
          a.flags = (uint8_t)((a.flags & ~TT_F_DONE_MASK) | TT_F_DONE_ARCHIVE);
          break;
        case 'd':
          a.flags = (uint8_t)((a.flags & ~TT_F_DONE_MASK) | TT_F_DONE_DELETE);
          break;
        default:
          break;
      }
    }
  }
  // every (minutes between repeats inside the day)
  if (tt_csv_field(&p, f, sizeof(f)) && f[0] && tt_parse_duration(f, &v)) {
    a.every = (uint16_t)(v > 1439 ? 1439 : v);
  }
  // last (time of day the repeats stop)
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    tt_parse_clock(f, &a.last);
  }
  // until
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    tt_parse_date(f, &a.until);
  }
  // color (RRGGBB, with or without a leading #)
  if (tt_csv_field(&p, f, sizeof(f)) && f[0]) {
    const char *s = f[0] == '#' ? f + 1 : f;
    uint32_t rgb = 0;
    int digits = 0;
    for (; *s && digits < 6; s++, digits++) {
      int h = prv_hex(*s);
      if (h < 0) {
        break;
      }
      rgb = (rgb << 4) | (uint32_t)h;
    }
    if (digits == 6) {
      a.color = 0x1000000u | rgb;
    }
  }

  if (a.every && a.last < a.minute) {
    a.last = 1439;
  }
  *out = a;
  return true;
}

static int prv_app_u32(char *out, size_t cap, int pos, uint32_t v) {
  char tmp[11];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n > 0) {
    pos = tt_app_char(out, cap, pos, tmp[--n]);
  }
  return pos;
}

int tt_alarm_format(const TTAlarm *a, char *out, size_t cap) {
  char head[80];
  char tail[120];
  char tmp[48];
  int h = 0;
  int t = 0;

  // id,on,time,rule
  h = prv_app_u32(head, sizeof(head), h, a->id);
  h = tt_app(head, sizeof(head), h, a->on ? ",1," : ",0,");
  tt_format_clock(a->minute, true, tmp, sizeof(tmp));
  h = tt_app(head, sizeof(head), h, tmp);
  tt_rule_format(&a->rule, tmp, sizeof(tmp));
  h = tt_csv_put(head, sizeof(head), h, tmp, true);

  // snooze_a,snooze_b,vibe,strength,sound,flags,every,last,until,color
  tail[0] = '\0';
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->snooze_a) {
    t = prv_app_u32(tail, sizeof(tail), t, a->snooze_a);
  }
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->snooze_b) {
    t = prv_app_u32(tail, sizeof(tail), t, a->snooze_b);
  }
  t = tt_csv_put(tail, sizeof(tail), t, a->vibe, true);
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->strength) {
    t = tt_app_int(tail, sizeof(tail), t, a->strength);
  }
  t = tt_csv_put(tail, sizeof(tail), t, a->sound, true);
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->flags & TT_F_RAMP) {
    t = tt_app_char(tail, sizeof(tail), t, 'r');
  }
  if (a->flags & TT_F_DONE_KEEP) {
    t = tt_app_char(tail, sizeof(tail), t, 'o');
  } else if (a->flags & TT_F_DONE_ARCHIVE) {
    t = tt_app_char(tail, sizeof(tail), t, 'a');
  } else if (a->flags & TT_F_DONE_DELETE) {
    t = tt_app_char(tail, sizeof(tail), t, 'd');
  }
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->every) {
    t = tt_app_int(tail, sizeof(tail), t, a->every);
  }
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->every && a->last < 1439) {
    tt_format_clock(a->last, true, tmp, sizeof(tmp));
    t = tt_app(tail, sizeof(tail), t, tmp);
  }
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (tt_date_is_set(a->until)) {
    tt_format_date(a->until, tmp, sizeof(tmp));
    t = tt_app(tail, sizeof(tail), t, tmp);
  }
  t = tt_app_char(tail, sizeof(tail), t, ',');
  if (a->color) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 20; shift >= 0; shift -= 4) {
      t = tt_app_char(tail, sizeof(tail), t, hex[(a->color >> shift) & 0xF]);
    }
  }

  // The message goes in the middle and is the only part allowed to shrink.
  char msg[TT_MSG_MAX];
  char field[TT_MSG_MAX * 2 + 4];
  tt_copy(msg, sizeof(msg), a->msg);
  int flen;
  for (;;) {
    flen = tt_csv_put(field, sizeof(field), 0, msg, true);
    if ((size_t)(h + flen + t) < cap || msg[0] == '\0') {
      break;
    }
    msg[strlen(msg) - 1] = '\0';
  }
  int pos = tt_app(out, cap, 0, head);
  pos = tt_app(out, cap, pos, field);
  return tt_app(out, cap, pos, tail);
}

// ---------------------------------------------------------------- next ring

time_t tt_alarm_next_occurrence(const TTAlarm *a, time_t after) {
  int32_t day;
  int minute;
  tt_epoch_to_local(after, &day, &minute, NULL);
  const int32_t until = tt_date_is_set(a->until) ? tt_date_to_days(a->until) : INT32_MAX;
  const int every = a->every;
  const int first = a->minute;
  const int last = (every > 0) ? (a->last >= first ? a->last : 1439) : first;

  int32_t d = day;
  for (int guard = 0; guard < 16; guard++) {
    d = tt_rule_next_day(&a->rule, d);
    if (d == TT_NO_DAY || d > until) {
      return 0;
    }
    int m = first;
    if (every > 0 && d == day && minute > first) {
      m = first + ((minute - first) / every) * every;  // skip repeats already past
    }
    for (; m <= last; m += (every > 0 ? every : 1440)) {
      time_t t = tt_local_to_epoch(d, m);
      if (t > after) {
        return t;
      }
    }
    d++;
  }
  return 0;
}

time_t tt_alarm_next_fire(const TTAlarm *a) {
  if (!a->on) {
    return 0;
  }
  if (a->snooze_until) {
    return a->snooze_until;
  }
  time_t after = a->handled_until > a->skip_until ? a->handled_until : a->skip_until;
  return tt_alarm_next_occurrence(a, after);
}

bool tt_alarm_finished(const TTAlarm *a, time_t now) {
  if (a->snooze_until) {
    return false;
  }
  time_t after = now > a->skip_until ? now : a->skip_until;
  return tt_alarm_next_occurrence(a, after) == 0;
}
