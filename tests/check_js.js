// Compares src/common/ttrows.js with the C core using vectors from tests/vectors.c.
// Usage: tests/vectors | node tests/check_js.js
'use strict';
const tt = require('../src/common/ttrows.js');

const lines = require('fs').readFileSync(0, 'utf8').split('\n').filter(Boolean);
let failures = 0;
let checks = 0;
function fail(msg) {
  failures++;
  if (failures <= 10) console.log('FAIL ' + msg);
}

for (const line of lines) {
  const [type, text, from, want] = line.split('\t');
  checks++;
  if (type === 'R') {
    const rule = tt.parseRule(text);
    if (!rule) { fail('rule did not parse: ' + text); continue; }
    if (tt.formatRule(rule) !== text) fail('rule round-trip: ' + text + ' -> ' + tt.formatRule(rule));
    const got = tt.ruleNextDay(rule, +from);
    if (String(got === null ? 'none' : got) !== want) fail(`${text} from ${from}: got ${got} want ${want}`);
  } else {
    let alarm;
    try { alarm = tt.parseAlarm(text); } catch (e) { fail('row did not parse: ' + text + ' (' + e.message + ')'); continue; }
    if (tt.formatAlarm(alarm) !== text) fail('row round-trip:\n  ' + text + '\n  ' + tt.formatAlarm(alarm));
    const got = tt.nextOccurrence(alarm, +from);
    if (String(got) !== want) fail(`${text} after ${from}: got ${got} want ${want}`);
  }
}

// Spoken-style rules the phone turns into dates (the watch never sees these).
const now = new Date(2026, 10, 17, 9, 0).getTime();  // Tue 17 Nov 2026
const spoken = { 'today': '2026-11-17', 'tomorrow': '2026-11-18', 'in 3 days': '2026-11-20',
  'in 2 weeks': '2026-12-01', 'friday': '2026-11-20', 'tuesday': '2026-11-24' };
for (const [text, date] of Object.entries(spoken)) {
  checks++;
  const r = tt.parseRule(text, now);
  if (!r || tt.formatRule(r) !== 'once=' + date) fail(`spoken "${text}" -> ${r && tt.formatRule(r)}`);
}
for (const [text, minutes] of Object.entries({ '3': 3, '30m': 30, '117h': 7020, '1h30m': 90, '2 days': 2880, '1w': 10080 })) {
  checks++;
  if (tt.parseDuration(text) !== minutes) fail(`duration ${text} -> ${tt.parseDuration(text)}`);
}
for (const bad of ['soon', '', '3x']) {
  checks++;
  if (tt.parseDuration(bad) !== null) fail(`duration "${bad}" should be rejected`);
}

console.log(`${process.env.TZ || '(default TZ)'}: js ${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
