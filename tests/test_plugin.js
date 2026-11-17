// Runs the built plugin (src/generated/plugin.js) against a fake Pebble runtime.
// Usage: node tests/test_plugin.js   (after `pebble build`)
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const store = {};
const pins = [];
let sourceHandler;
let actionHandler;
global.localStorage = {
  getItem: (k) => (k in store ? store[k] : null),
  setItem: (k, v) => { store[k] = String(v); },
  removeItem: (k) => { delete store[k]; },
};
global.Pebble = {
  registerSourceHandler: (fn) => { sourceHandler = fn; },
  registerActionHandler: (fn) => { actionHandler = fn; },
  insertTimelinePin: (pin) => pins.push(pin),
};
const quiet = console.log;
console.log = () => {};
eval(fs.readFileSync(path.join(__dirname, '../src/generated/plugin.js'), 'utf8'));

function call(handler, request) {
  let result;
  handler(request, {
    data: (d) => { result = { data: d }; },
    error: (code, message) => { result = { error: code, message }; },
    ok: (d) => { result = { ok: d }; },
  });
  return result;
}

const now = Math.floor(Date.now() / 1000);
store['tt.snapshot'] = JSON.stringify({
  alarms: ['11,1,07:30,daily,Wake up', '12,0,08:00,daily,Off one', '13,1,23:59,weekly=mo.tu.we.th.fr.sa.su,Late'],
  state: { 13: { next: now + 3600, snooze: now + 600, skip: 0, missed: 0 } },
  agenda: [], patterns: [], tunes: [], archive: [], settings: '', updated: Date.now(),
});

let checks = 0;
const check = (cond, msg) => { checks++; assert.ok(cond, msg); };

// alarms/upcoming: on alarms only, soonest first, snoozed one first.
let r = call(sourceHandler, { category: 'alarms', item: 'upcoming', limit: 4 });
const inst = r.data.instances;
check(inst.length === 4, 'four upcoming');
check(inst[0].properties.name.longText.text === 'Late' && inst[0].properties.snoozed.boolean.value, 'snoozed first');
check(inst.every((i) => !/Off one/.test(i.properties.name.longText.text)), 'switched-off alarms left out');
for (let i = 1; i < inst.length; i++) {
  check(inst[i].properties.time.timestamp.value >= inst[i - 1].properties.time.timestamp.value, 'sorted');
}

// alarms/all lists every alarm with its row.
r = call(sourceHandler, { category: 'alarms', item: 'all' });
check(r.data.instances.length === 3 && r.data.instances[1].properties.row.longText.text === '12,0,08:00,daily,Off one', 'all');

// add_alarm with fields.
r = call(actionHandler, { action: 'add_alarm', args: { time: '18:30', rule: 'in 3 days', message: 'Call mum', snooze_a: '117h' } });
check(r.ok && /Call mum/.test(r.ok.text), 'add_alarm ok: ' + JSON.stringify(r));
let outbox = JSON.parse(store['tt.outbox']);
check(outbox.length === 1 && outbox[0].op === 2 && outbox[0].kind === 0, 'queued for the watch');
check(/,1,18:30,once=\d{4}-\d\d-\d\d,Call mum,7020,/.test(outbox[0].text), 'row: ' + outbox[0].text);
check(pins.length === 1 && pins[0].reminders.length === 1, 'reminder pin while it waits');

// The queued alarm shows up in upcoming straight away.
r = call(sourceHandler, { category: 'alarms', item: 'upcoming', limit: 50 });
check(r.data.instances.some((i) => i.properties.name.longText.text === 'Call mum'), 'queued alarm listed');

// add_alarm with a CSV row.
r = call(actionHandler, { action: 'add_alarm', args: { row: ',1,06:00,nth=2tu,Bins' } });
check(r.ok, 'row accepted');
outbox = JSON.parse(store['tt.outbox']);
check(/^\d+,1,06:00,nth=2tu,Bins,/.test(outbox[1].text), 'row given an id: ' + outbox[1].text);

// Errors say what is wrong.
r = call(actionHandler, { action: 'add_alarm', args: { time: '25:61' } });
check(r.error === 'INVALID_ARGS' && /time/.test(r.message), 'bad time rejected');
r = call(actionHandler, { action: 'add_alarm', args: { time: '07:00', rule: 'whenever' } });
check(r.error === 'INVALID_ARGS' && /rule/.test(r.message), 'bad rule rejected');
r = call(actionHandler, { action: 'add_alarm', args: { time: '07:00', snooze_b: 'a while' } });
check(r.error === 'INVALID_ARGS' && /snooze_b/.test(r.message), 'bad snooze rejected');
r = call(actionHandler, { action: 'nope', args: {} });
check(r.error === 'INVALID_REQUEST', 'unknown action');

// create_reminder from an ISO time.
const due = new Date(Date.now() + 2 * 86400 * 1000);
due.setSeconds(0, 0);
r = call(actionHandler, { action: 'create_reminder', args: { title: 'Renew passport', due_iso: due.toISOString(), notify_before_ms: 600000 } });
check(r.ok, 'reminder ok');
outbox = JSON.parse(store['tt.outbox']);
const before = new Date(due.getTime() - 600000);
const hhmm = String(before.getHours()).padStart(2, '0') + ':' + String(before.getMinutes()).padStart(2, '0');
check(outbox[2].text.indexOf(',' + hhmm + ',once=') > 0 && /Renew passport/.test(outbox[2].text), 'reminder row: ' + outbox[2].text);

r = call(sourceHandler, { category: 'reminders', item: 'configured' });
check(r.data.instances[0].properties.configured.boolean.value === true, 'reminders/configured');

quiet(`${process.env.TZ || '(default TZ)'}: plugin ${checks} checks, 0 failures`);
