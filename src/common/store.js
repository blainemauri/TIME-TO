// localStorage shared by PebbleKit JS and the plugin (they share one store).
/* eslint-disable no-var */
'use strict';

var SNAPSHOT = 'tt.snapshot';
var OUTBOX = 'tt.outbox';
var PINS = 'tt.pins';

function read(key, fallback) {
  try {
    var text = localStorage.getItem(key);
    return text ? JSON.parse(text) : fallback;
  } catch (e) {
    return fallback;
  }
}

function write(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch (e) {
    console.log('[TimeTo] could not save ' + key + ': ' + e);
  }
}

function emptySnapshot() {
  return { alarms: [], patterns: [], tunes: [], archive: [], settings: '', state: {}, agenda: [], updated: 0 };
}

function loadSnapshot() {
  var snap = read(SNAPSHOT, null);
  var empty = emptySnapshot();
  if (!snap) return empty;
  Object.keys(empty).forEach(function (k) {
    if (snap[k] === undefined) snap[k] = empty[k];
  });
  return snap;
}

module.exports = {
  emptySnapshot: emptySnapshot,
  loadSnapshot: loadSnapshot,
  saveSnapshot: function (snap) { write(SNAPSHOT, snap); },
  loadOutbox: function () { return read(OUTBOX, []); },
  saveOutbox: function (items) { write(OUTBOX, items); },
  loadPins: function () { return read(PINS, {}); },
  savePins: function (pins) { write(PINS, pins); },
};
