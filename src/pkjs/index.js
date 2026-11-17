// Time To, phone side (PebbleKit JS).
//
// * Keeps a copy of everything on the watch (`tt.snapshot` in localStorage),
//   refreshed whenever the watch app runs and anything changes.
// * Sends changes to the watch from the settings page and from the plugin
//   (`tt.outbox`, so a change made while the watch app is closed is delivered
//   the next time it opens).
// * Mirrors upcoming alarms into the timeline when that is switched on.
'use strict';

var tt = require('../common/ttrows.js');
var store = require('../common/store.js');

// Operations, matching src/c/comm.c.
var OP = { PULL: 1, PUT: 2, DEL: 3, ARCHIVE: 4, RESTORE: 5, PLAY: 6, DUMP_BEGIN: 10, DUMP_ROWS: 11, DUMP_END: 12 };
var KIND_NAMES = ['alarms', 'patterns', 'tunes', 'archive', 'settings', 'state', 'agenda'];
var CHUNK = 900;  // bytes of rows per message; the watch's inbox is 1200

var incoming = null;  // the dump being received
var sending = false;
var watchSeen = false;
var retryTimer = null;

function log(msg) {
  console.log('[TimeTo] ' + msg);
}

// ---------------------------------------------------------------- receiving

function onDumpRows(kind, text) {
  if (!incoming) incoming = store.emptySnapshot();
  var name = KIND_NAMES[kind];
  var lines = String(text || '').split('\n').filter(Boolean);
  if (name === 'settings') {
    incoming.settings = lines[0] || '';
  } else if (name === 'state') {
    lines.forEach(function (line) {
      var f = line.split(',');
      incoming.state[f[0]] = { next: +f[1], snooze: +f[2], skip: +f[3], missed: +f[4] };
    });
  } else if (name === 'agenda') {
    lines.forEach(function (line) {
      var f = line.split(',');
      incoming.agenda.push({ when: +f[0], id: +f[1], snoozed: f[2] === '1' });
    });
  } else if (incoming[name]) {
    Array.prototype.push.apply(incoming[name], lines);
  }
}

function onDumpEnd() {
  if (!incoming) return;
  incoming.updated = Date.now();
  store.saveSnapshot(incoming);
  incoming = null;
  log('snapshot: ' + store.loadSnapshot().alarms.length + ' alarms');
  updateTimeline();
  pushToPage({ type: 'snapshot' });
}

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  watchSeen = true;
  switch (p.TT_OP) {
    case OP.DUMP_BEGIN:
      incoming = store.emptySnapshot();
      break;
    case OP.DUMP_ROWS:
      onDumpRows(p.TT_KIND, p.TT_TEXT);
      break;
    case OP.DUMP_END:
      onDumpEnd();
      break;
    default:
      break;
  }
});

// ---------------------------------------------------------------- sending

/** Split rows into messages that fit the watch's inbox. */
function chunks(rows) {
  var out = [];
  var current = '';
  rows.forEach(function (row) {
    if (current && current.length + row.length + 1 > CHUNK) {
      out.push(current);
      current = '';
    }
    current = current ? current + '\n' + row : row;
  });
  if (current) out.push(current);
  return out;
}

function queue(op, kind, rows) {
  var outbox = store.loadOutbox();
  chunks(rows).forEach(function (text) {
    outbox.push({ op: op, kind: kind, text: text });
  });
  store.saveOutbox(outbox);
  pump();
}

function pump() {
  if (sending) return;
  var outbox = store.loadOutbox();
  if (!outbox.length) return;
  var item = outbox[0];
  sending = true;
  Pebble.sendAppMessage({ TT_OP: item.op, TT_KIND: item.kind || 0, TT_TEXT: item.text || '' },
    function () {
      sending = false;
      var now = store.loadOutbox();
      // Drop the item we sent (it is still first unless something else changed the queue).
      if (now.length && JSON.stringify(now[0]) === JSON.stringify(item)) now.shift();
      store.saveOutbox(now);
      pump();
    },
    function () {
      sending = false;
      // The watch app is not open or is busy; try again shortly.
      if (!retryTimer) {
        retryTimer = setTimeout(function () {
          retryTimer = null;
          pump();
        }, 3000);
      }
    });
}

function pull() {
  Pebble.sendAppMessage({ TT_OP: OP.PULL, TT_KIND: 0 }, function () {}, function () {});
}

// ---------------------------------------------------------------- optimistic updates
//
// The page shows a change straight away; the watch's next dump then replaces
// the snapshot with what the watch really holds.

