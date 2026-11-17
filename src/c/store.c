#include "store.h"

#define KEY_VERSION 1
#define KEY_SETTINGS 2
#define KEY_PATTERN_COUNT 3
#define KEY_TUNE_COUNT 4
#define KEY_ARCHIVE_COUNT 5
#define KEY_ALARM_ROW 1000    // + slot
#define KEY_ALARM_STATE 2000  // + slot
#define KEY_PATTERN 3000      // + index
#define KEY_TUNE 4000         // + index
#define KEY_ARCHIVE 5000      // + index

#define STORE_VERSION 1
#define ID_EPOCH 1577836800  // 2020-01-01T00:00:00Z

typedef struct __attribute__((packed)) {
  uint32_t id;
  int32_t snooze_until;
  int32_t handled_until;
  int32_t skip_until;
  uint8_t auto_snoozes;
  uint8_t missed;
} StateRecord;

static TTSettings s_settings;
static TTAlarm s_alarms[TT_MAX_ALARMS];  // id == 0 marks a free slot
static TTPattern s_patterns[TT_MAX_PATTERNS];
static int s_pattern_count;
static TTTune s_tunes[TT_MAX_TUNES];
static int s_tune_count;
static int s_archive_count;
static uint32_t s_generation = 1;
static char s_row[TT_ROW_MAX];

static const char *const s_default_patterns[] = {
    "Pulse,400 300 400 300 400",
    "Double,150 120 150",
    "Heartbeat,100 100 250 700 100 100 250",
    "SOS,100 100 100 100 100 300 300 100 300 100 300 300 100 100 100 100 100",
    "Long,1500",
    "Rapid,60 60 60 60 60 60 60 60 60 60 60",
    "Knock,80 150 80 400 80 150 80",
};

static const char *const s_default_tunes[] = {
    "Beep,84:120 0:80 84:120 0:80 84:120",
    "Chime,76:180 72:180 67:400",
    "Rise,60:120 64:120 67:120 72:260",
    "Alarm,88:100 0:60 88:100 0:60 88:100 0:60 88:100",
    "Bells,79:200 76:200 72:200 67:400 0:100 72:200 76:200 79:400",
};

uint32_t store_generation(void) {
  return s_generation;
}

void store_touch(void) {
  s_generation++;
}

// ---------------------------------------------------------------- settings

TTSettings *store_settings(void) {
  return &s_settings;
}

void store_settings_save(void) {
  tt_settings_format(&s_settings, s_row, sizeof(s_row));
  persist_write_string(KEY_SETTINGS, s_row);
  store_touch();
}

// ---------------------------------------------------------------- alarms

TTAlarm *store_alarm(int slot) {
  if (slot < 0 || slot >= TT_MAX_ALARMS || s_alarms[slot].id == 0) {
    return NULL;
  }
  return &s_alarms[slot];
}

TTAlarm *store_alarm_by_id(uint32_t id) {
  if (id == 0) {
    return NULL;
  }
  for (int i = 0; i < TT_MAX_ALARMS; i++) {
    if (s_alarms[i].id == id) {
      return &s_alarms[i];
    }
  }
  return NULL;
}

int store_alarm_count(void) {
  int n = 0;
  for (int i = 0; i < TT_MAX_ALARMS; i++) {
    n += s_alarms[i].id != 0;
  }
  return n;
}

int store_alarm_count_on(void) {
  int n = 0;
  for (int i = 0; i < TT_MAX_ALARMS; i++) {
    n += s_alarms[i].id != 0 && s_alarms[i].on;
  }
  return n;
}

uint32_t store_new_id(void) {
  uint32_t id = (uint32_t)(time(NULL) - ID_EPOCH);
  if (id == 0) {
    id = 1;
  }
  while (store_alarm_by_id(id)) {
    id++;
  }
  return id;
}

static int prv_slot_of(const TTAlarm *a) {
  return (int)(a - s_alarms);
}

void store_alarm_save_state(TTAlarm *a) {
  StateRecord rec = {
      .id = a->id,
      .snooze_until = (int32_t)a->snooze_until,
      .handled_until = (int32_t)a->handled_until,
      .skip_until = (int32_t)a->skip_until,
      .auto_snoozes = a->auto_snoozes,
      .missed = a->missed,
  };
  persist_write_data(KEY_ALARM_STATE + prv_slot_of(a), &rec, sizeof(rec));
  store_touch();
}

