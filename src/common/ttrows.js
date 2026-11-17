// Time To row formats and recurrence rules, for everything that is not the
// watch: PebbleKit JS, the plugin, the settings page and tools/tt.js.
//
// It mirrors src/c/core exactly (tests/run.sh checks the two agree), so a
// row this file accepts is a row the watch accepts, and "next ring" means the
// same thing on both sides.
/* eslint-disable no-var */
'use strict';

var HEADER = 'id,on,time,rule,msg,snooze_a,snooze_b,vibe,strength,sound,flags,every,last,until,color';
var COLUMNS = HEADER.split(',');
var KINDS = ['once', 'daily', 'weekly', 'monthly', 'nth', 'yearly', 'yday'];
var WDAY_CODE = ['mo', 'tu', 'we', 'th', 'fr', 'sa', 'su'];
var WDAY_ABBR = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
var WDAY_NAME = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];
var MONTH_ABBR = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
var DOW_ALL = 0x7f;
var DOW_WEEKDAYS = 0x1f;
var DOW_WEEKEND = 0x60;
var MSG_MAX = 80;
var NAME_MAX = 16;
var SNOOZE_MAX = 525600;

// ---------------------------------------------------------------- dates

function isLeap(y) {
  return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
}

function daysInMonth(y, m) {
  return [31, isLeap(y) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1] || 30;
}

function fdiv(a, b) {
  return Math.floor(a / b);
}

function mod(a, b) {
  return ((a % b) + b) % b;
}

/** Days since 1970-01-01 (proleptic Gregorian). */
function daysFromCivil(y, m, d) {
  y -= m <= 2 ? 1 : 0;
  var era = fdiv(y, 400);
  var yoe = y - era * 400;
  var doy = fdiv(153 * (m + (m > 2 ? -3 : 9)) + 2, 5) + d - 1;
  var doe = yoe * 365 + fdiv(yoe, 4) - fdiv(yoe, 100) + doy;
  return era * 146097 + doe - 719468;
}

function civilFromDays(z) {
  z += 719468;
  var era = fdiv(z, 146097);
  var doe = z - era * 146097;
  var yoe = fdiv(doe - fdiv(doe, 1460) + fdiv(doe, 36524) - fdiv(doe, 146096), 365);
  var y = yoe + era * 400;
  var doy = doe - (365 * yoe + fdiv(yoe, 4) - fdiv(yoe, 100));
  var mp = fdiv(5 * doy + 2, 153);
  var d = doy - fdiv(153 * mp + 2, 5) + 1;
  var m = mp < 10 ? mp + 3 : mp - 9;
  return { y: y + (m <= 2 ? 1 : 0), m: m, d: d };
}

function dateToDays(date) {
  return daysFromCivil(date.y, date.m, date.d);
}

/** 0 = Monday ... 6 = Sunday. */
function dow(days) {
  return mod(days + 3, 7);
}

function validDate(y, m, d) {
  return y >= 1970 && y <= 2199 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}

function parseDate(text) {
  var match = /^\s*(\d{4})-(\d{1,2})(?:-(\d{1,2}))?\s*$/.exec(String(text || ''));
  if (!match) return null;
  var y = +match[1];
  var m = +match[2];
  var d = match[3] ? +match[3] : 1;
  return validDate(y, m, d) ? { y: y, m: m, d: d } : null;
}

function pad(n, width) {
  var s = String(Math.abs(n));
  while (s.length < (width || 2)) s = '0' + s;
  return (n < 0 ? '-' : '') + s;
}

function formatDate(date) {
  return date ? pad(date.y, 4) + '-' + pad(date.m) + '-' + pad(date.d) : '';
}

/** Local wall-clock to epoch seconds, the way mktime does it. */
function localToEpoch(day, minute) {
  var c = civilFromDays(day);
  return Math.floor(new Date(c.y, c.m - 1, c.d, Math.floor(minute / 60), minute % 60, 0).getTime() / 1000);
}

function epochToLocal(seconds) {
  var t = new Date(seconds * 1000);
  return {
    day: daysFromCivil(t.getFullYear(), t.getMonth() + 1, t.getDate()),
    minute: t.getHours() * 60 + t.getMinutes(),
  };
}

