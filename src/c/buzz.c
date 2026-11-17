#include "buzz.h"

#include "store.h"

// ---------------------------------------------------------------- vibration
//
// Strength. The public vibration call only takes on/off durations, so a
// softer buzz is made by chopping each "on" stretch into short on/off slices:
// the lower the strength, the shorter the on part of each slice. Firmware
// that exports vibes_enqueue_custom_pattern_with_amplitudes() gets the real
// thing instead (wscript defines TT_HAVE_VIBE_AMPLITUDES when the SDK has it).
//
// Long patterns are fed to the firmware a chunk at a time from an app timer,
// which is also what lets a ringing alarm loop for as long as it needs to.

#define CHUNK_SEGS 24    // on/off entries handed to the firmware at once
#define PWM_PERIOD 40    // ms per slice when softening
#define PWM_MIN_ON 8     // ms: below this the motor barely moves
#define LOOP_GAP_MS 700  // silence between repeats of the pattern

static TTPattern s_pattern;
static uint8_t s_strength = 100;
static bool s_loop;
static bool s_ramp;
static bool s_active;
static int s_pass;
static int s_seg;
static uint32_t s_rem;
static AppTimer *s_timer;
static uint32_t s_chunk[CHUNK_SEGS];
#if defined(TT_HAVE_VIBE_AMPLITUDES)
static uint32_t s_amps[CHUNK_SEGS];
#endif

static int prv_duty(void) {
  static const uint8_t ramp[] = {35, 55, 75, 100};
  int duty = s_strength ? s_strength : 100;
  if (s_ramp) {
    int step = s_pass < (int)ARRAY_LENGTH(ramp) ? s_pass : (int)ARRAY_LENGTH(ramp) - 1;
    duty = duty * ramp[step] / 100;
  }
  if (duty < 5) {
    duty = 5;
  }
  return duty > 100 ? 100 : duty;
}

static void prv_run(void *unused) {
  s_timer = NULL;
  if (!s_active) {
    return;
  }
  const int duty = prv_duty();
  uint32_t n = 0;
  uint32_t total = 0;

  // A chunk has to begin with a buzz. If we are parked on a silence, sit it
  // out with the timer and come back.
  if (s_seg < s_pattern.n && (s_seg % 2 == 1)) {
    uint32_t wait = s_rem;
    s_seg++;
    s_rem = s_seg < s_pattern.n ? s_pattern.seg[s_seg] : 0;
    s_timer = app_timer_register(wait ? wait : 1, prv_run, NULL);
    return;
  }

  while (s_seg < s_pattern.n && n + 2 <= CHUNK_SEGS) {
    if (s_rem == 0) {
      s_seg++;
      s_rem = s_seg < s_pattern.n ? s_pattern.seg[s_seg] : 0;
      continue;
    }
    const bool on = (s_seg % 2 == 0);
    uint32_t ms = s_rem;
    bool emit_on = on;
#if !defined(TT_HAVE_VIBE_AMPLITUDES)
    if (on && duty < 100) {
      uint32_t on_ms = (uint32_t)(PWM_PERIOD * duty / 100);
      if (on_ms < PWM_MIN_ON) {
        on_ms = PWM_MIN_ON;
      }
      // Alternate short on and off slices inside this "on" stretch.
      const bool last_was_on = (n % 2 == 1);
      if (last_was_on) {
        emit_on = false;
        ms = PWM_PERIOD - on_ms;
      } else {
        ms = on_ms;
      }
      if (ms > s_rem) {
        ms = s_rem;
      }
    }
#endif
    const bool last_was_on = (n % 2 == 1);
    if (n > 0 && last_was_on == emit_on) {
      s_chunk[n - 1] += ms;  // same state as the previous entry: extend it
    } else {
      s_chunk[n++] = ms;
    }
    s_rem -= ms;
    total += ms;
  }

  // Entries alternate on, off, on ... so a trailing even count ends on "off":
  // keep its time in the timer but do not send a silence to the motor.
  uint32_t send = (n % 2 == 0 && n > 0) ? n - 1 : n;
  if (send > 0) {
    vibes_cancel();
#if defined(TT_HAVE_VIBE_AMPLITUDES)
    for (uint32_t i = 0; i < send; i++) {
      s_amps[i] = (i % 2 == 0) ? (uint32_t)duty : 0;
    }
    vibes_enqueue_custom_pattern_with_amplitudes((VibePatternWithAmplitudes){
        .durations = s_chunk,
        .amplitudes = s_amps,
        .num_segments = send,
    });
#else
    vibes_enqueue_custom_pattern((VibePattern){.durations = s_chunk, .num_segments = send});
#endif
  }

  if (s_seg >= s_pattern.n) {
    // End of the pattern.
    s_pass++;
    if (!s_loop) {
      s_active = false;
      return;
    }
    s_seg = 0;
    s_rem = s_pattern.seg[0];
    total += LOOP_GAP_MS;
  }
  s_timer = app_timer_register(total + 10, prv_run, NULL);
}