void store_alarm_save(TTAlarm *a) {
  tt_alarm_format(a, s_row, sizeof(s_row));
  persist_write_string(KEY_ALARM_ROW + prv_slot_of(a), s_row);
  store_alarm_save_state(a);
}

TTAlarm *store_alarm_add(const TTAlarm *src) {
  for (int i = 0; i < TT_MAX_ALARMS; i++) {
    if (s_alarms[i].id == 0) {
      TTAlarm *a = &s_alarms[i];
      // The slot is still free here, so a lookup only finds other alarms.
      const uint32_t id = (src->id && !store_alarm_by_id(src->id)) ? src->id : store_new_id();
      *a = *src;
      a->id = id;
      store_alarm_save(a);
      return a;
    }
  }
  return NULL;
}

void store_alarm_delete(TTAlarm *a) {
  int slot = prv_slot_of(a);
  memset(a, 0, sizeof(*a));
  persist_delete(KEY_ALARM_ROW + slot);
  persist_delete(KEY_ALARM_STATE + slot);
  store_touch();
}

bool store_alarm_archive(TTAlarm *a) {
  if (!store_archive_add(a)) {
    return false;
  }
  store_alarm_delete(a);
  return true;
}

static void prv_load_alarms(void) {
  const time_t now = time(NULL);
  memset(s_alarms, 0, sizeof(s_alarms));
  for (int slot = 0; slot < TT_MAX_ALARMS; slot++) {
    if (!persist_exists(KEY_ALARM_ROW + slot)) {
      continue;
    }
    if (persist_read_string(KEY_ALARM_ROW + slot, s_row, sizeof(s_row)) <= 0) {
      continue;
    }
    TTAlarm a;
    if (!tt_alarm_parse(s_row, &a) || a.id == 0) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "bad alarm row in slot %d", slot);
      continue;
    }
    a.handled_until = now;
    StateRecord rec;
    if (persist_read_data(KEY_ALARM_STATE + slot, &rec, sizeof(rec)) == (int)sizeof(rec) &&
        rec.id == a.id) {
      a.snooze_until = rec.snooze_until;
      a.handled_until = rec.handled_until;
      a.skip_until = rec.skip_until;
      a.auto_snoozes = rec.auto_snoozes;
      a.missed = rec.missed;
    }
    s_alarms[slot] = a;
  }
}

// ---------------------------------------------------------------- patterns

int store_pattern_count(void) {
  return s_pattern_count;
}

TTPattern *store_pattern(int index) {
  return (index >= 0 && index < s_pattern_count) ? &s_patterns[index] : NULL;
}

TTPattern *store_pattern_find(const char *name) {
  if (!name || !name[0]) {
    return NULL;
  }
  for (int i = 0; i < s_pattern_count; i++) {
    if (strcmp(s_patterns[i].name, name) == 0) {
      return &s_patterns[i];
    }
  }
  return NULL;
}

static void prv_save_patterns(int old_count) {
  for (int i = 0; i < s_pattern_count; i++) {
    tt_pattern_format(&s_patterns[i], s_row, sizeof(s_row));
    persist_write_string(KEY_PATTERN + i, s_row);
  }
  for (int i = s_pattern_count; i < old_count; i++) {
    persist_delete(KEY_PATTERN + i);
  }
  persist_write_int(KEY_PATTERN_COUNT, s_pattern_count);
  store_touch();
}

bool store_pattern_put(const TTPattern *p) {
  if (!p->name[0] || p->n == 0) {
    return false;
  }
  TTPattern *existing = store_pattern_find(p->name);
  if (existing) {
    *existing = *p;
  } else {
    if (s_pattern_count >= TT_MAX_PATTERNS) {
      return false;
    }
    s_patterns[s_pattern_count++] = *p;
  }
  prv_save_patterns(s_pattern_count);
  return true;
}

void store_pattern_delete(const char *name) {
  TTPattern *p = store_pattern_find(name);
  if (!p) {
    return;
  }
  int index = (int)(p - s_patterns);
  int old = s_pattern_count;
  memmove(&s_patterns[index], &s_patterns[index + 1],
          sizeof(TTPattern) * (size_t)(s_pattern_count - index - 1));
  s_pattern_count--;
  prv_save_patterns(old);
}

