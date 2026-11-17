// Small "pick a value" screens used by the editors: numbers, times, dates,
// durations, one-of-many lists and text.
#pragma once

#include "ui.h"

typedef void (*NumberPicked)(void *ctx, int32_t value);
typedef void (*TimePicked)(void *ctx, int minute_of_day);
typedef void (*DatePicked)(void *ctx, TTDate date);
typedef void (*DurationPicked)(void *ctx, uint32_t minutes);
typedef void (*ChoicePicked)(void *ctx, int index);
typedef void (*TextPicked)(void *ctx, const char *text);

void pick_number(const char *title, int32_t initial, int32_t min, int32_t max, const char *unit,
                 NumberPicked done, void *ctx);
void pick_time(const char *title, int minute_of_day, TimePicked done, void *ctx);
void pick_date(const char *title, TTDate initial, DatePicked done, void *ctx);
void pick_duration(const char *title, uint32_t minutes, DurationPicked done, void *ctx);

#define PICK_CHOICE_MAX 24
#define PICK_CHOICE_LEN 26
//! A list to choose one entry from. The strings are copied. `highlight` (optional)
//! is told as the selection moves, e.g. to preview a buzz pattern.
void pick_choice(const char *title, const char *const *items, int count, int selected,
                 ChoicePicked done, ChoicePicked highlight, void *ctx);

//! Enter text by dictation (where there is a microphone) or letter by letter.
void pick_text(const char *title, const char *initial, size_t max_len, TextPicked done,
               void *ctx);
