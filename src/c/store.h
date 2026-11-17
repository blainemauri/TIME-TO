// Everything the app remembers, kept in Pebble persistent storage.
//
// Each alarm, buzz pattern, tune and archived alarm is one persist value
// holding the same row of text the CSV export uses, so what is on the watch
// is exactly what you see in a spreadsheet.
#pragma once

#include <pebble.h>

#include "core/tt_core.h"

#define TT_MAX_ALARMS 64
#define TT_MAX_PATTERNS 16
#define TT_MAX_TUNES 10
#define TT_MAX_ARCHIVE 64

void store_init(void);
//! Increases every time anything stored changes; screens use it to refresh.
uint32_t store_generation(void);
void store_touch(void);

TTSettings *store_settings(void);
void store_settings_save(void);

// ---- alarms (slot = position in storage, stable while the alarm exists)
TTAlarm *store_alarm(int slot);
TTAlarm *store_alarm_by_id(uint32_t id);
int store_alarm_count(void);
int store_alarm_count_on(void);
//! Copy `src` into a free slot. Gives it a fresh id if it has none or it clashes.
TTAlarm *store_alarm_add(const TTAlarm *src);
void store_alarm_save(TTAlarm *a);
void store_alarm_save_state(TTAlarm *a);
void store_alarm_delete(TTAlarm *a);
//! Move an alarm into the archive. False if the archive is full.
bool store_alarm_archive(TTAlarm *a);
uint32_t store_new_id(void);

// ---- buzz patterns
int store_pattern_count(void);
TTPattern *store_pattern(int index);
TTPattern *store_pattern_find(const char *name);
bool store_pattern_put(const TTPattern *p);
void store_pattern_delete(const char *name);
//! The pattern an alarm should use: by name, else the default, else the first.
const TTPattern *store_pattern_resolve(const char *name);
//! A name nothing else uses yet, like "Buzz 3".
void store_pattern_unique_name(char *buf, size_t cap);

// ---- tunes
int store_tune_count(void);
TTTune *store_tune(int index);
TTTune *store_tune_find(const char *name);
bool store_tune_put(const TTTune *t);
void store_tune_delete(const char *name);
//! The tune an alarm should play, or NULL for silence.
const TTTune *store_tune_resolve(const char *name);

// ---- archive (rows only; nothing here ever rings)
int store_archive_count(void);
bool store_archive_row(int index, char *row, size_t cap);
bool store_archive_get(int index, TTAlarm *out);
bool store_archive_add(const TTAlarm *a);
//! Bring an archived alarm back as a live one. Returns it, or NULL if full.
TTAlarm *store_archive_restore(int index);
void store_archive_delete(int index);
int store_archive_find(uint32_t id);

//! Wipe everything and start again from the built-in defaults.
void store_reset(void);
