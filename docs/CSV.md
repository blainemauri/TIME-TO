# Alarm rows (CSV)

Every alarm is one line of CSV. The watch stores each alarm as exactly this
line, the settings page imports and exports it, the plugin accepts it, and
`tools/tt.js` makes and checks it. If you can write a line of a spreadsheet,
you can make an alarm.

```
id,on,time,rule,msg,snooze_a,snooze_b,vibe,strength,sound,flags,every,last,until,color
,1,07:30,weekdays,Wake up,5,45,Heartbeat,80,,r,,,,ff5500
```

Only `time` is required. Leave anything else empty and it takes its default.
Trailing empty columns can be left off entirely: `,,07:30` is a valid alarm
(every day at 07:30).

| column | what it is | examples |
| --- | --- | --- |
| `id` | Any whole number. A row with an id that already exists replaces that alarm. Empty or `0` makes a new one. | `17`, empty |
| `on` | `1` on, `0` off. Empty means on. | `1` |
| `time` | Time of day. Required. | `07:30`, `7:30pm`, `0730`, `19:00` |
| `rule` | When it rings (see below). Empty means every day. | `weekdays`, `nth=2tu` |
| `msg` | What it shows. Up to 80 characters. Quote it if it contains a comma: `"Bins, recycling"`. | `Take meds` |
| `snooze_a` | Top button snooze. Minutes, or with a unit. Empty = the default from settings. Up to a year. | `3`, `10m`, `2h`, `117h`, `3d`, `1w` |
| `snooze_b` | Bottom button snooze, the same way. | `30`, `117h` |
| `vibe` | Buzz pattern name. Empty = the default pattern. | `Heartbeat` |
| `strength` | Buzz strength, 1 to 100. Empty = the default. | `40` |
| `sound` | Tune name, `none` for silent. Empty = the default. | `Chime`, `none` |
| `flags` | Letters: `r` build up (start soft, get stronger). When it is finished: `o` switch off, `a` archive, `d` delete. | `r`, `ra`, `d` |
| `every` | After the first ring of the day, ring again every this many minutes. | `120`, `30m`, `2h` |
| `last` | The last time of day for those repeats. Empty = until midnight. | `17:00` |
| `until` | The last date it can ring. | `2026-12-31` |
| `color` | A colour for the alarm (list stripe, ring screen). | `ff5500` |

## Rules

| rule | means |
| --- | --- |
| `once=2026-12-25` or just `2026-12-25` | that date only |
| `daily` | every day |
| `daily/3` | every 3rd day (counting from today, or from `@date`) |
| `weekdays`, `weekends` | Monday to Friday, Saturday and Sunday |
| `weekly=mo.we.fr` | those days (`mo tu we th fr sa su`) |
| `weekly=tu/2@2026-11-17` | every other Tuesday, counting from the week of 17 Nov 2026 |
| `monthly=15` | the 15th of each month (`31` means the last day in shorter months) |
| `monthly=-1` | the last day of the month (`-2` the day before that, ...) |
| `monthly=1/3@2026-01-01` | the 1st of every 3rd month: Jan, Apr, Jul, Oct |
| `nth=2tu` | the 2nd Tuesday of the month (`1` to `5`) |
| `nth=-1fr` | the last Friday of the month (`-2fr` the one before) |
| `yearly=03-14` | every 14 March (`02-29` rings on 28 Feb in other years) |
| `yday=256` | the 256th day of the year |
| `...@YYYY-MM-DD` | on any repeating rule: start on that date, and count "every N" from it |
| `.../N` | on any repeating rule: every Nth day/week/month/year |

The settings page, plugin and `tools/tt.js` also understand words, which they
turn into a date before saving: `today`, `tomorrow`, `friday` (the next
Friday), `in 3 days`, `in 2 weeks`. Files you keep should use dates, because
"tomorrow" means something different tomorrow.

## Other row types

Buzz patterns: name, then on/off times in milliseconds (on, off, on, ...).
Up to 39 numbers; each at most 10000.

```
Heartbeat,100 100 250 700 100 100 250
```

Tunes: name, then `midi:milliseconds` notes. MIDI 60 is middle C, 72 the C
above; `0` is a rest. Up to 40 notes.

```
Chime,76:180 72:180 67:400
```

Settings: `key=value` pairs separated by `;`.

```
snooze_a=3;snooze_b=30;vibe=Pulse;strength=100;sound=none;volume=70;ring=60;auto=5;done=archive;timeline=1;light=1
```

`ring` is how many seconds an unanswered alarm rings before it snoozes itself,
`auto` how many of those before it gives up and marks the alarm missed (0 =
never), `done` what happens to a one-off when it is finished (`archive`,
`keep`, `delete`).

## Getting rows onto the watch

* **Settings page**, CSV tab: paste rows, press Check, then Import. Bad rows are
  listed with the reason and nothing is sent until you import the good ones.
* **Quick add** on the settings page: `7:30 weekdays Wake up`.
* **The plugin's `add_alarm` action** (other apps, and the Index assistant):
  `{ "row": ",1,07:30,weekdays,Wake up" }` or `{ "text": "7:30 weekdays Wake up" }`.
  It reaches the watch the next time Time To opens; until then a timeline
  reminder stands in for it.
* **On the watch**: Archive, "Use as template" turns any archived row back
  into a new alarm.

## Making rows with a script or an agent

`tools/tt.js` (needs Node) is the reference:

```bash
node tools/tt.js quick "6:45pm in 3 days Call the dentist back" --snooze-b 117h
node tools/tt.js row --time 07:30 --rule weekdays --msg "Wake up" --ramp
node tools/tt.js check my-alarms.csv
node tools/tt.js next my-alarms.csv --count 10
```

A prompt that works well for turning a voice note into rows:

> Convert this note into Time To alarm rows. Output only CSV lines with the
> columns `id,on,time,rule,msg,snooze_a,snooze_b` and leave `id` empty. `time`
> is 24-hour HH:MM. `rule` is one of: a date `YYYY-MM-DD` for a one-off,
> `daily`, `daily/N`, `weekdays`, `weekends`, `weekly=mo.we.fr` (two-letter
> days joined by dots), `monthly=D` (`-1` = last day), `nth=Nxx` (for example
> `nth=2tu`, `nth=-1fr`), `yearly=MM-DD`. Snoozes are like `10m`, `2h`, `117h`;
> leave them empty unless the note says. Today is {date}. Quote a message that
> contains a comma.

Then `node tools/tt.js check` the result before importing it.
