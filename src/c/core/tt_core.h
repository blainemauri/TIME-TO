// TIME-TO core: data model, recurrence rules and the one-row-of-text formats.
//
// Everything in src/c/core is plain C with no Pebble dependency, so the same
// code is compiled into the watchapp and into the host unit tests (tests/).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(PBL_SDK_3)
#include <pebble.h>  // the watch SDK declares struct tm, mktime and localtime itself
#else
#include <time.h>
#endif

#define TT_MSG_MAX 81           // alarm message bytes, including the NUL
#define TT_NAME_MAX 17          // buzz pattern / tune name bytes, including the NUL
#define TT_ROW_MAX 256          // longest serialized row, including the NUL
#define TT_PATTERN_MAX_SEGS 40  // on/off segments in one buzz pattern
#define TT_PATTERN_MAX_MS 10000 // longest single segment the firmware accepts
#define TT_TUNE_MAX_NOTES 40
#define TT_SNOOZE_MAX_MIN 525600u  // one year, in minutes

// ---------------------------------------------------------------- dates

//! A calendar date. y == 0 means "not set".
typedef struct {
  int16_t y;
  uint8_t m;  // 1..12
  uint8_t d;  // 1..31
} TTDate;

#define TT_DATE_NONE ((TTDate){0, 0, 0})

bool tt_is_leap(int y);
int tt_days_in_month(int y, int m);
//! Days since 1970-01-01 for a proleptic Gregorian date.
int32_t tt_days_from_civil(int y, int m, int d);
TTDate tt_civil_from_days(int32_t days);
int32_t tt_date_to_days(TTDate date);
//! 0 = Monday ... 6 = Sunday.
int tt_dow(int32_t days);
bool tt_date_is_set(TTDate date);
bool tt_date_valid(int y, int m, int d);

//! Local wall-clock (day number + minute of day) to UTC epoch seconds.
time_t tt_local_to_epoch(int32_t day, int minute_of_day);
//! UTC epoch seconds to local day number, minute of day and second.
void tt_epoch_to_local(time_t t, int32_t *day, int *minute_of_day, int *second);

extern const char *const tt_wday_abbr[7];   // "Mon".."Sun"
extern const char *const tt_wday_code[7];   // "mo".."su"
extern const char *const tt_month_abbr[12]; // "Jan".."Dec"

// ---------------------------------------------------------------- rules

typedef enum {
  TT_RULE_ONCE = 0,  // one specific date
  TT_RULE_DAILY,     // every N days
  TT_RULE_WEEKLY,    // chosen weekdays, every N weeks
  TT_RULE_MONTHLY,   // a day of the month (negative counts from the end), every N months
  TT_RULE_NTH,       // the nth weekday of the month (negative counts from the end)
  TT_RULE_YEARLY,    // a month + day, every N years
  TT_RULE_YDAY,      // the nth day of the year, every N years
  TT_RULE_KIND_COUNT
} TTRuleKind;

typedef struct {
  uint8_t kind;       // TTRuleKind
  uint8_t dow_mask;   // WEEKLY: bit 0 = Monday ... bit 6 = Sunday
  int8_t mday;        // MONTHLY: 1..31 or -1..-31; YEARLY: 1..31
  int8_t nth;         // NTH: 1..5 or -1..-5
  uint8_t wday;       // NTH: 0 = Monday ... 6 = Sunday
  uint8_t month;      // YEARLY: 1..12
  uint16_t yday;      // YDAY: 1..366
  uint16_t interval;  // every N periods, >= 1
  TTDate date;        // ONCE: the date. Otherwise an optional start/anchor date.
} TTRule;

#define TT_DOW_WEEKDAYS 0x1F
#define TT_DOW_WEEKEND 0x60
#define TT_DOW_ALL 0x7F

//! Parse the canonical rule text ("daily", "weekly=mo.we.fr", "nth=2tu/2@2026-11-17" ...).
bool tt_rule_parse(const char *text, TTRule *out);
//! Write the canonical rule text. Returns the length written (excluding NUL).
int tt_rule_format(const TTRule *rule, char *out, size_t cap);
//! A short human description, e.g. "Mon Wed Fri" or "2nd Tue monthly".
int tt_rule_describe(const TTRule *rule, char *out, size_t cap);
//! Does the rule select this local day?
bool tt_rule_matches(const TTRule *rule, int32_t day);
//! First matching day >= from, or INT32_MIN when there is none.
int32_t tt_rule_next_day(const TTRule *rule, int32_t from);
//! Clamp every field into its legal range.
void tt_rule_normalize(TTRule *rule);

#define TT_NO_DAY INT32_MIN

// ---------------------------------------------------------------- alarms

#define TT_F_RAMP 0x01          // 'r': buzz starts soft and builds up
#define TT_F_DONE_KEEP 0x02     // 'o': when finished, stay in the list switched off
#define TT_F_DONE_ARCHIVE 0x04  // 'a': when finished, move to the archive
#define TT_F_DONE_DELETE 0x08   // 'd': when finished, delete
#define TT_F_DONE_MASK 0x0E

