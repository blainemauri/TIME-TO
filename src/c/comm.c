#include "comm.h"

#include "buzz.h"
#include "main.h"
#include "sched.h"
#include "store.h"
#include "ui/ring.h"
#include "ui/ui.h"

// Operations (TT_OP). Phone -> watch:
#define OP_PULL 1     // send me everything
#define OP_PUT 2      // add or replace the rows in TT_TEXT (one per line) of kind TT_KIND
#define OP_DEL 3      // delete: TT_TEXT lists ids (alarms, archive) or names (patterns, tunes)
#define OP_ARCHIVE 4  // move the alarms with these ids into the archive
#define OP_RESTORE 5  // bring the archived alarms with these ids back
#define OP_PLAY 6     // try it on the wrist: a pattern row, a tune row or an alarm id
// Watch -> phone:
#define OP_DUMP_BEGIN 10
#define OP_DUMP_ROWS 11
#define OP_DUMP_END 12

// Row kinds (TT_KIND).
enum {
  KIND_ALARMS = 0,
  KIND_PATTERNS = 1,
  KIND_TUNES = 2,
  KIND_ARCHIVE = 3,
  KIND_SETTINGS = 4,
  KIND_STATE = 5,   // id,next,snooze_until,skip_until,missed  (read-only, watch -> phone)
  KIND_AGENDA = 6,  // when,id,snoozed                         (read-only, watch -> phone)
  KIND_COUNT
};

#define CHUNK_MAX 900
#define AGENDA_ROWS 24

static bool s_phone_seen;
static bool s_busy;      // a message is in flight
static bool s_dumping;
static bool s_dirty;     // something changed since the last dump started
static int s_phase;      // 0 = begin, 1 = rows, 2 = end
static int s_kind;
static int s_index;
static int s_retries;
static uint32_t s_dump_gen;
static AppTimer *s_retry_timer;
static char s_text[CHUNK_MAX];
static char s_line[TT_ROW_MAX];
static TTOccurrence s_agenda[AGENDA_ROWS];
static int s_agenda_count;

bool comm_phone_seen(void) {
  return s_phone_seen;
}

// ---------------------------------------------------------------- dump

//! Produce row `index` of `kind` into s_line. Returns false past the end;
//! an empty line means "nothing in this slot, keep going".
static bool prv_row(int kind, int index) {
  s_line[0] = '\0';
  switch (kind) {
    case KIND_ALARMS: {
      if (index >= TT_MAX_ALARMS) {
        return false;
      }
      TTAlarm *a = store_alarm(index);
      if (a) {
        tt_alarm_format(a, s_line, sizeof(s_line));
      }
      return true;
    }
    case KIND_PATTERNS: {
      TTPattern *p = store_pattern(index);
      if (!p) {
        return false;
      }
      tt_pattern_format(p, s_line, sizeof(s_line));
      return true;
    }
    case KIND_TUNES: {
      TTTune *t = store_tune(index);
      if (!t) {
        return false;
      }
      tt_tune_format(t, s_line, sizeof(s_line));
      return true;
    }
    case KIND_ARCHIVE:
      if (index >= store_archive_count()) {
        return false;
      }
      store_archive_row(index, s_line, sizeof(s_line));
      return true;
    case KIND_SETTINGS:
      if (index > 0) {
        return false;
      }
      tt_settings_format(store_settings(), s_line, sizeof(s_line));
      return true;
    case KIND_STATE: {
      if (index >= TT_MAX_ALARMS) {
        return false;
      }
      TTAlarm *a = store_alarm(index);
      if (a) {
        snprintf(s_line, sizeof(s_line), "%lu,%ld,%ld,%ld,%d", (unsigned long)a->id,
                 (long)a->next, (long)a->snooze_until,
                 (long)(sched_is_skipping(a) ? a->skip_until : 0), (int)a->missed);
      }
      return true;
    }
    case KIND_AGENDA: {
      if (index == 0) {
        s_agenda_count = sched_agenda(s_agenda, AGENDA_ROWS);
      }
      if (index >= s_agenda_count) {
        return false;
      }
      TTAlarm *a = store_alarm(s_agenda[index].slot);
      if (a) {
        snprintf(s_line, sizeof(s_line), "%ld,%lu,%d", (long)s_agenda[index].when,
                 (unsigned long)a->id, (int)s_agenda[index].snoozed);
      }
      return true;
    }
    default:
      return false;
  }
}

static void prv_dump_step(void);

static void prv_retry(void *unused) {
  s_retry_timer = NULL;
  prv_dump_step();
}