const TTPattern *store_pattern_resolve(const char *name) {
  static const TTPattern fallback = {.name = "Pulse", .n = 3, .seg = {400, 300, 400}};
  const TTPattern *p = store_pattern_find(name);
  if (!p) {
    p = store_pattern_find(s_settings.vibe);
  }
  if (!p && s_pattern_count) {
    p = &s_patterns[0];
  }
  return p ? p : &fallback;
}

void store_pattern_unique_name(char *buf, size_t cap) {
  for (int n = 1; n < 200; n++) {
    snprintf(buf, cap, "Buzz %d", n);
    if (!store_pattern_find(buf)) {
      return;
    }
  }
}

static void prv_load_patterns(void) {
  s_pattern_count = 0;
  int count = persist_read_int(KEY_PATTERN_COUNT);
  for (int i = 0; i < count && s_pattern_count < TT_MAX_PATTERNS; i++) {
    if (persist_read_string(KEY_PATTERN + i, s_row, sizeof(s_row)) > 0 &&
        tt_pattern_parse(s_row, &s_patterns[s_pattern_count])) {
      s_pattern_count++;
    }
  }
}

// ---------------------------------------------------------------- tunes

int store_tune_count(void) {
  return s_tune_count;
}

TTTune *store_tune(int index) {
  return (index >= 0 && index < s_tune_count) ? &s_tunes[index] : NULL;
}

TTTune *store_tune_find(const char *name) {
  if (!name || !name[0]) {
    return NULL;
  }
  for (int i = 0; i < s_tune_count; i++) {
    if (strcmp(s_tunes[i].name, name) == 0) {
      return &s_tunes[i];
    }
  }
  return NULL;
}

static void prv_save_tunes(int old_count) {
  for (int i = 0; i < s_tune_count; i++) {
    tt_tune_format(&s_tunes[i], s_row, sizeof(s_row));
    persist_write_string(KEY_TUNE + i, s_row);
  }
  for (int i = s_tune_count; i < old_count; i++) {
    persist_delete(KEY_TUNE + i);
  }
  persist_write_int(KEY_TUNE_COUNT, s_tune_count);
  store_touch();
}

bool store_tune_put(const TTTune *t) {
  if (!t->name[0] || t->n == 0 || strcmp(t->name, "none") == 0) {
    return false;
  }
  TTTune *existing = store_tune_find(t->name);
  if (existing) {
    *existing = *t;
  } else {
    if (s_tune_count >= TT_MAX_TUNES) {
      return false;
    }
    s_tunes[s_tune_count++] = *t;
  }
  prv_save_tunes(s_tune_count);
  return true;
}

void store_tune_delete(const char *name) {
  TTTune *t = store_tune_find(name);
  if (!t) {
    return;
  }
  int index = (int)(t - s_tunes);
  int old = s_tune_count;
  memmove(&s_tunes[index], &s_tunes[index + 1],
          sizeof(TTTune) * (size_t)(s_tune_count - index - 1));
  s_tune_count--;
  prv_save_tunes(old);
}

const TTTune *store_tune_resolve(const char *name) {
  const char *want = (name && name[0]) ? name : s_settings.sound;
  if (!want[0] || strcmp(want, "none") == 0) {
    return NULL;
  }
  return store_tune_find(want);
}

static void prv_load_tunes(void) {
  s_tune_count = 0;
  int count = persist_read_int(KEY_TUNE_COUNT);
  for (int i = 0; i < count && s_tune_count < TT_MAX_TUNES; i++) {
    if (persist_read_string(KEY_TUNE + i, s_row, sizeof(s_row)) > 0 &&
        tt_tune_parse(s_row, &s_tunes[s_tune_count])) {
      s_tune_count++;
    }
  }
}

// ---------------------------------------------------------------- archive

int store_archive_count(void) {
  return s_archive_count;
}

bool store_archive_row(int index, char *row, size_t cap) {
  if (index < 0 || index >= s_archive_count) {
    return false;
  }
  return persist_read_string(KEY_ARCHIVE + index, row, cap) > 0;
}

