// Talking to the phone: sends the watch's alarms, patterns, tunes, settings
// and upcoming agenda as rows of text, and applies rows the phone sends back.
#pragma once

#include <pebble.h>

void comm_init(void);
void comm_deinit(void);
//! Something changed on the watch: send the phone a fresh copy when it can.
void comm_changed(void);
//! Is a phone connected and has it spoken to us this session?
bool comm_phone_seen(void);
//! Send everything now (the "Sync now" row in settings).
void comm_sync_now(void);