static void prv_dump_abort(void) {
  s_dumping = false;
  s_busy = false;
}

static bool prv_send(int op, int kind, const char *text) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  dict_write_int32(iter, MESSAGE_KEY_TT_OP, op);
  dict_write_int32(iter, MESSAGE_KEY_TT_KIND, kind);
  dict_write_int32(iter, MESSAGE_KEY_TT_GEN, (int32_t)s_dump_gen);
  if (text) {
    dict_write_cstring(iter, MESSAGE_KEY_TT_TEXT, text);
  }
  if (app_message_outbox_send() != APP_MSG_OK) {
    return false;
  }
  s_busy = true;
  return true;
}

static void prv_dump_step(void) {
  if (!s_dumping || s_busy) {
    return;
  }
  bool ok = true;
  if (s_phase == 0) {
    ok = prv_send(OP_DUMP_BEGIN, 0, NULL);
    if (ok) {
      s_phase = 1;
      s_kind = 0;
      s_index = 0;
    }
  } else if (s_phase == 1) {
    // Pack as many rows of the current kind as fit into one message.
    size_t len = 0;
    int kind = s_kind;
    int index = s_index;
    s_text[0] = '\0';
    while (kind < KIND_COUNT) {
      if (!prv_row(kind, index)) {
        if (len > 0) {
          break;  // send what we have for this kind first
        }
        kind++;
        index = 0;
        continue;
      }
      size_t row_len = strlen(s_line);
      if (row_len == 0) {
        index++;
        continue;
      }
      if (len + row_len + 2 > sizeof(s_text)) {
        break;
      }
      if (len) {
        s_text[len++] = '\n';
      }
      memcpy(s_text + len, s_line, row_len + 1);
      len += row_len;
      index++;
    }
    if (len == 0) {
      s_phase = 2;  // every kind sent
      prv_dump_step();
      return;
    }
    ok = prv_send(OP_DUMP_ROWS, kind, s_text);
    if (ok) {
      s_kind = kind;
      s_index = index;
    }
  } else {
    ok = prv_send(OP_DUMP_END, 0, NULL);
    if (ok) {
      s_dumping = false;
    }
  }
  if (!ok) {
    if (++s_retries > 6) {
      prv_dump_abort();
    } else if (!s_retry_timer) {
      s_retry_timer = app_timer_register(700, prv_retry, NULL);
    }
  } else {
    s_retries = 0;
  }
}

static void prv_dump_start(void) {
  if (s_dumping) {
    s_dirty = true;  // finish this one, then go again
    return;
  }
  s_dirty = false;
  s_dumping = true;
  s_phase = 0;
  s_retries = 0;
  s_dump_gen = store_generation();
  prv_dump_step();
}

void comm_changed(void) {
  if (s_phone_seen) {
    prv_dump_start();
  }
}

void comm_sync_now(void) {
  s_phone_seen = true;
  prv_dump_start();
}

// ---------------------------------------------------------------- incoming

static uint32_t prv_parse_id(const char *text) {
  uint32_t id = 0;
  for (const char *s = text; *s >= '0' && *s <= '9'; s++) {
    id = id * 10 + (uint32_t)(*s - '0');
  }
  return id;
}

static void prv_put(int kind, const char *line) {
  switch (kind) {
    case KIND_ALARMS: {
      static TTAlarm incoming;
      static char existing_row[TT_ROW_MAX];
      static char incoming_row[TT_ROW_MAX];
      if (!tt_alarm_parse(line, &incoming)) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "alarm row rejected");
        return;
      }
      TTAlarm *a = store_alarm_by_id(incoming.id);
      if (a) {
        // Unchanged rows must not disturb a snooze in progress.
        tt_alarm_format(a, existing_row, sizeof(existing_row));
        tt_alarm_format(&incoming, incoming_row, sizeof(incoming_row));
        if (strcmp(existing_row, incoming_row) == 0) {
          return;
        }
        incoming.handled_until = a->handled_until;
        *a = incoming;
        sched_edited(a);
      } else {
        incoming.handled_until = time(NULL);
        a = store_alarm_add(&incoming);
        if (a) {
          sched_edited(a);
        }
      }
      break;
    }
    case KIND_PATTERNS: {
      static TTPattern p;
      if (tt_pattern_parse(line, &p)) {
        store_pattern_put(&p);
      }
      break;
    }
    case KIND_TUNES: {
      static TTTune t;
      if (tt_tune_parse(line, &t)) {
        store_tune_put(&t);
      }
      break;
    }
    case KIND_ARCHIVE: {
      static TTAlarm a;
      if (tt_alarm_parse(line, &a)) {
        if (a.id == 0) {
          a.id = store_new_id();
        }
        store_archive_add(&a);
      }
      break;
    }
    case KIND_SETTINGS:
      if (tt_settings_parse(line, store_settings())) {
        store_settings_save();
      }
      break;
    default:
      break;
  }
}

