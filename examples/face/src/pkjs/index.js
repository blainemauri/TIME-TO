// Reads Time To's plugin source `alarms/upcoming` and sends the next few rings
// to the watchface. Needs the Pebble app's plugins preview switched on
// (Settings > Plugins) and Time To installed.
var TIME_TO = '78ff0b72-3e3c-45d8-bb73-45381a822bd6';
var ROWS = 3;
var lastSent = null;

function send(instances) {
  var rows = instances.slice(0, ROWS).map(function (inst) {
    var p = inst.properties || {};
    var when = p.time && p.time.timestamp ? p.time.timestamp.value : 0;
    var snoozed = p.snoozed && p.snoozed.boolean && p.snoozed.boolean.value ? 1 : 0;
    var name = (p.name && (p.name.longText || p.name.shortText) || { text: '' }).text;
    return when + '\t' + snoozed + '\t' + name.replace(/[\t\n]/g, ' ');
  }).join('\n');
  if (rows === lastSent) return;
  lastSent = rows;
  Pebble.sendAppMessage({ ROWS: rows }, function () {}, function () { lastSent = null; });
}

Pebble.addEventListener('ready', function () {
  if (typeof Pebble.subscribeToSource !== 'function') {
    console.log('This phone app has no plugin support yet');
    return;
  }
  Pebble.subscribeToSource({
    plugin: TIME_TO,
    category: 'alarms',
    item: 'upcoming',
    properties: ['name', 'time', 'snoozed'],
    limit: ROWS,
    onData: function (update) { send(update.instances || []); },
    onError: function (err) { console.log('Time To source: ' + err.code + ' ' + (err.message || '')); },
  });
});