function applyLocally(op, kind, rows) {
  var snap = store.loadSnapshot();
  var name = KIND_NAMES[kind];
  var keyOf = function (row) {
    return name === 'alarms' || name === 'archive' ? String(tt.parseCsvLine(row)[0]) : tt.parseCsvLine(row)[0];
  };
  if (op === OP.PUT && name === 'settings') {
    snap.settings = rows[0];
  } else if (op === OP.PUT && snap[name]) {
    rows.forEach(function (row) {
      var key = keyOf(row);
      var i = snap[name].findIndex(function (r) { return keyOf(r) === key; });
      if (i >= 0 && key !== '0' && key !== '') snap[name][i] = row;
      else snap[name].push(row);
    });
  } else if (op === OP.DEL && snap[name]) {
    snap[name] = snap[name].filter(function (r) { return rows.indexOf(keyOf(r)) < 0; });
  } else if (op === OP.ARCHIVE || op === OP.RESTORE) {
    var from = op === OP.ARCHIVE ? 'alarms' : 'archive';
    var to = op === OP.ARCHIVE ? 'archive' : 'alarms';
    snap[from] = snap[from].filter(function (r) {
      if (rows.indexOf(String(tt.parseCsvLine(r)[0])) < 0) return true;
      snap[to].push(r);
      return false;
    });
  }
  store.saveSnapshot(snap);
}

// ---------------------------------------------------------------- timeline

function updateTimeline() {
  if (typeof Pebble.insertTimelinePin !== 'function') return;
  var snap = store.loadSnapshot();
  var settings = tt.parseSettings(snap.settings);
  var old = store.loadPins();
  var keep = {};
  if (settings.timeline !== '0') {
    var names = {};
    snap.alarms.forEach(function (row) {
      var a = tt.tryParseAlarm(row);
      if (a) names[a.id] = a.msg || 'Alarm';
    });
    var horizon = Date.now() / 1000 + 7 * 86400;
    snap.agenda.filter(function (o) { return o.when < horizon; }).slice(0, 12).forEach(function (o) {
      var id = 'tt-' + o.id + '-' + o.when;
      keep[id] = true;
      if (old[id]) return;
      try {
        Pebble.insertTimelinePin({
          id: id,
          time: new Date(o.when * 1000).toISOString(),
          layout: {
            type: 'genericPin',
            title: names[o.id] || 'Alarm',
            subtitle: o.snoozed ? 'Time To (snoozed)' : 'Time To',
            tinyIcon: 'system://images/ALARM_CLOCK',
          },
        });
      } catch (e) {
        log('pin insert failed: ' + e);
      }
    });
  }
  Object.keys(old).forEach(function (id) {
    if (keep[id]) return;
    try {
      Pebble.deleteTimelinePin(id);
    } catch (e) {
      log('pin delete failed: ' + e);
    }
  });
  store.savePins(keep);
}

// ---------------------------------------------------------------- settings page

function pushToPage(message) {
  if (typeof Pebble.sendConfigMessage === 'function') {
    try {
      Pebble.sendConfigMessage(message);
    } catch (e) {
      /* the page is not open */
    }
  }
}

function status() {
  return {
    snapshot: store.loadSnapshot(),
    pending: store.loadOutbox().length,
    watchSeen: watchSeen,
  };
}

/** Requests from the settings page (src/config/config.src.html). */
function handlePageMessage(m) {
  var kind = KIND_NAMES.indexOf(m.kind);
  var rows = (m.rows || []).map(String).filter(Boolean);
  switch (m.type) {
    case 'hello':
    case 'snapshot':
      if (m.type === 'hello') pull();
      return status();
    case 'put':
    case 'del':
      if (kind < 0 || !rows.length) return { error: 'nothing to send' };
      queue(m.type === 'put' ? OP.PUT : OP.DEL, kind, rows);
      applyLocally(m.type === 'put' ? OP.PUT : OP.DEL, kind, rows);
      return status();
    case 'archive':
    case 'restore':
      queue(m.type === 'archive' ? OP.ARCHIVE : OP.RESTORE, 0, rows);
      applyLocally(m.type === 'archive' ? OP.ARCHIVE : OP.RESTORE, 0, rows);
      return status();
    case 'play':
      Pebble.sendAppMessage({ TT_OP: OP.PLAY, TT_KIND: kind < 0 ? 0 : kind, TT_TEXT: rows[0] || '' },
        function () {}, function () {});
      return { ok: true };
    case 'pull':
      pull();
      return status();
    default:
      return { error: 'unknown request ' + m.type };
  }
}

Pebble.addEventListener('configmessage', function (e) {
  var reply;
  try {
    reply = handlePageMessage(e.data || {});
  } catch (err) {
    reply = { error: String(err && err.message || err) };
  }
  e.respond(reply);
});

// ---------------------------------------------------------------- start

Pebble.addEventListener('ready', function () {
  log('ready, ' + store.loadOutbox().length + ' queued');
  pump();  // anything the plugin or the page queued while the watch app was closed
  pull();
});