static void prv_del(int kind, const char *line) {
  switch (kind) {
    case KIND_ALARMS: {
      TTAlarm *a = store_alarm_by_id(prv_parse_id(line));
      if (a) {
        store_alarm_delete(a);
      }
      break;
    }
    case KIND_PATTERNS:
      store_pattern_delete(line);
      break;
    case KIND_TUNES:
      store_tune_delete(line);
      break;
    case KIND_ARCHIVE: {
      int index = store_archive_find(prv_parse_id(line));
      if (index >= 0) {
        store_archive_delete(index);
      }
      break;
    }
    default:
      break;
  }
}

static void prv_play(int kind, const char *line) {
  if (ring_showing()) {
    return;
  }
  if (kind == KIND_PATTERNS) {
    static TTPattern p;
    if (tt_pattern_parse(line, &p)) {
      buzz_preview_pattern(&p, 0);
    }
  } else if (kind == KIND_TUNES) {
    static TTTune t;
    if (tt_tune_parse(line, &t)) {
      buzz_preview_tune(&t);
    }
  } else if (kind == KIND_ALARMS) {
    TTAlarm *a = store_alarm_by_id(prv_parse_id(line));
    if (a) {
      ring_show(a, true);
    }
  }
}

static void prv_apply(int op, int kind, const char *text) {
  // TT_TEXT holds one row per line.
  const char *p = text;
  while (*p) {
    size_t n = 0;
    while (*p && *p != '\n') {
      if (n + 1 < sizeof(s_line)) {
        s_line[n++] = *p;
      }
      p++;
    }
    if (*p == '\n') {
      p++;
    }
    while (n > 0 && (s_line[n - 1] == '\r' || s_line[n - 1] == ' ')) {
      n--;
    }
    s_line[n] = '\0';
    if (n == 0) {
      continue;
    }
    switch (op) {
      case OP_PUT:
        prv_put(kind, s_line);
        break;
      case OP_DEL:
        prv_del(kind, s_line);
        break;
      case OP_ARCHIVE: {
        TTAlarm *a = store_alarm_by_id(prv_parse_id(s_line));
        if (a) {
          store_alarm_archive(a);
        }
        break;
      }
      case OP_RESTORE: {
        int index = store_archive_find(prv_parse_id(s_line));
        if (index >= 0) {
          TTAlarm *a = store_archive_restore(index);
          if (a) {
            sched_edited(a);
          }
        }
        break;
      }
      case OP_PLAY:
        prv_play(kind, s_line);
        break;
      default:
        break;
    }
  }
}

static void prv_inbox(DictionaryIterator *iter, void *context) {
  Tuple *op_t = dict_find(iter, MESSAGE_KEY_TT_OP);
  if (!op_t) {
    return;
  }
  s_phone_seen = true;
  const int op = (int)op_t->value->int32;
  Tuple *kind_t = dict_find(iter, MESSAGE_KEY_TT_KIND);
  Tuple *text_t = dict_find(iter, MESSAGE_KEY_TT_TEXT);
  const int kind = kind_t ? (int)kind_t->value->int32 : 0;

  if (op == OP_PULL) {
    prv_dump_start();
    return;
  }
  if (text_t && text_t->type == TUPLE_CSTRING) {
    prv_apply(op, kind, text_t->value->cstring);
  }
  if (op != OP_PLAY) {
    ui_data_changed();
    main_changed();
  }
}

static void prv_outbox_sent(DictionaryIterator *iter, void *context) {
  s_busy = false;
  if (s_dumping) {
    prv_dump_step();
  } else if (s_dirty) {
    prv_dump_start();
  }
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_busy = false;
  if (!s_dumping) {
    return;
  }
  if (++s_retries > 6) {
    prv_dump_abort();
    return;
  }
  if (!s_retry_timer) {
    s_retry_timer = app_timer_register(700, prv_retry, NULL);
  }
}

void comm_init(void) {
  app_message_register_inbox_received(prv_inbox);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_register_outbox_failed(prv_outbox_failed);
  uint32_t in = app_message_inbox_size_maximum();
  uint32_t out = app_message_outbox_size_maximum();
  app_message_open(in > 1200 ? 1200 : in, out > 1200 ? 1200 : out);
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}
