#!/usr/bin/env node
// Time To command line: make, check and preview alarm rows without the watch.
//
//   node tools/tt.js quick "7:30 weekdays Wake up"           -> one CSV row
//   node tools/tt.js row --time 18:00 --rule "in 3 days" --msg "Call mum" --snooze-b 117h
//   node tools/tt.js check alarms.csv                          -> lists any bad rows
//   node tools/tt.js next alarms.csv --count 10                -> when they will ring
//   node tools/tt.js show alarms.csv                           -> plain English
//
// Rows it prints can be pasted into the CSV tab of Time To's settings page,
// or given to the plugin's add_alarm action as `row`.
'use strict';

const fs = require('fs');
const path = require('path');
const tt = require(path.join(__dirname, '..', 'src', 'common', 'ttrows.js'));

const USAGE = `Usage:
  tt.js quick "<time> [when] [message]"      one alarm from a line of text
  tt.js row --time HH:MM [options]             one alarm from options
  tt.js check <file.csv|->                     validate rows (exit 1 if any are bad)
  tt.js next <file.csv|-> [--count N]          the next rings, soonest first
  tt.js show <file.csv|->                      each alarm in plain English
  tt.js header                                 the CSV header line

Options for "row" (and "quick", to override):
  --id N  --off  --time HH:MM  --rule R  --msg TEXT  --snooze-a 5m  --snooze-b 117h
  --vibe NAME  --strength 1-100  --sound NAME|none  --ramp  --every 30m  --last 17:00
  --until YYYY-MM-DD  --color RRGGBB  --done keep|archive|delete  --header

Rules: once=2026-12-25 (or a date), today, tomorrow, friday, "in 3 days", daily, daily/3,
  weekdays, weekends, weekly=mo.we.fr, weekly=tu/2, monthly=15, monthly=-1, nth=2tu,
  nth=-1fr, yearly=03-14, yday=256; add @YYYY-MM-DD to count from (and start on) a date.`;

function fail(msg) {
  process.stderr.write(msg + '\n');
  process.exit(2);
}

function parseArgs(argv) {
  const out = { _: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (!a.startsWith('--')) {
      out._.push(a);
      continue;
    }
    const key = a.slice(2);
    if (['off', 'ramp', 'header', 'help'].includes(key)) {
      out[key] = true;
    } else {
      if (i + 1 >= argv.length) fail('--' + key + ' needs a value');
      out[key] = argv[++i];
    }
  }
  return out;
}

function readRows(file) {
  const text = file === '-' || !file ? fs.readFileSync(0, 'utf8') : fs.readFileSync(file, 'utf8');
  const lines = text.split(/\r?\n/);
  const rows = [];
  lines.forEach((line, i) => {
    const t = line.trim();
    if (!t || t[0] === '#' || /^"?id"?\s*,\s*"?on"?\s*,/i.test(t)) return;
    rows.push({ line: i + 1, text: t });
  });
  return rows;
}

function applyOptions(a, o) {
  const dur = (key) => {
    const v = tt.parseDuration(o[key]);
    if (v === null) fail(`--${key}: "${o[key]}" is not a length of time`);
    return v;
  };
  if (o.id !== undefined) a.id = +o.id;
  if (o.off) a.on = false;
  if (o.time !== undefined) {
    a.minute = tt.parseClock(o.time);
    if (a.minute === null) fail(`--time: "${o.time}" is not a time of day`);
  }
  if (o.rule !== undefined) {
    a.rule = tt.parseRule(o.rule);
    if (!a.rule) fail(`--rule: "${o.rule}" is not a rule I know`);
  }
  if (o.msg !== undefined) a.msg = String(o.msg).slice(0, tt.MSG_MAX);
  if (o['snooze-a'] !== undefined) a.snoozeA = dur('snooze-a');
  if (o['snooze-b'] !== undefined) a.snoozeB = dur('snooze-b');
  if (o.vibe !== undefined) a.vibe = o.vibe.slice(0, tt.NAME_MAX);
  if (o.strength !== undefined) a.strength = Math.max(1, Math.min(100, +o.strength || 0));
  if (o.sound !== undefined) a.sound = o.sound.slice(0, tt.NAME_MAX);
  if (o.ramp) a.ramp = true;
  if (o.every !== undefined) a.every = Math.min(1439, dur('every'));
  if (o.last !== undefined) {
    a.last = tt.parseClock(o.last);
    if (a.last === null) fail(`--last: "${o.last}" is not a time of day`);
  }
  if (o.until !== undefined) {
    a.until = tt.parseDate(o.until);
    if (!a.until) fail(`--until: "${o.until}" is not a date`);
  }
  if (o.color !== undefined) a.color = o.color.replace(/^#/, '').toLowerCase();
  if (o.done !== undefined) a.done = o.done;
  return a;
}

function when(sec) {
  const d = new Date(sec * 1000);
  return d.toLocaleString(undefined, { weekday: 'short', year: 'numeric', month: 'short', day: 'numeric',
    hour: '2-digit', minute: '2-digit' });
}

function print(a, opts) {
  if (opts.header) console.log(tt.HEADER);
  // Round-trip through the parser so what we print is exactly what the watch reads.
  console.log(tt.formatAlarm(tt.parseAlarm(tt.formatAlarm(a))));
  const next = tt.nextOccurrence(a, Math.floor(Date.now() / 1000));
  process.stderr.write(tt.describeAlarm(a) + (next ? '\n  first rings ' + when(next) : '\n  (as set, it never rings)') + '\n');
}

function main() {
  const o = parseArgs(process.argv.slice(2));
  const cmd = o._[0];
  if (!cmd || o.help) {
    console.log(USAGE);
    return;
  }
  switch (cmd) {
    case 'header':
      console.log(tt.HEADER);
      return;
    case 'quick': {
      let a;
      try {
        a = tt.quickParse(o._.slice(1).join(' '));
      } catch (e) {
        fail(e.message);
      }
      print(applyOptions(a, o), o);
      return;
    }
    case 'row': {
      if (o.time === undefined) fail('row needs --time');
      print(applyOptions(tt.newAlarm(), o), o);
      return;
    }
    case 'check': {
      let bad = 0;
      const rows = readRows(o._[1]);
      rows.forEach((r) => {
        try {
          tt.parseAlarm(r.text);
        } catch (e) {
          bad++;
          console.log(`line ${r.line}: ${e.message}\n  ${r.text}`);
        }
      });
      console.log(`${rows.length - bad} good, ${bad} bad`);
      process.exit(bad ? 1 : 0);
      return;
    }
    case 'next': {
      const count = +(o.count || 10);
      const now = Math.floor(Date.now() / 1000);
      const rings = [];
      readRows(o._[1]).forEach((r) => {
        const a = tt.tryParseAlarm(r.text);
        if (!a || !a.on) return;
        let after = now;
        for (let i = 0; i < count; i++) {
          const t = tt.nextOccurrence(a, after);
          if (!t) break;
          rings.push({ t, a });
          after = t;
        }
      });
      rings.sort((x, y) => x.t - y.t).slice(0, count).forEach((r) => {
        console.log(when(r.t).padEnd(28) + (r.a.msg || 'Alarm'));
      });
      return;
    }
    case 'show':
      readRows(o._[1]).forEach((r) => {
        const a = tt.tryParseAlarm(r.text);
        console.log(a ? (a.on ? '  ' : 'x ') + tt.describeAlarm(a) : `? line ${r.line} does not parse`);
      });
      return;
    default:
      fail('Unknown command "' + cmd + '".\n\n' + USAGE);
  }
}

main();
