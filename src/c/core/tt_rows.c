// Row formats for buzz patterns, tunes and settings.
//
//   pattern row:  name,120 80 120 600        on, off, on, off ... in milliseconds
//   tune row:     name,72:150 0:50 76:300    MIDI note : milliseconds (note 0 is a rest)
//   settings row: snooze_a=3;snooze_b=30;vibe=Pulse;strength=100;...
#include "tt_internal.h"

#include <string.h>

// ---------------------------------------------------------------- patterns

bool tt_pattern_parse(const char *row, TTPattern *out) {
  TTPattern p;
  memset(&p, 0, sizeof(p));
  char f[TT_ROW_MAX];
  const char *r = row;
  if (!tt_csv_field(&r, f, sizeof(f)) || !f[0]) {
    return false;
  }
  tt_copy(p.name, sizeof(p.name), f);
  if (!tt_csv_field(&r, f, sizeof(f))) {
    return false;
  }
  const char *s = f;
  while (*s && p.n < TT_PATTERN_MAX_SEGS) {
    while (*s == ' ' || *s == '.' || *s == ';') {
      s++;
    }
    if (!*s) {
      break;
    }
    int32_t v;
    if (!tt_scan_int(&s, &v) || v < 0) {
      return false;
    }
    if (v > TT_PATTERN_MAX_MS) {
      v = TT_PATTERN_MAX_MS;
    }
    p.seg[p.n++] = (uint16_t)v;
  }
  // A pattern must start with a buzz and has no use for a trailing silence.
  while (p.n > 0 && (p.n % 2 == 0)) {
    p.n--;
  }
  if (p.n == 0 || p.seg[0] == 0) {
    return false;
  }
  *out = p;
  return true;
}

int tt_pattern_format(const TTPattern *p, char *out, size_t cap) {
  int pos = tt_csv_put(out, cap, 0, p->name, false);
  pos = tt_app_char(out, cap, pos, ',');
  for (int i = 0; i < p->n; i++) {
    if ((size_t)pos + 8 >= cap) {
      break;
    }
    if (i) {
      pos = tt_app_char(out, cap, pos, ' ');
    }
    pos = tt_app_int(out, cap, pos, p->seg[i]);
  }
  return pos;
}

uint32_t tt_pattern_total_ms(const TTPattern *p) {
  uint32_t total = 0;
  for (int i = 0; i < p->n; i++) {
    total += p->seg[i];
  }
  return total;
}

// ---------------------------------------------------------------- tunes

bool tt_tune_parse(const char *row, TTTune *out) {
  TTTune t;
  memset(&t, 0, sizeof(t));
  char f[TT_ROW_MAX];
  const char *r = row;
  if (!tt_csv_field(&r, f, sizeof(f)) || !f[0]) {
    return false;
  }
  tt_copy(t.name, sizeof(t.name), f);
  if (!tt_csv_field(&r, f, sizeof(f))) {
    return false;
  }
  const char *s = f;
  while (*s && t.n < TT_TUNE_MAX_NOTES) {
    while (*s == ' ' || *s == ';') {
      s++;
    }
    if (!*s) {
      break;
    }
    int32_t midi, ms;
    if (!tt_scan_int(&s, &midi) || *s != ':') {
      return false;
    }
    s++;
    if (!tt_scan_int(&s, &ms) || midi < 0 || midi > 127 || ms <= 0) {
      return false;
    }
    if (ms > 10000) {
      ms = 10000;
    }
    t.note[t.n].midi = (uint8_t)midi;
    t.note[t.n].ms = (uint16_t)ms;
    t.n++;
  }
  if (t.n == 0) {
    return false;
  }
  *out = t;
  return true;
}

int tt_tune_format(const TTTune *t, char *out, size_t cap) {
  int pos = tt_csv_put(out, cap, 0, t->name, false);
  pos = tt_app_char(out, cap, pos, ',');
  for (int i = 0; i < t->n; i++) {
    if ((size_t)pos + 12 >= cap) {
      break;
    }
    if (i) {
      pos = tt_app_char(out, cap, pos, ' ');
    }
    pos = tt_app_int(out, cap, pos, t->note[i].midi);
    pos = tt_app_char(out, cap, pos, ':');
    pos = tt_app_int(out, cap, pos, t->note[i].ms);
  }
  return pos;
}

// ---------------------------------------------------------------- settings

void tt_settings_init(TTSettings *s) {
  memset(s, 0, sizeof(*s));
  s->snooze_a = 3;
  s->snooze_b = 30;
  tt_copy(s->vibe, sizeof(s->vibe), "Pulse");
  s->strength = 100;
  tt_copy(s->sound, sizeof(s->sound), "none");
  s->volume = 70;
  s->ring_secs = 60;
  s->auto_snooze_max = 5;
  s->done_action = TT_DONE_ARCHIVE;
  s->timeline = true;
  s->light = true;
}

