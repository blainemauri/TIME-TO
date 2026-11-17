// Text helpers: CSV fields, durations, clock times and dates.
#include "tt_internal.h"

#include <string.h>

int tt_app_char(char *out, size_t cap, int pos, char c) {
  if (cap == 0) {
    return 0;
  }
  if ((size_t)pos + 1 < cap) {
    out[pos++] = c;
  }
  out[pos] = '\0';
  return pos;
}

int tt_app(char *out, size_t cap, int pos, const char *s) {
  if (cap == 0) {
    return 0;
  }
  while (*s && (size_t)pos + 1 < cap) {
    out[pos++] = *s++;
  }
  out[pos] = '\0';
  return pos;
}

int tt_app_pad(char *out, size_t cap, int pos, int32_t value, int width) {
  char tmp[12];
  int n = 0;
  bool neg = value < 0;
  uint32_t v = neg ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < 11);
  if (neg) {
    pos = tt_app_char(out, cap, pos, '-');
  }
  for (int i = n; i < width; i++) {
    pos = tt_app_char(out, cap, pos, '0');
  }
  while (n > 0) {
    pos = tt_app_char(out, cap, pos, tmp[--n]);
  }
  return pos;
}

int tt_app_int(char *out, size_t cap, int pos, int32_t value) {
  return tt_app_pad(out, cap, pos, value, 1);
}

bool tt_scan_int(const char **p, int32_t *value) {
  const char *s = *p;
  bool neg = false;
  if (*s == '-' || *s == '+') {
    neg = (*s == '-');
    s++;
  }
  if (*s < '0' || *s > '9') {
    return false;
  }
  int64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    if (v > 2000000000) {
      v = 2000000000;
    }
    s++;
  }
  *value = (int32_t)(neg ? -v : v);
  *p = s;
  return true;
}

