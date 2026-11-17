// Making the watch buzz and beep: plays a buzz pattern at a chosen strength
// (optionally looping and building up) and a tune through the speaker.
#pragma once

#include <pebble.h>

#include "core/tt_core.h"

//! Ring for an alarm: loops its pattern and tune until buzz_stop().
void buzz_start(const TTAlarm *alarm);
void buzz_stop(void);
//! Play a pattern once (previews, the composer's playback).
void buzz_preview_pattern(const TTPattern *pattern, uint8_t strength);
//! Play a tune once. Does nothing on watches without a speaker.
void buzz_preview_tune(const TTTune *tune);
bool buzz_active(void);
//! True when this watch can play tunes.
bool buzz_has_speaker(void);