static void prv_vibe_start(const TTPattern *pattern, uint8_t strength, bool loop, bool ramp) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (!pattern || pattern->n == 0) {
    s_active = false;
    return;
  }
  s_pattern = *pattern;
  s_strength = strength;
  s_loop = loop;
  s_ramp = ramp;
  s_pass = 0;
  s_seg = 0;
  s_rem = s_pattern.seg[0];
  s_active = true;
  prv_run(NULL);
}

static void prv_vibe_stop(void) {
  s_active = false;
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  vibes_cancel();
}

// ---------------------------------------------------------------- sound

#if defined(PBL_SPEAKER)
#define SOUND_GAP_MS 700

static SpeakerNote s_notes[TT_TUNE_MAX_NOTES];
static uint32_t s_note_count;
static bool s_sound_loop;
static bool s_sound_ramp;
static int s_sound_pass;
static AppTimer *s_sound_timer;

static void prv_sound_play(void *unused) {
  s_sound_timer = NULL;
  if (!s_note_count) {
    return;
  }
  int volume = store_settings()->volume;
  if (s_sound_ramp) {
    static const uint8_t ramp[] = {30, 50, 75, 100};
    int step = s_sound_pass < (int)ARRAY_LENGTH(ramp) ? s_sound_pass : (int)ARRAY_LENGTH(ramp) - 1;
    volume = volume * ramp[step] / 100;
  }
  if (volume < 1) {
    return;
  }
  speaker_play_notes(s_notes, s_note_count, (uint8_t)volume);
}

static void prv_sound_finished(SpeakerFinishReason reason, void *context) {
  if (reason == SpeakerFinishReasonDone && s_sound_loop && s_note_count) {
    s_sound_pass++;
    s_sound_timer = app_timer_register(SOUND_GAP_MS, prv_sound_play, NULL);
  }
}

static void prv_sound_stop(void) {
  s_sound_loop = false;
  s_note_count = 0;
  if (s_sound_timer) {
    app_timer_cancel(s_sound_timer);
    s_sound_timer = NULL;
  }
  speaker_stop();
}

static void prv_sound_start(const TTTune *tune, bool loop, bool ramp) {
  prv_sound_stop();
  if (!tune || tune->n == 0 || store_settings()->volume == 0) {
    return;
  }
  for (int i = 0; i < tune->n; i++) {
    s_notes[i] = (SpeakerNote){
        .midi_note = tune->note[i].midi,
        .waveform = SpeakerWaveformSquare,
        .duration_ms = tune->note[i].ms,
        .velocity = 0,
        .reserved = 0,
    };
  }
  s_note_count = tune->n;
  s_sound_loop = loop;
  s_sound_ramp = ramp;
  s_sound_pass = 0;
  speaker_set_finish_callback(prv_sound_finished, NULL);
  prv_sound_play(NULL);
}

bool buzz_has_speaker(void) {
  return true;
}
#else
static void prv_sound_stop(void) {}
static void prv_sound_start(const TTTune *tune, bool loop, bool ramp) {}
bool buzz_has_speaker(void) {
  return false;
}
#endif

// ---------------------------------------------------------------- public

void buzz_start(const TTAlarm *alarm) {
  const TTSettings *settings = store_settings();
  const bool ramp = (alarm->flags & TT_F_RAMP) != 0;
  prv_vibe_start(store_pattern_resolve(alarm->vibe),
                 alarm->strength ? alarm->strength : settings->strength, true, ramp);
  prv_sound_start(store_tune_resolve(alarm->sound), true, ramp);
}

void buzz_stop(void) {
  prv_vibe_stop();
  prv_sound_stop();
}

void buzz_preview_pattern(const TTPattern *pattern, uint8_t strength) {
  prv_vibe_start(pattern, strength ? strength : store_settings()->strength, false, false);
}

void buzz_preview_tune(const TTTune *tune) {
  prv_sound_start(tune, false, false);
}

bool buzz_active(void) {
  return s_active;
}