void tt_copy(char *dst, size_t cap, const char *src) {
  if (cap == 0) {
    return;
  }
  size_t i = 0;
  for (; src && src[i] && i + 1 < cap; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

char tt_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int tt_starts(const char *text, const char *prefix) {
  int n = 0;
  while (prefix[n]) {
    if (tt_lower(text[n]) != tt_lower(prefix[n])) {
      return 0;
    }
    n++;
  }
  return n;
}

// ---------------------------------------------------------------- CSV

bool tt_csv_field(const char **p, char *out, size_t cap) {
  const char *s = *p;
  size_t n = 0;
  if (out && cap) {
    out[0] = '\0';
  }
  if (s == NULL || *s == '\0' || *s == '\n' || *s == '\r') {
    return false;
  }
  while (*s == ' ') {
    s++;  // spaces before a field (or before its opening quote) are ignored
  }
  if (*s == '"') {
    s++;
    while (*s) {
      if (*s == '"') {
        if (s[1] == '"') {
          s++;  // escaped quote
        } else {
          s++;
          break;
        }
      }
      if (out && n + 1 < cap) {
        out[n++] = *s;
      }
      s++;
    }
    while (*s && *s != ',' && *s != '\n' && *s != '\r') {
      s++;  // junk after the closing quote
    }
  } else {
    while (*s && *s != ',' && *s != '\n' && *s != '\r') {
      if (out && n + 1 < cap) {
        out[n++] = *s;
      }
      s++;
    }
    // Unquoted fields lose trailing spaces too.
    while (n > 0 && out && out[n - 1] == ' ') {
      n--;
    }
  }
  if (out && cap) {
    out[n] = '\0';
  }
  if (*s == ',') {
    s++;  // a trailing comma just ends the row; missing columns are defaults
  }
  *p = s;
  return true;
}

int tt_csv_put(char *out, size_t cap, int pos, const char *field, bool comma) {
  if (comma) {
    pos = tt_app_char(out, cap, pos, ',');
  }
  bool quote = false;
  for (const char *s = field; *s; s++) {
    if (*s == ',' || *s == '"' || *s == '\n' || *s == '\r') {
      quote = true;
      break;
    }
  }
  if (field[0] == ' ' || (field[0] && field[strlen(field) - 1] == ' ')) {
    quote = true;
  }
  if (!quote) {
    return tt_app(out, cap, pos, field);
  }
  pos = tt_app_char(out, cap, pos, '"');
  for (const char *s = field; *s; s++) {
    if (*s == '"') {
      pos = tt_app_char(out, cap, pos, '"');
    }
    pos = tt_app_char(out, cap, pos, (*s == '\n' || *s == '\r') ? ' ' : *s);
  }
  return tt_app_char(out, cap, pos, '"');
}

// ---------------------------------------------------------------- durations

int tt_format_duration(uint32_t minutes, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  if (minutes < 60) {
    pos = tt_app_int(out, cap, pos, (int32_t)minutes);
    return tt_app_char(out, cap, pos, 'm');
  }
  uint32_t hours = minutes / 60;
  uint32_t mins = minutes % 60;
  // Whole days read better as days, but only when nothing is left over;
  // "117h" should stay "117h".
  if (mins == 0 && hours % 24 == 0) {
    pos = tt_app_int(out, cap, pos, (int32_t)(hours / 24));
    return tt_app_char(out, cap, pos, 'd');
  }
  pos = tt_app_int(out, cap, pos, (int32_t)hours);
  pos = tt_app_char(out, cap, pos, 'h');
  if (mins) {
    pos = tt_app_char(out, cap, pos, ' ');
    pos = tt_app_int(out, cap, pos, (int32_t)mins);
    pos = tt_app_char(out, cap, pos, 'm');
  }
  return pos;
}

bool tt_parse_duration(const char *text, uint32_t *minutes) {
  const char *s = text;
  uint64_t total = 0;
  bool any = false;
  while (*s) {
    while (*s == ' ') {
      s++;
    }
    if (!*s) {
      break;
    }
    int32_t v;
    if (!tt_scan_int(&s, &v) || v < 0) {
      return false;
    }
    while (*s == ' ') {
      s++;
    }
    uint32_t mult = 1;
    char unit = tt_lower(*s);
    if (unit == 'm') {
      mult = 1;
    } else if (unit == 'h') {
      mult = 60;
    } else if (unit == 'd') {
      mult = 1440;
    } else if (unit == 'w') {
      mult = 10080;
    } else if (unit != '\0') {
      return false;
    }
    // Skip the rest of the unit word ("min", "hours", "days").
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) {
      s++;
    }
    total += (uint64_t)v * mult;
    any = true;
  }
  if (!any) {
    return false;
  }
  if (total > TT_SNOOZE_MAX_MIN) {
    total = TT_SNOOZE_MAX_MIN;
  }
  *minutes = (uint32_t)total;
  return true;
}

int tt_format_relative(int32_t seconds, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  bool past = seconds < 0;
  uint32_t s = past ? (uint32_t)(-(int64_t)seconds) : (uint32_t)seconds;
  if (s < 60) {
    return tt_app(out, cap, pos, past ? "just now" : "now");
  }
  uint32_t mins = s / 60;
  uint32_t days = mins / 1440;
  uint32_t hours = (mins % 1440) / 60;
  mins %= 60;
  if (!past) {
    pos = tt_app(out, cap, pos, "in ");
  }
  if (days) {
    pos = tt_app_int(out, cap, pos, (int32_t)days);
    pos = tt_app_char(out, cap, pos, 'd');
    if (hours && days < 14) {
      pos = tt_app_char(out, cap, pos, ' ');
      pos = tt_app_int(out, cap, pos, (int32_t)hours);
      pos = tt_app_char(out, cap, pos, 'h');
    }
  } else if (hours) {
    pos = tt_app_int(out, cap, pos, (int32_t)hours);
    pos = tt_app_char(out, cap, pos, 'h');
    if (mins) {
      pos = tt_app_char(out, cap, pos, ' ');
      pos = tt_app_int(out, cap, pos, (int32_t)mins);
      pos = tt_app_char(out, cap, pos, 'm');
    }
  } else {
    pos = tt_app_int(out, cap, pos, (int32_t)mins);
    pos = tt_app_char(out, cap, pos, 'm');
  }
  if (past) {
    pos = tt_app(out, cap, pos, " ago");
  }
  return pos;
}

// ---------------------------------------------------------------- clock + date

int tt_format_clock(int minute_of_day, bool h24, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  int h = (minute_of_day / 60) % 24;
  int m = minute_of_day % 60;
  if (h24) {
    pos = tt_app_pad(out, cap, pos, h, 2);
    pos = tt_app_char(out, cap, pos, ':');
    return tt_app_pad(out, cap, pos, m, 2);
  }
  int h12 = h % 12;
  if (h12 == 0) {
    h12 = 12;
  }
  pos = tt_app_int(out, cap, pos, h12);
  pos = tt_app_char(out, cap, pos, ':');
  pos = tt_app_pad(out, cap, pos, m, 2);
  return tt_app(out, cap, pos, h < 12 ? "am" : "pm");
}

bool tt_parse_clock(const char *text, uint16_t *minute_of_day) {
  const char *s = text;
  while (*s == ' ') {
    s++;
  }
  int32_t h, m = 0;
  if (!tt_scan_int(&s, &h) || h < 0) {
    return false;
  }
  if (*s == ':' || *s == '.') {
    s++;
    if (!tt_scan_int(&s, &m) || m < 0) {
      return false;
    }
  } else if (h > 99) {
    // "0730" style.
    m = h % 100;
    h = h / 100;
  }
  while (*s == ' ') {
    s++;
  }
  char ap = tt_lower(*s);
  if (ap == 'a' || ap == 'p') {
    if (h < 1 || h > 12) {
      return false;
    }
    if (ap == 'a' && h == 12) {
      h = 0;
    } else if (ap == 'p' && h != 12) {
      h += 12;
    }
  }
  if (h > 23 || m > 59) {
    return false;
  }
  *minute_of_day = (uint16_t)(h * 60 + m);
  return true;
}

bool tt_parse_date(const char *text, TTDate *date) {
  const char *s = text;
  while (*s == ' ') {
    s++;
  }
  int32_t y, m, d;
  if (!tt_scan_int(&s, &y) || *s != '-') {
    return false;
  }
  s++;
  if (!tt_scan_int(&s, &m)) {
    return false;
  }
  if (*s == '-') {
    s++;
    if (!tt_scan_int(&s, &d)) {
      return false;
    }
  } else {
    d = 1;  // "2026-12" means the first of the month
  }
  if (!tt_date_valid(y, m, d)) {
    return false;
  }
  date->y = (int16_t)y;
  date->m = (uint8_t)m;
  date->d = (uint8_t)d;
  return true;
}

int tt_format_date(TTDate date, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  if (!tt_date_is_set(date)) {
    return 0;
  }
  pos = tt_app_pad(out, cap, pos, date.y, 4);
  pos = tt_app_char(out, cap, pos, '-');
  pos = tt_app_pad(out, cap, pos, date.m, 2);
  pos = tt_app_char(out, cap, pos, '-');
  return tt_app_pad(out, cap, pos, date.d, 2);
}