function today(now) {
  return epochToLocal(Math.floor((now || Date.now()) / 1000)).day;
}

// ---------------------------------------------------------------- clock + durations

function parseClock(text) {
  var match = /^\s*(\d{1,4})(?:[:.](\d{1,2}))?\s*([ap])?\.?m?\.?\s*$/i.exec(String(text || ''));
  if (!match) return null;
  var h = +match[1];
  var m = match[2] !== undefined ? +match[2] : 0;
  if (match[2] === undefined && h > 99) {
    m = h % 100;
    h = Math.floor(h / 100);
  }
  if (match[3]) {
    if (h < 1 || h > 12) return null;
    var pm = match[3].toLowerCase() === 'p';
    if (h === 12) h = 0;
    if (pm) h += 12;
  }
  if (h > 23 || m > 59) return null;
  return h * 60 + m;
}

function formatClock(minute, h24) {
  var h = Math.floor(minute / 60) % 24;
  var m = minute % 60;
  if (h24 !== false) return pad(h) + ':' + pad(m);
  return (h % 12 || 12) + ':' + pad(m) + (h < 12 ? 'am' : 'pm');
}

/** "3", "3m", "117h", "1h30m", "2 days", "1w" -> minutes. null on junk. */
function parseDuration(text) {
  var s = String(text === undefined || text === null ? '' : text).trim();
  if (!s) return null;
  var re = /(\d+)\s*([a-z]*)/gi;
  var total = 0;
  var any = false;
  var consumed = '';
  var match;
  while ((match = re.exec(s))) {
    var unit = match[2].toLowerCase();
    var mult = { '': 1, m: 1, min: 1, mins: 1, minute: 1, minutes: 1, h: 60, hr: 60, hrs: 60, hour: 60,
      hours: 60, d: 1440, day: 1440, days: 1440, w: 10080, wk: 10080, week: 10080, weeks: 10080 }[unit];
    if (mult === undefined) return null;
    total += +match[1] * mult;
    any = true;
    consumed += match[0];
  }
  if (!any || consumed.replace(/\s/g, '') !== s.replace(/\s/g, '')) return null;
  return Math.min(total, SNOOZE_MAX);
}

function formatDuration(minutes) {
  if (minutes < 60) return minutes + 'm';
  var h = Math.floor(minutes / 60);
  var m = minutes % 60;
  if (m === 0 && h % 24 === 0) return h / 24 + 'd';
  return h + 'h' + (m ? ' ' + m + 'm' : '');
}

// ---------------------------------------------------------------- rules

function newRule(kind) {
  return { kind: kind || 'daily', dow: 0, mday: 1, nth: 1, wday: 0, month: 1, yday: 1, interval: 1, date: null };
}

function normalizeRule(r) {
  if (KINDS.indexOf(r.kind) < 0) r.kind = 'daily';
  r.interval = Math.max(1, Math.min(65535, r.interval | 0 || 1));
  r.dow &= DOW_ALL;
  if (r.kind === 'weekly' && !r.dow) r.dow = DOW_ALL;
  r.mday = r.mday | 0 || 1;
  r.mday = Math.max(-31, Math.min(31, r.mday));
  r.nth = r.nth | 0 || 1;
  r.nth = Math.max(-5, Math.min(5, r.nth));
  if (!(r.wday >= 0 && r.wday <= 6)) r.wday = 0;
  if (!(r.month >= 1 && r.month <= 12)) r.month = 1;
  if (r.kind === 'yearly') {
    if (r.mday < 1) r.mday = 1;
    r.mday = Math.min(r.mday, daysInMonth(2024, r.month));
  }
  r.yday = Math.max(1, Math.min(366, r.yday | 0 || 1));
  if (r.date && !validDate(r.date.y, r.date.m, r.date.d)) r.date = null;
  return r;
}

function dayCode(s) {
  return WDAY_CODE.indexOf(String(s).slice(0, 2).toLowerCase());
}

