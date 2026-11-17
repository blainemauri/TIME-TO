// Time To's plugin: runs on the phone inside the Pebble app.
//
// Sources (what other watchfaces and apps can read):
//   alarms/upcoming   the next rings across all alarms, soonest first
//   alarms/all        every alarm, with its CSV row
// Actions (what other apps, and the Index assistant, can do):
//   add_alarm         create an alarm from a time + rule + message, or a CSV row
//   create_reminder   the standard reminders action: a one-off alarm
//
// The plugin cannot reach the watch itself. New alarms go into the outbox it
// shares with Time To's PebbleKit JS, which delivers them the next time the
// watch app opens; until then a timeline reminder stands in for the alarm.
'use strict';

var tt = require('../common/ttrows.js');
var store = require('../common/store.js');

var ID_EPOCH = 1577836800;  // ids are seconds since 2020, as the watch makes them

function log(msg) {
  console.log('[TimeTo plugin] ' + msg);
}

/** Alarms the watch has, plus ones still waiting in the outbox. */
function allAlarms() {
  var snap = store.loadSnapshot();
  var byId = {};
  snap.alarms.forEach(function (row) {
    var a = tt.tryParseAlarm(row);
    if (a) byId[a.id] = { alarm: a, row: row, state: snap.state[a.id] || null, queued: false };
  });
  store.loadOutbox().forEach(function (item) {
    if (item.op !== 2 || item.kind !== 0) return;
    String(item.text).split('\n').forEach(function (row) {
      var a = tt.tryParseAlarm(row);
      if (a && a.id) byId[a.id] = { alarm: a, row: row, state: null, queued: true };
    });
  });
  return Object.keys(byId).map(function (k) { return byId[k]; });
}

/** Upcoming rings, soonest first, computed from the rows so a stale copy still counts forward. */
function upcoming(limit) {
  var now = Math.floor(Date.now() / 1000);
  var out = [];
  allAlarms().forEach(function (entry) {
    var a = entry.alarm;
    var st = entry.state || {};
    if (!a.on) return;
    var after = Math.max(now - 1, st.skip || 0);
    if (st.snooze && st.snooze > now) {
      out.push({ entry: entry, when: st.snooze, snoozed: true });
      after = Math.max(after, st.snooze);
    }
    for (var i = 0; i < 5; i++) {
      var t = tt.nextOccurrence(a, after);
      if (!t) break;
      out.push({ entry: entry, when: t, snoozed: false });
      after = t;
    }
  });
  out.sort(function (x, y) { return x.when - y.when; });
  return out.slice(0, limit);
}

function dayLabel(when) {
  var d = tt.epochToLocal(when).day;
  var t = tt.today();
  if (d === t) return 'Today';
  if (d === t + 1) return 'Tmrw';
  return tt.WDAY_ABBR[tt.dow(d)];
}

function text(s) {
  return { text: String(s) };
}

function serveUpcoming(request, respond) {
  var limit = Math.min(request.limit || 10, 50);
  respond.data({
    validUntilMs: Date.now() + 60000,
    instances: upcoming(limit).map(function (o) {
      var a = o.entry.alarm;
      var minute = tt.epochToLocal(o.when).minute;
      var name = a.msg || 'Alarm';
      return {
        instanceId: a.id + '-' + o.when,
        properties: {
          name: { shortText: text(name.slice(0, 7)), longText: text(name) },
          time: {
            timestamp: { value: o.when },
            shortText: text(tt.formatClock(minute)),
            longText: text(dayLabel(o.when) + ' ' + tt.formatClock(minute)),
          },
          day: { shortText: text(dayLabel(o.when)) },
          snoozed: { boolean: { value: o.snoozed } },
        },
      };
    }),
  });
}

function serveAll(request, respond) {
  respond.data({
    instances: allAlarms().map(function (entry) {
      var a = entry.alarm;
      var name = a.msg || 'Alarm';
      return {
        instanceId: String(a.id),
        properties: {
          name: { shortText: text(name.slice(0, 7)), longText: text(name) },
          schedule: { longText: text(tt.describeAlarm(a)) },
          on: { boolean: { value: !!a.on } },
          row: { longText: text(entry.row) },
        },
      };
    }),
  });
}

Pebble.registerSourceHandler(function (request, respond) {
  if (request.category === 'alarms' && request.item === 'upcoming') return serveUpcoming(request, respond);
  if (request.category === 'alarms' && request.item === 'all') return serveAll(request, respond);
  if (request.category === 'reminders' && request.item === 'configured') {
    return respond.data({
      instances: [{
        instanceId: 'timeto',
        properties: {
          name: { shortText: text('Time To') },
          configured: { boolean: { value: true }, shortText: text('Yes') },
        },
      }],
    });
  }
  respond.error('PLUGIN_UNAVAILABLE');
});