static bool prv_key(const char *pair, const char *key, const char **value) {
  int n = tt_starts(pair, key);
  if (n && pair[n] == '=') {
    *value = pair + n + 1;
    return true;
  }
  return false;
}

bool tt_settings_parse(const char *row, TTSettings *s) {
  char pair[64];
  const char *p = row;
  bool any = false;
  while (*p) {
    size_t n = 0;
    while (*p && *p != ';' && *p != '\n' && *p != '\r') {
      if (n + 1 < sizeof(pair)) {
        pair[n++] = *p;
      }
      p++;
    }
    pair[n] = '\0';
    if (*p) {
      p++;
    }
    const char *start = pair;
    while (*start == ' ') {
      start++;
    }
    const char *v;
    uint32_t num = 0;
    const char *scan;
    int32_t iv = 0;
    if (prv_key(start, "snooze_a", &v)) {
      if (tt_parse_duration(v, &num) && num > 0) {
        s->snooze_a = num;
        any = true;
      }
    } else if (prv_key(start, "snooze_b", &v)) {
      if (tt_parse_duration(v, &num) && num > 0) {
        s->snooze_b = num;
        any = true;
      }
    } else if (prv_key(start, "vibe", &v)) {
      tt_copy(s->vibe, sizeof(s->vibe), v);
      any = true;
    } else if (prv_key(start, "strength", &v)) {
      scan = v;
      if (tt_scan_int(&scan, &iv) && iv >= 1) {
        s->strength = (uint8_t)(iv > 100 ? 100 : iv);
        any = true;
      }
    } else if (prv_key(start, "sound", &v)) {
      tt_copy(s->sound, sizeof(s->sound), v);
      any = true;
    } else if (prv_key(start, "volume", &v)) {
      scan = v;
      if (tt_scan_int(&scan, &iv) && iv >= 0) {
        s->volume = (uint8_t)(iv > 100 ? 100 : iv);
        any = true;
      }
    } else if (prv_key(start, "ring", &v)) {
      scan = v;
      if (tt_scan_int(&scan, &iv) && iv >= 5) {
        s->ring_secs = (uint16_t)(iv > 3600 ? 3600 : iv);
        any = true;
      }
    } else if (prv_key(start, "auto", &v)) {
      scan = v;
      if (tt_scan_int(&scan, &iv) && iv >= 0) {
        s->auto_snooze_max = (uint8_t)(iv > 99 ? 99 : iv);
        any = true;
      }
    } else if (prv_key(start, "done", &v)) {
      char c = tt_lower(v[0]);
      s->done_action = (c == 'a') ? TT_DONE_ARCHIVE : (c == 'd') ? TT_DONE_DELETE : TT_DONE_KEEP;
      any = true;
    } else if (prv_key(start, "timeline", &v)) {
      s->timeline = (v[0] == '1' || tt_lower(v[0]) == 'y' || tt_lower(v[0]) == 't');
      any = true;
    } else if (prv_key(start, "light", &v)) {
      s->light = (v[0] == '1' || tt_lower(v[0]) == 'y' || tt_lower(v[0]) == 't');
      any = true;
    }
  }
  return any;
}

int tt_settings_format(const TTSettings *s, char *out, size_t cap) {
  int pos = 0;
  if (cap) {
    out[0] = '\0';
  }
  pos = tt_app(out, cap, pos, "snooze_a=");
  pos = tt_app_int(out, cap, pos, (int32_t)s->snooze_a);
  pos = tt_app(out, cap, pos, ";snooze_b=");
  pos = tt_app_int(out, cap, pos, (int32_t)s->snooze_b);
  pos = tt_app(out, cap, pos, ";vibe=");
  pos = tt_app(out, cap, pos, s->vibe);
  pos = tt_app(out, cap, pos, ";strength=");
  pos = tt_app_int(out, cap, pos, s->strength);
  pos = tt_app(out, cap, pos, ";sound=");
  pos = tt_app(out, cap, pos, s->sound);
  pos = tt_app(out, cap, pos, ";volume=");
  pos = tt_app_int(out, cap, pos, s->volume);
  pos = tt_app(out, cap, pos, ";ring=");
  pos = tt_app_int(out, cap, pos, s->ring_secs);
  pos = tt_app(out, cap, pos, ";auto=");
  pos = tt_app_int(out, cap, pos, s->auto_snooze_max);
  pos = tt_app(out, cap, pos, ";done=");
  pos = tt_app(out, cap, pos,
               s->done_action == TT_DONE_ARCHIVE
                   ? "archive"
                   : s->done_action == TT_DONE_DELETE ? "delete" : "keep");
  pos = tt_app(out, cap, pos, ";timeline=");
  pos = tt_app_char(out, cap, pos, s->timeline ? '1' : '0');
  pos = tt_app(out, cap, pos, ";light=");
  return tt_app_char(out, cap, pos, s->light ? '1' : '0');
}