/**
 * Parse rule text. Accepts the canonical form plus a few spoken-style
 * shorthands that become a one-off date: "today", "tomorrow", "in 3 days",
 * "in 2 weeks", and weekday names ("friday" = the next Friday, once).
 */
function parseRule(text, now) {
  var s = String(text || '').trim();
  var lower = s.toLowerCase();
  var r = newRule();
  var t = today(now);
  if (!s) return null;
  var rel = /^in\s+(\d+)\s*(d|day|days|w|wk|week|weeks)?$/.exec(lower);
  if (lower === 'today' || lower === 'tomorrow' || rel) {
    var add = lower === 'today' ? 0 : lower === 'tomorrow' ? 1 : +rel[1] * (/^w/.test(rel[2] || '') ? 7 : 1);
    r.kind = 'once';
    r.date = civilFromDays(t + add);
    return r;
  }
  for (var w = 0; w < 7; w++) {
    if (lower === WDAY_NAME[w].toLowerCase() || lower === 'next ' + WDAY_NAME[w].toLowerCase()) {
      r.kind = 'once';
      r.date = civilFromDays(t + 1 + mod(w - dow(t + 1), 7));
      return r;
    }
  }
  if (/^\d/.test(s)) {
    r.kind = 'once';
    r.date = parseDate(s);
    return r.date ? r : null;
  }
  var match = /^([a-z]+)(?:=([^/@]*))?(?:\/(\d+))?(?:@(.*))?$/i.exec(s);
  if (!match) return null;
  var kind = match[1].toLowerCase();
  var spec = match[2];
  if (kind === 'weekdays' || kind === 'weekends') {
    r.kind = 'weekly';
    r.dow = kind === 'weekdays' ? DOW_WEEKDAYS : DOW_WEEKEND;
  } else if (kind === 'everyday') {
    r.kind = 'daily';
  } else {
    if (KINDS.indexOf(kind) < 0) return null;
    r.kind = kind;
  }
  if (spec !== undefined && kind !== 'weekdays' && kind !== 'weekends') {
    var v;
    switch (r.kind) {
      case 'once':
        r.date = parseDate(spec);
        if (!r.date) return null;
        break;
      case 'weekly': {
        var rest = spec.replace(/[.+\s,]/g, '');
        if (rest.length % 2) return null;
        for (var i = 0; i < rest.length; i += 2) {
          var code = dayCode(rest.slice(i, i + 2));
          if (code < 0) return null;
          r.dow |= 1 << code;
        }
        break;
      }
      case 'monthly':
        if (!/^-?\d+$/.test(spec)) return null;
        v = +spec;
        if (!v || v > 31 || v < -31) return null;
        r.mday = v;
        break;
      case 'nth': {
        var nm = /^(-?\d)([a-z]{2})$/i.exec(spec);
        if (!nm) return null;
        v = +nm[1];
        if (!v || v > 5 || v < -5 || dayCode(nm[2]) < 0) return null;
        r.nth = v;
        r.wday = dayCode(nm[2]);
        break;
      }
      case 'yearly': {
        var ym = /^(\d{1,2})-(\d{1,2})$/.exec(spec);
        if (!ym) return null;
        var mm = +ym[1];
        var dd = +ym[2];
        if (mm < 1 || mm > 12 || dd < 1 || dd > daysInMonth(2024, mm)) return null;
        r.month = mm;
        r.mday = dd;
        break;
      }
      case 'yday':
        if (!/^\d+$/.test(spec)) return null;
        v = +spec;
        if (v < 1 || v > 366) return null;
        r.yday = v;
        break;
      default:
        break;
    }
  } else if (r.kind === 'once') {
    return null;
  }
  if (match[3] !== undefined) {
    var n = +match[3];
    if (n < 1 || n > 65535) return null;
    r.interval = n;
  }
  if (match[4] !== undefined && r.kind !== 'once') {
    r.date = parseDate(match[4]);
    if (!r.date) return null;
  }
  return normalizeRule(r);
}

