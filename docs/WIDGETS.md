# Showing alarms on a watchface

A watchface cannot read another app's storage on the watch, so Time To
publishes its alarms through the phone instead, as a **plugin source**. Any
watchface can subscribe to it from its PebbleKit JS, and draw what it gets with
the small C renderer in `src/c/widget/`.

```
Time To (watch) --sync--> Time To (phone) --plugin source alarms/upcoming--> your face's JS --AppMessage--> your face (tt_widget_draw)
```

`examples/face` is a complete watchface doing exactly this: the time, the
date, and the next three alarms. Build it with `pebble build` inside that
folder.

Plugins are a preview feature of the Pebble app: switch them on in the Pebble
app under Settings, Plugins. Time To and the face both need to be installed.

## The source: `alarms/upcoming`

One instance per upcoming ring, soonest first, across all alarms. Snoozed
alarms appear at their snooze time. The plugin works the times out from the
alarm rows, so the list stays right even if the watch app has not run for a
while.

| property | shapes | |
| --- | --- | --- |
| `name` | `shortText` (7 chars), `longText` | the alarm's message |
| `time` | `timestamp`, `shortText` (`07:30`), `longText` (`Tue 07:30`) | when it rings |
| `day` | `shortText` | `Today`, `Tmrw`, `Wed` |
| `snoozed` | `boolean` | this ring is a snooze coming back |

```js
Pebble.subscribeToSource({
  plugin: '78ff0b72-3e3c-45d8-bb73-45381a822bd6',  // Time To
  category: 'alarms',
  item: 'upcoming',
  limit: 3,
  onData: function (update) {
    // update.instances[i].properties.time.timestamp.value, .name.longText.text ...
  },
});
```

There is also `alarms/all` (every alarm, with `name`, `schedule` as plain
English, `on`, and its CSV `row`) for faces or apps that want the whole list.

## The renderer: `tt_widget`

`src/c/widget/tt_widget.c` and `tt_widget.h` depend on nothing but
`<pebble.h>`. Copy both into your face.

```c
#include "tt_widget.h"

static TTWidgetItem s_items[3];  // fill from your AppMessage handler
static int s_count;

static void update_proc(Layer *layer, GContext *ctx) {
  TTWidgetStyle style = tt_widget_default_style();
  style.text = GColorWhite;
  style.accent = GColorOrange;
  style.relative = true;          // "in 3h" for the first entry
  tt_widget_draw(ctx, GRect(8, 130, 184, 70), s_items, s_count, 3, &style);
}
```

* Items in the past are skipped, so a list that has not been refreshed for a
  while still shows only what is to come.
* `tt_widget_height(rows, &style)` tells you how much room N rows take.
* `tt_widget_format_when()` and `tt_widget_format_until()` are there if you
  want to draw entries your own way.

Time To's own home screen uses the same renderer for its "Coming up" card.

## In the launcher

Without any watchface work, Time To's line in the watch's app list (its app
glance) shows the next alarm with a live countdown, and moves on to the next
one by itself when that one goes off.

## In the timeline

With "Show upcoming alarms in the timeline" switched on (Settings), the phone
puts a pin in the timeline for each ring in the coming week. These are pins
only, with no reminders, so they never double up with the alarm itself.