bool store_archive_get(int index, TTAlarm *out) {
  return store_archive_row(index, s_row, sizeof(s_row)) && tt_alarm_parse(s_row, out);
}

int store_archive_find(uint32_t id) {
  TTAlarm a;
  for (int i = 0; i < s_archive_count; i++) {
    if (store_archive_get(i, &a) && a.id == id) {
      return i;
    }
  }
  return -1;
}

void store_archive_delete(int index) {
  if (index < 0 || index >= s_archive_count) {
    return;
  }
  for (int i = index; i < s_archive_count - 1; i++) {
    if (persist_read_string(KEY_ARCHIVE + i + 1, s_row, sizeof(s_row)) > 0) {
      persist_write_string(KEY_ARCHIVE + i, s_row);
    }
  }
  s_archive_count--;
  persist_delete(KEY_ARCHIVE + s_archive_count);
  persist_write_int(KEY_ARCHIVE_COUNT, s_archive_count);
  store_touch();
}

bool store_archive_add(const TTAlarm *a) {
  // The same alarm archived twice replaces its older copy.
  int existing = store_archive_find(a->id);
  if (existing >= 0) {
    store_archive_delete(existing);
  }
  if (s_archive_count >= TT_MAX_ARCHIVE) {
    return false;
  }
  TTAlarm copy = *a;
  copy.on = false;
  tt_alarm_format(&copy, s_row, sizeof(s_row));
  persist_write_string(KEY_ARCHIVE + s_archive_count, s_row);
  s_archive_count++;
  persist_write_int(KEY_ARCHIVE_COUNT, s_archive_count);
  store_touch();
  return true;
}

TTAlarm *store_archive_restore(int index) {
  TTAlarm a;
  if (!store_archive_get(index, &a)) {
    return NULL;
  }
  a.on = true;
  a.snooze_until = 0;
  a.skip_until = 0;
  a.handled_until = time(NULL);
  a.auto_snoozes = 0;
  a.missed = 0;
  TTAlarm *live = store_alarm_add(&a);
  if (live) {
    store_archive_delete(index);
  }
  return live;
}

// ---------------------------------------------------------------- lifecycle

static void prv_seed_defaults(void) {
  tt_settings_init(&s_settings);
  store_settings_save();
  s_pattern_count = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(s_default_patterns); i++) {
    if (tt_pattern_parse(s_default_patterns[i], &s_patterns[s_pattern_count])) {
      s_pattern_count++;
    }
  }
  prv_save_patterns(s_pattern_count);
  s_tune_count = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(s_default_tunes); i++) {
    if (tt_tune_parse(s_default_tunes[i], &s_tunes[s_tune_count])) {
      s_tune_count++;
    }
  }
  prv_save_tunes(s_tune_count);
  persist_write_int(KEY_ARCHIVE_COUNT, 0);
  persist_write_int(KEY_VERSION, STORE_VERSION);
}

void store_init(void) {
  tt_settings_init(&s_settings);
  if (!persist_exists(KEY_VERSION)) {
    prv_seed_defaults();
  }
  if (persist_read_string(KEY_SETTINGS, s_row, sizeof(s_row)) > 0) {
    tt_settings_parse(s_row, &s_settings);
  }
  prv_load_patterns();
  prv_load_tunes();
  s_archive_count = persist_read_int(KEY_ARCHIVE_COUNT);
  if (s_archive_count < 0 || s_archive_count > TT_MAX_ARCHIVE) {
    s_archive_count = 0;
  }
  prv_load_alarms();
}

void store_reset(void) {
  for (int i = 0; i < TT_MAX_ALARMS; i++) {
    persist_delete(KEY_ALARM_ROW + i);
    persist_delete(KEY_ALARM_STATE + i);
  }
  for (int i = 0; i < TT_MAX_PATTERNS; i++) {
    persist_delete(KEY_PATTERN + i);
  }
  for (int i = 0; i < TT_MAX_TUNES; i++) {
    persist_delete(KEY_TUNE + i);
  }
  for (int i = 0; i < TT_MAX_ARCHIVE; i++) {
    persist_delete(KEY_ARCHIVE + i);
  }
  persist_delete(KEY_VERSION);
  memset(s_alarms, 0, sizeof(s_alarms));
  s_archive_count = 0;
  prv_seed_defaults();
  store_touch();
}