function formatRule(r) {
  var out = r.kind;
  switch (r.kind) {
    case 'once':
      return 'once=' + formatDate(r.date);
    case 'weekly': {
      var days = [];
      var mask = r.dow & DOW_ALL || DOW_ALL;
      for (var i = 0; i < 7; i++) if (mask & (1 << i)) days.push(WDAY_CODE[i]);
      out += '=' + days.join('.');
      break;
    }
    case 'monthly':
      out += '=' + r.mday;
      break;
    case 'nth':
      out += '=' + r.nth + WDAY_CODE[r.wday];
      break;
    case 'yearly':
      out += '=' + pad(r.month) + '-' + pad(r.mday);
      break;
    case 'yday':
      out += '=' + r.yday;
      break;
    default:
      break;
  }
  if (r.interval > 1) out += '/' + r.interval;
  if (r.date) out += '@' + formatDate(r.date);
  return out;
}

function ordinal(n) {
  var s = (Math.floor(n / 10) % 10 === 1) ? 'th' : ({ 1: 'st', 2: 'nd', 3: 'rd' }[n % 10] || 'th');
  return n + s;
}

function describeRule(r) {
  var n = r.interval || 1;
  var every = function (unit) {
    return n === 1 ? '' : ', every ' + n + ' ' + unit;
  };
  switch (r.kind) {
    case 'once': {
      if (!r.date) return 'Once';
      var d = dateToDays(r.date);
      return 'Once, ' + WDAY_ABBR[dow(d)] + ' ' + r.date.d + ' ' + MONTH_ABBR[r.date.m - 1] + ' ' + r.date.y;
    }
    case 'daily':
      return n === 1 ? 'Every day' : 'Every ' + n + ' days';
    case 'weekly': {
      var mask = r.dow & DOW_ALL || DOW_ALL;
      var label;
      if (mask === DOW_ALL) label = 'Every day';
      else if (mask === DOW_WEEKDAYS) label = 'Weekdays';
      else if (mask === DOW_WEEKEND) label = 'Weekends';
      else {
        label = [];
        for (var i = 0; i < 7; i++) if (mask & (1 << i)) label.push(WDAY_ABBR[i]);
        label = label.join(' ');
      }
      return n === 1 ? label : 'Every ' + n + ' weeks: ' + label;
    }
    case 'monthly':
      if (r.mday === -1) return 'Last day of the month' + every('months');
      if (r.mday < 0) return ordinal(-r.mday) + ' to last day of the month' + every('months');
      return ordinal(r.mday) + ' of the month' + every('months');
    case 'nth':
      return (r.nth === -1 ? 'Last' : r.nth < 0 ? ordinal(-r.nth) + ' to last' : ordinal(r.nth)) + ' ' +
        WDAY_NAME[r.wday] + ' of the month' + every('months');
    case 'yearly':
      return r.mday + ' ' + MONTH_ABBR[r.month - 1] + ' every year' + (n > 1 ? ' (every ' + n + ')' : '');
    case 'yday':
      return 'Day ' + r.yday + ' of the year' + every('years');
    default:
      return '?';
  }
}

function dayInMonth(r, y, m) {
  var dim = daysInMonth(y, m);
  if (r.kind === 'nth') {
    var first = daysFromCivil(y, m, 1);
    if (r.nth > 0) {
      var d = 1 + mod(r.wday - dow(first), 7) + (r.nth - 1) * 7;
      return d <= dim ? d : 0;
    }
    var last = dim - mod(dow(first + dim - 1) - r.wday, 7) + (r.nth + 1) * 7;
    return last >= 1 ? last : 0;
  }
  if (r.mday > 0) return Math.min(r.mday, dim);
  return Math.max(1, dim + 1 + r.mday);
}