typedef struct {
  // --- characteristics: exactly what one CSV row holds
  uint32_t id;
  bool on;
  uint16_t minute;      // minute of day, 0..1439
  TTRule rule;
  char msg[TT_MSG_MAX];
  uint32_t snooze_a;    // minutes, 0 = use the default
  uint32_t snooze_b;    // minutes, 0 = use the default
  char vibe[TT_NAME_MAX];   // buzz pattern name, "" = default
  uint8_t strength;     // 1..100, 0 = default
  char sound[TT_NAME_MAX];  // tune name, "" = default, "none" = silent
  uint8_t flags;        // TT_F_*
  uint16_t every;       // repeat within the day every N minutes, 0 = no
  uint16_t last;        // last minute of day for those repeats
  TTDate until;         // last day it may fire, unset = forever
  uint32_t color;       // 0xRRGGBB + 0x1000000, 0 = default
  // --- runtime state: saved separately, never part of the row
  time_t snooze_until;  // snoozed until this instant, 0 = not snoozed
  time_t handled_until; // occurrences at or before this are finished with
  time_t skip_until;    // occurrences at or before this are skipped
  time_t next;          // cached next fire time, 0 = none
  uint8_t auto_snoozes; // how many times the current ring timed out unanswered
  uint8_t missed;       // gave up ringing without an answer
} TTAlarm;

//! Fill an alarm with defaults (daily at 08:00, on, nothing else set).
void tt_alarm_init(TTAlarm *a);
//! Parse one CSV row. Missing trailing columns take their defaults.
bool tt_alarm_parse(const char *row, TTAlarm *out);
//! Format one CSV row; always fits in cap (the message is shortened if it has to be).
int tt_alarm_format(const TTAlarm *a, char *out, size_t cap);
//! The header line naming the columns of tt_alarm_format.
extern const char *const tt_alarm_header;

//! First occurrence strictly after `after`, ignoring snooze/skip state. 0 = none.
time_t tt_alarm_next_occurrence(const TTAlarm *a, time_t after);
//! When the alarm will next ring given its state. 0 = never.
time_t tt_alarm_next_fire(const TTAlarm *a);
//! True if the alarm can never ring again (a one-off in the past, or past `until`).
bool tt_alarm_finished(const TTAlarm *a, time_t now);

// ---------------------------------------------------------------- buzz patterns

typedef struct {
  char name[TT_NAME_MAX];
  uint8_t n;                          // number of segments; even indexes are "on"
  uint16_t seg[TT_PATTERN_MAX_SEGS];  // milliseconds
} TTPattern;

bool tt_pattern_parse(const char *row, TTPattern *out);
int tt_pattern_format(const TTPattern *p, char *out, size_t cap);
uint32_t tt_pattern_total_ms(const TTPattern *p);

// ---------------------------------------------------------------- tunes

typedef struct {
  uint8_t midi;  // MIDI note number, 0 = rest
  uint16_t ms;
} TTNote;

typedef struct {
  char name[TT_NAME_MAX];
  uint8_t n;
  TTNote note[TT_TUNE_MAX_NOTES];
} TTTune;

bool tt_tune_parse(const char *row, TTTune *out);
int tt_tune_format(const TTTune *t, char *out, size_t cap);

// ---------------------------------------------------------------- settings

enum { TT_DONE_KEEP = 0, TT_DONE_ARCHIVE = 1, TT_DONE_DELETE = 2 };

typedef struct {
  uint32_t snooze_a;        // default snooze for the top button, minutes
  uint32_t snooze_b;        // default snooze for the bottom button, minutes
  char vibe[TT_NAME_MAX];   // default buzz pattern
  uint8_t strength;         // default strength 1..100
  char sound[TT_NAME_MAX];  // default tune, "none" = silent
  uint8_t volume;           // 0..100
  uint16_t ring_secs;       // how long an unanswered alarm rings
  uint8_t auto_snooze_max;  // unanswered rings before giving up, 0 = never give up
  uint8_t done_action;      // TT_DONE_*
  bool timeline;            // mirror upcoming alarms into the timeline
  bool light;               // light up the screen while ringing
} TTSettings;

void tt_settings_init(TTSettings *s);
bool tt_settings_parse(const char *row, TTSettings *s);
int tt_settings_format(const TTSettings *s, char *out, size_t cap);

// ---------------------------------------------------------------- text helpers

//! Read one CSV field (handles "quoted, ""fields"""). Advances *p past the
//! separator. Returns false once the row is exhausted.
bool tt_csv_field(const char **p, char *out, size_t cap);
//! Append one CSV field (quoted when needed) plus an optional leading comma.
int tt_csv_put(char *out, size_t cap, int pos, const char *field, bool comma);

//! "3m", "1h 30m", "117h", "2d 4h".
int tt_format_duration(uint32_t minutes, char *out, size_t cap);
//! Parse "3", "3m", "2h", "1h30m", "4d" into minutes. Returns false on junk.
bool tt_parse_duration(const char *text, uint32_t *minutes);
//! "now", "in 5m", "in 3h 12m", "in 2d 4h", "5m ago".
int tt_format_relative(int32_t seconds, char *out, size_t cap);
//! "HH:MM" from a minute of the day (24h) or "h:MMam".
int tt_format_clock(int minute_of_day, bool h24, char *out, size_t cap);
bool tt_parse_clock(const char *text, uint16_t *minute_of_day);
bool tt_parse_date(const char *text, TTDate *date);
int tt_format_date(TTDate date, char *out, size_t cap);
//! Copy with truncation, always NUL terminating.
void tt_copy(char *dst, size_t cap, const char *src);
