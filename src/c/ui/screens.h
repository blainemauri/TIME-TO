// Entry points of the app's screens.
#pragma once

#include "ui.h"

//! The first screen: what is coming up, and the way into everything else.
void home_show(void);
//! Upcoming rings in time order, grouped by day.
void agenda_show(void);
//! Every alarm, with on/off.
void alarms_show(void);
//! What you can do to one alarm (toggle, edit, skip, duplicate, archive ...).
void alarm_actions_show(uint32_t alarm_id);
//! Create (id 0) or edit an alarm.
void editor_show(uint32_t alarm_id);
//! Edit when an alarm repeats. `rule` is changed in place.
void repeat_show(TTRule *rule);
//! Buzz patterns: play, pick a default, rename, delete, record a new one.
void patterns_show(void);
//! Record a buzz pattern by holding and releasing Select.
void composer_show(void);
//! Tunes for watches with a speaker.
void tunes_show(void);
//! Alarms put away for later.
void archive_show(void);
void settings_show(void);

//! Shared helpers for the alarm screens.
void alarm_title(const TTAlarm *a, char *buf, size_t cap);
//! "07:30 · Mon Wed Fri"
void alarm_summary(const TTAlarm *a, char *buf, size_t cap);
//! Menu choices for a snooze length. Returns the number of entries written.
#define SNOOZE_PRESET_COUNT 13
extern const uint32_t snooze_presets[SNOOZE_PRESET_COUNT];