/** The first day >= from that the rule picks, or null. */
function ruleNextDay(r, from) {
  var anchored = !!r.date;
  var anchor = anchored ? dateToDays(r.date) : 0;
  var n = Math.max(1, r.interval || 1);
  if (r.kind === 'once') return anchored && anchor >= from ? anchor : null;
  var start = anchored && anchor > from ? anchor : from;
  var guard;
  switch (r.kind) {
    case 'daily':
      return n <= 1 ? start : anchor + fdiv(start - anchor + n - 1, n) * n;
    case 'weekly': {
      var mask = r.dow & DOW_ALL || DOW_ALL;
      var baseMonday = anchor - dow(anchor);
      var d = start;
      for (guard = 0; guard < 40; guard++) {
        var week = fdiv(d - baseMonday, 7);
        var phase = mod(week, n);
        if (phase !== 0) {
          d = baseMonday + (week + (n - phase)) * 7;
          continue;
        }
        if (mask & (1 << dow(d))) return d;
        d++;
      }
      return null;
    }
    case 'monthly':
    case 'nth': {
      var s = civilFromDays(start);
      var mi = s.y * 12 + (s.m - 1);
      var ami = anchored ? r.date.y * 12 + (r.date.m - 1) : 0;
      mi += mod(ami - mi, n);
      for (guard = 0; guard < 120; guard++, mi += n) {
        var y = Math.floor(mi / 12);
        var m = (mi % 12) + 1;
        if (y > 2199) break;
        var dd = dayInMonth(r, y, m);
        if (!dd) continue;
        var cand = daysFromCivil(y, m, dd);
        if (cand >= start) return cand;
      }
      return null;
    }
    case 'yearly':
    case 'yday': {
      var sy = civilFromDays(start).y;
      sy += mod((anchored ? r.date.y : 0) - sy, n);
      for (guard = 0; guard < 6; guard++, sy += n) {
        if (sy > 2199) break;
        var c;
        if (r.kind === 'yearly') {
          c = daysFromCivil(sy, r.month, dayInMonth(r, sy, r.month));
        } else {
          c = daysFromCivil(sy, 1, 1) + Math.min(r.yday, isLeap(sy) ? 366 : 365) - 1;
        }
        if (c >= start) return c;
      }
      return null;
    }
    default:
      return null;
  }
}

// ---------------------------------------------------------------- CSV

function parseCsvLine(line) {
  var fields = [];
  var s = String(line);
  var i = 0;
  while (i <= s.length) {
    while (s[i] === ' ') i++;
    var field = '';
    if (s[i] === '"') {
      i++;
      while (i < s.length) {
        if (s[i] === '"') {
          if (s[i + 1] === '"') {
            field += '"';
            i += 2;
            continue;
          }
          i++;
          break;
        }
        field += s[i++];
      }
      while (i < s.length && s[i] !== ',') i++;
    } else {
      while (i < s.length && s[i] !== ',') field += s[i++];
      field = field.replace(/\s+$/, '');
    }
    fields.push(field);
    if (s[i] !== ',') break;
    i++;
    if (i === s.length) break;  // a trailing comma ends the row
  }
  return fields;
}