// ---------------------------------------------------------------- actions

function freshId() {
  var taken = {};
  allAlarms().forEach(function (e) { taken[e.alarm.id] = true; });
  var id = Math.floor(Date.now() / 1000) - ID_EPOCH;
  while (taken[id]) id++;
  return id;
}

/** "The next time it is HH:MM": today if still to come, else tomorrow. */
function nextDateFor(minute) {
  var here = tt.epochToLocal(Math.floor(Date.now() / 1000));
  return tt.civilFromDays(minute > here.minute ? here.day : here.day + 1);
}

function alarmFromArgs(args) {
  args = args || {};
  if (args.row) return tt.parseAlarm(String(args.row));
  if (args.text && !args.time) return tt.quickParse(String(args.text));
  var a = tt.newAlarm();
  if (args.time === undefined || args.time === '') throw new Error('time: give a time such as 07:30');
  a.minute = tt.parseClock(args.time);
  if (a.minute === null) throw new Error('time: "' + args.time + '" is not a time of day');
  if (args.rule) {
    a.rule = tt.parseRule(args.rule);
    if (!a.rule) throw new Error('rule: "' + args.rule + '" is not a rule I know');
  } else {
    a.rule = tt.newRule('once');
    a.rule.date = nextDateFor(a.minute);
  }
  a.msg = String(args.message || '').slice(0, tt.MSG_MAX);
  ['snooze_a', 'snooze_b'].forEach(function (key, i) {
    if (args[key] === undefined || args[key] === '') return;
    var minutes = tt.parseDuration(args[key]);
    if (minutes === null) throw new Error(key + ': "' + args[key] + '" is not a length of time');
    if (i === 0) a.snoozeA = minutes;
    else a.snoozeB = minutes;
  });
  return a;
}

function reminderFromArgs(args) {
  args = args || {};
  var a = tt.newAlarm();
  var when = args.due_iso ? new Date(args.due_iso) : new Date(Date.now() + 3600 * 1000);
  if (isNaN(when.getTime())) throw new Error('due_iso: not a date');
  when = new Date(when.getTime() - (args.notify_before_ms || 0));
  var here = tt.epochToLocal(Math.floor(when.getTime() / 1000));
  a.minute = here.minute;
  a.rule = tt.newRule('once');
  a.rule.date = tt.civilFromDays(here.day);
  a.msg = String(args.title || 'Reminder').slice(0, tt.MSG_MAX);
  return a;
}

/** Queue the alarm for the watch, and put a reminder in the timeline until it gets there. */
function deliver(a) {
  if (!a.id) a.id = freshId();
  var row = tt.formatAlarm(a);
  var outbox = store.loadOutbox();
  outbox.push({ op: 2, kind: 0, text: row });
  store.saveOutbox(outbox);

  var when = tt.nextOccurrence(a, Math.floor(Date.now() / 1000) - 1);
  if (when && typeof Pebble.insertTimelinePin === 'function') {
    var id = 'tt-' + a.id + '-' + when;
    try {
      Pebble.insertTimelinePin({
        id: id,
        time: new Date(when * 1000).toISOString(),
        layout: { type: 'genericPin', title: a.msg || 'Alarm', subtitle: 'Time To', tinyIcon: 'system://images/ALARM_CLOCK' },
        reminders: [{
          time: new Date(when * 1000).toISOString(),
          layout: { type: 'genericReminder', title: a.msg || 'Alarm', tinyIcon: 'system://images/ALARM_CLOCK' },
        }],
      });
      var pins = store.loadPins();
      pins[id] = true;
      store.savePins(pins);
    } catch (e) {
      log('pin failed: ' + e);
    }
  }
  return { row: row, when: when };
}

function confirmation(a, when) {
  if (!when) return 'Saved, but that alarm will never ring as set.';
  var here = tt.epochToLocal(when);
  var day = dayLabel(when).replace('Tmrw', 'tomorrow').replace('Today', 'today');
  return 'Alarm set for ' + day + ' at ' + tt.formatClock(here.minute) + (a.msg ? ': ' + a.msg : '') +
    '. It reaches your watch next time Time To opens.';
}

Pebble.registerActionHandler(function (request, respond) {
  var a;
  try {
    if (request.action === 'add_alarm') a = alarmFromArgs(request.args);
    else if (request.action === 'create_reminder') a = reminderFromArgs(request.args);
    else return respond.error('INVALID_REQUEST', 'Unknown action ' + request.action);
  } catch (e) {
    return respond.error('INVALID_ARGS', e.message);
  }
  var result = deliver(a);
  log('queued ' + result.row);
  respond.ok({ text: confirmation(a, result.when), refreshed: ['alarms/upcoming', 'alarms/all'] });
});
