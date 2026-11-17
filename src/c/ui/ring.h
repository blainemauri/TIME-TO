// The screen shown while an alarm is going off.
//   Up     snooze for the alarm's first snooze length   (hold: pick any length)
//   Down   snooze for the alarm's second snooze length  (hold: pick any length)
//   Select done
//   Back   same as Up, so an alarm is never lost by backing out
#pragma once

#include "ui.h"

//! Start ringing for this alarm. `preview` rings without changing anything.
void ring_show(TTAlarm *alarm, bool preview);
bool ring_showing(void);
