// App-wide hooks the screens call into.
#pragma once

#include <pebble.h>

//! Something stored changed: recompute what rings next, re-arm the wakeups
//! and let the phone know (all slightly deferred, so calls can be bunched).
void main_changed(void);
//! Ring anything that is due right now.
void main_check_due(void);