function csvField(value) {
  var s = String(value === undefined || value === null ? '' : value).replace(/[\r\n]+/g, ' ');
  if (/[",]/.test(s) || /^\s|\s$/.test(s)) return '"' + s.replace(/"/g, '""') + '"';
  return s;
}

/** Lines of a CSV document that hold rows (no blanks, comments or headers). */
function rowsFromText(text) {
  return String(text || '')
    .split(/\r?\n/)
    .map(function (l) { return l.trim(); })
    .filter(function (l) { return l && l[0] !== '#' && !/^"?id"?\s*,\s*"?on"?\s*,/i.test(l); });
}

// ---------------------------------------------------------------- alarms

function newAlarm() {
  return {
    id: 0, on: true, minute: 480, rule: newRule('daily'), msg: '', snoozeA: 0, snoozeB: 0, vibe: '',
    strength: 0, sound: '', ramp: false, done: '', every: 0, last: 1439, until: null, color: '',
  };
}

/**
 * Parse one alarm row. Returns the alarm or throws an Error saying which
 * column is wrong (handy for importing and for tools/tt.js).
 */
function parseAlarm(row, now) {
  var f = parseCsvLine(row);
  var a = newAlarm();
  var bad = function (col, why) {
    throw new Error(col + ': ' + why);
  };
  if (f[0]) {
    if (!/^\d+$/.test(f[0]) || +f[0] > 4294967295) bad('id', 'must be a whole number');
    a.id = +f[0];
  }
  if (f[1]) a.on = !/^(0|n|no|f|false|off)$/i.test(f[1]);
  if (!f[2]) bad('time', 'is required');
  a.minute = parseClock(f[2]);
  if (a.minute === null) bad('time', '"' + f[2] + '" is not a time of day');
  if (f[3]) {
    a.rule = parseRule(f[3], now);
    if (!a.rule) bad('rule', '"' + f[3] + '" is not a rule I know');
    // A plain weekday name or "tomorrow" means the next one at that time.
  }
  a.msg = (f[4] || '').slice(0, MSG_MAX);
  if (f[5]) {
    a.snoozeA = parseDuration(f[5]);
    if (a.snoozeA === null) bad('snooze_a', '"' + f[5] + '" is not a length of time');
  }
  if (f[6]) {
    a.snoozeB = parseDuration(f[6]);
    if (a.snoozeB === null) bad('snooze_b', '"' + f[6] + '" is not a length of time');
  }
  a.vibe = (f[7] || '').slice(0, NAME_MAX);
  if (f[8]) {
    if (!/^\d+$/.test(f[8])) bad('strength', 'must be 1 to 100');
    a.strength = Math.min(100, +f[8]);
  }
  a.sound = (f[9] || '').slice(0, NAME_MAX);
  var flags = (f[10] || '').toLowerCase();
  a.ramp = flags.indexOf('r') >= 0;
  a.done = /o/.test(flags) ? 'keep' : /a/.test(flags) ? 'archive' : /d/.test(flags) ? 'delete' : '';
  if (f[11]) {
    a.every = parseDuration(f[11]);
    if (a.every === null) bad('every', '"' + f[11] + '" is not a length of time');
    a.every = Math.min(1439, a.every);
  }
  if (f[12]) {
    a.last = parseClock(f[12]);
    if (a.last === null) bad('last', '"' + f[12] + '" is not a time of day');
  }
  if (f[13]) {
    a.until = parseDate(f[13]);
    if (!a.until) bad('until', '"' + f[13] + '" is not a date (YYYY-MM-DD)');
  }
  if (f[14]) {
    var hex = f[14].replace(/^#/, '');
    if (!/^[0-9a-f]{6}$/i.test(hex)) bad('color', 'must be RRGGBB');
    a.color = hex.toLowerCase();
  }
  if (a.every && a.last < a.minute) a.last = 1439;
  return a;
}

function tryParseAlarm(row, now) {
  try {
    return parseAlarm(row, now);
  } catch (e) {
    return null;
  }
}

function formatAlarm(a) {
  var flags = (a.ramp ? 'r' : '') + ({ keep: 'o', archive: 'a', delete: 'd' }[a.done] || '');
  return [
    String(a.id || 0),
    a.on ? '1' : '0',
    formatClock(a.minute),
    csvField(formatRule(a.rule)),
    csvField(a.msg || ''),
    a.snoozeA ? String(a.snoozeA) : '',
    a.snoozeB ? String(a.snoozeB) : '',
    csvField(a.vibe || ''),
    a.strength ? String(a.strength) : '',
    csvField(a.sound || ''),
    flags,
    a.every ? String(a.every) : '',
    a.every && a.last < 1439 ? formatClock(a.last) : '',
    a.until ? formatDate(a.until) : '',
    a.color || '',
  ].join(',');
}

/** The first ring strictly after `after` (epoch seconds), ignoring snoozes. 0 = never. */
function nextOccurrence(a, after) {
  var here = epochToLocal(after);
  var until = a.until ? dateToDays(a.until) : Infinity;
  var first = a.minute;
  var last = a.every ? (a.last >= first ? a.last : 1439) : first;
  var d = here.day;
  for (var guard = 0; guard < 16; guard++) {
    d = ruleNextDay(a.rule, d);
    if (d === null || d > until) return 0;
    var m = first;
    if (a.every && d === here.day && here.minute > first) m = first + Math.floor((here.minute - first) / a.every) * a.every;
    for (; m <= last; m += a.every || 1440) {
      var t = localToEpoch(d, m);
      if (t > after) return t;
    }
    d++;
  }
  return 0;
}

/** Plain-English one-liner: "07:30 weekdays - Wake up". */
function describeAlarm(a, h24) {
  var s = formatClock(a.minute, h24) + ' ' + describeRule(a.rule);
  if (a.every) s += ', then every ' + formatDuration(a.every) + (a.last < 1439 ? ' until ' + formatClock(a.last, h24) : '');
  return s + (a.msg ? ' - ' + a.msg : '');
}

// ---------------------------------------------------------------- patterns, tunes, settings

function parsePattern(row) {
  var f = parseCsvLine(row);
  if (!f[0] || f[1] === undefined) return null;
  var segs = f[1].trim().split(/[\s.;]+/).filter(Boolean).map(Number);
  if (!segs.length || segs.some(function (v) { return !(v >= 0) || v % 1; })) return null;
  segs = segs.slice(0, 40).map(function (v) { return Math.min(v, 10000); });
  while (segs.length && segs.length % 2 === 0) segs.pop();
  if (!segs.length || !segs[0]) return null;
  return { name: f[0].slice(0, NAME_MAX), segs: segs };
}

function formatPattern(p) {
  return csvField(p.name) + ',' + p.segs.join(' ');
}

function parseTune(row) {
  var f = parseCsvLine(row);
  if (!f[0] || f[1] === undefined) return null;
  var notes = [];
  var parts = f[1].trim().split(/[\s;]+/).filter(Boolean);
  for (var i = 0; i < parts.length && notes.length < 40; i++) {
    var m = /^(\d+):(\d+)$/.exec(parts[i]);
    if (!m || +m[1] > 127 || +m[2] <= 0) return null;
    notes.push({ midi: +m[1], ms: Math.min(10000, +m[2]) });
  }
  return notes.length ? { name: f[0].slice(0, NAME_MAX), notes: notes } : null;
}

function formatTune(t) {
  return csvField(t.name) + ',' + t.notes.map(function (n) { return n.midi + ':' + n.ms; }).join(' ');
}

function parseSettings(text) {
  var out = {};
  String(text || '').split(';').forEach(function (pair) {
    var i = pair.indexOf('=');
    if (i > 0) out[pair.slice(0, i).trim()] = pair.slice(i + 1).trim();
  });
  return out;
}

function formatSettings(obj) {
  return Object.keys(obj).map(function (k) { return k + '=' + obj[k]; }).join(';');
}

module.exports = {
  HEADER: HEADER,
  COLUMNS: COLUMNS,
  KINDS: KINDS,
  WDAY_CODE: WDAY_CODE,
  WDAY_ABBR: WDAY_ABBR,
  WDAY_NAME: WDAY_NAME,
  MONTH_ABBR: MONTH_ABBR,
  DOW_ALL: DOW_ALL,
  DOW_WEEKDAYS: DOW_WEEKDAYS,
  DOW_WEEKEND: DOW_WEEKEND,
  MSG_MAX: MSG_MAX,
  NAME_MAX: NAME_MAX,
  isLeap: isLeap,
  daysInMonth: daysInMonth,
  daysFromCivil: daysFromCivil,
  civilFromDays: civilFromDays,
  dateToDays: dateToDays,
  dow: dow,
  today: today,
  parseDate: parseDate,
  formatDate: formatDate,
  localToEpoch: localToEpoch,
  epochToLocal: epochToLocal,
  parseClock: parseClock,
  formatClock: formatClock,
  parseDuration: parseDuration,
  formatDuration: formatDuration,
  newRule: newRule,
  normalizeRule: normalizeRule,
  parseRule: parseRule,
  formatRule: formatRule,
  describeRule: describeRule,
  ruleNextDay: ruleNextDay,
  parseCsvLine: parseCsvLine,
  csvField: csvField,
  rowsFromText: rowsFromText,
  newAlarm: newAlarm,
  parseAlarm: parseAlarm,
  tryParseAlarm: tryParseAlarm,
  formatAlarm: formatAlarm,
  nextOccurrence: nextOccurrence,
  describeAlarm: describeAlarm,
  parsePattern: parsePattern,
  formatPattern: formatPattern,
  parseTune: parseTune,
  formatTune: formatTune,
  parseSettings: parseSettings,
  formatSettings: formatSettings,
};
