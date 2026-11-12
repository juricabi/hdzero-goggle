#include "audio_mixer.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>
#include <minIni.h>

#include "lang/language.h"
#include "ui/page_common.h"
#include "util/system.h"

#define AUDIO_MIXER_SECTION "mixer"
// 2: Record What I Hear and the Record source switches (rec_*) replaced the As Heard / Custom mode
// (dvr_mode, dvr_mic, dvr_line_in, dvr_av) of version 1, which was never released.
#define AUDIO_MIXER_VERSION 2

// The last batch stays there, handy to see what was sent to the codec.
#ifndef EMULATOR_BUILD
#define AUDIO_MIXER_CMD_FILE TMP_DIR "/audio_mixer.cmd"
#else
#define AUDIO_MIXER_CMD_FILE "audio_mixer.cmd"
#endif
#define AUDIO_MIXER_AMIXER "amixer -q -s < " AUDIO_MIXER_CMD_FILE

// How often the worker compares the codec with the last batch (audio_mixer_codec.c). A pass reads
// the controls libmedia_mpp's reset writes (reset_controls[], at most 12 ioctls, no process);
// every AUDIO_MIXER_WATCH_FULL passes, every control of the batch (about 36 ioctls).
#ifndef AUDIO_MIXER_WATCH_MS
#define AUDIO_MIXER_WATCH_MS 250
#endif
#define AUDIO_MIXER_WATCH_FULL 8
// After audio_mixer_recheck() (the record daemon was restarted): a full pass this often, this long.
#define AUDIO_MIXER_RECHECK_MS     (AUDIO_MIXER_WATCH_MS / 5)
#define AUDIO_MIXER_RECHECK_WINDOW (AUDIO_MIXER_WATCH_MS * 24)
// Applies in a row for the same state when the codec does not keep it; then it is applied again
// with a backoff (and when the state changes).
#define AUDIO_MIXER_WATCH_RETRIES 3
// Backoff (codec not readable, amixer failing, routing not kept): from this, doubling, up to the max.
#define AUDIO_MIXER_BACKOFF_MS     (AUDIO_MIXER_WATCH_MS * 8)
#define AUDIO_MIXER_BACKOFF_MAX_MS (AUDIO_MIXER_WATCH_MS * 240)

// The controls libmedia_mpp's alsaOpenMixer() writes whenever AW_MPI_SYS_Init() runs (record daemon
// start, DVR playback; found by disassembling libmedia_mpp.so, docs/audio.md): a reset changes one of
// them, so the quick pass reads only these. utilities/audio_mixer/codec_model.py LIBMEDIA_RESET.
static const char *const reset_controls[] = {
    "Lineout Switch", "DACL Mixer AIF1DA0L Switch", "Left Output Mixer DACL Switch", "AIF1 AD0L Mixer ADCL Switch",
    "LADC input Mixer MIC1 boost Switch", "lineout volume", "DAC volume", "LINEOUTL Mux", "LINEOUTR Mux",
    "MIC1 boost AMP gain control", "ADC input gain control", "ADC volume",
};

static const char *const field_key[AUDIO_MIXER_FIELD_COUNT] = {
    [AUDIO_MIXER_MIC] = "mic",
    [AUDIO_MIXER_LINE_IN] = "line_in",
    [AUDIO_MIXER_AV] = "av",
    [AUDIO_MIXER_MIC_BOOST] = "mic_boost",
    [AUDIO_MIXER_LINE_IN_BOOST] = "line_in_boost",
    [AUDIO_MIXER_HEADPHONES] = "headphones",
    [AUDIO_MIXER_PLAYBACK] = "playback",
    [AUDIO_MIXER_REC_MIX] = "rec_mix",
    [AUDIO_MIXER_REC_MIC] = "rec_mic",
    [AUDIO_MIXER_REC_LINE_IN] = "rec_line_in",
    [AUDIO_MIXER_REC_AV] = "rec_av",
    [AUDIO_MIXER_DVR_LEVEL] = "dvr_level",
    [AUDIO_MIXER_RX_AUDIO] = "rx_audio",
    [AUDIO_MIXER_HDMI] = "hdmi",
    [AUDIO_MIXER_PHONES_AT_BOOT] = "phones_at_boot",
};

// keys of version 1 (test builds only), removed when the section is migrated
static const char *const old_keys[] = {"dvr_mode", "dvr_mic", "dvr_line_in", "dvr_av"};

/*
 * Threads: the UI (page, source switches) changes `mixer` / `context` and asks for an apply;
 * a worker writes the latest state to the codec, so a burst of requests (dial steps) ends
 * in one or two amixer runs and never waits for amixer (and the Goggle 2 driver's 160 ms
 * PA sleep) in the input thread. setting.ini is written by audio_mixer_save() only, from
 * the UI like every other setting.
 */
static pthread_mutex_t mixer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t batch_mutex = PTHREAD_MUTEX_INITIALIZER; // one amixer batch at a time
static pthread_cond_t work_cond;                                 // worker: something to apply
static pthread_cond_t done_cond;                                 // audio_mixer_apply_wait()
static audio_mixer_settings_t mixer;                             // current values
static audio_mixer_settings_t saved;                             // values in setting.ini (UI)
static audio_mixer_context_t context = {.line_out = false, .playback = false, .live_dac = false};
static unsigned requested; // apply generations: requested by the UI or the watcher ...
static unsigned applied;   // ... and written
static unsigned ui_requests; // requests from outside the worker: a new state, retried afresh
static bool worker_running;
static bool line_out_by_source; // audio_mixer_set_line_out() was called (a source decided)
static uint64_t recheck_until_ms; // audio_mixer_recheck(): full passes, often, until then
static char last_batch[4096]; // what the codec should have (batch_mutex)
static bool have_batch;
static bool batch_failed; // amixer did not run or failed: the codec may not have last_batch
static unsigned codec_reads; // controls read by the watcher (host test: the cost of a pass)

static void start_worker(void);

const char *audio_mixer_key(audio_mixer_field_t field) {
    return field < AUDIO_MIXER_FIELD_COUNT ? field_key[field] : "";
}

static int clamp_field(audio_mixer_field_t field, long value) {
    if (value < 0)
        return 0;
    if (value > audio_mixer_max(field))
        return audio_mixer_max(field);
    return value;
}

void audio_mixer_init(void) {
    pthread_mutex_lock(&mixer_mutex);

    // The old single-source setting (0 = Mic, 1 = Line In, 2 = A/V In) stays in [record]:
    // a setting.ini without [mixer], or a field missing from it, is taken over from it.
    const long audio_source = ini_getl("record", "audio_source", 0, SETTING_INI);
    audio_mixer_settings_t defaults;
    audio_mixer_defaults_from_audio_source(&defaults, audio_source);

    const long version = ini_getl(AUDIO_MIXER_SECTION, "version", 0, SETTING_INI);
    if (version < AUDIO_MIXER_VERSION) {
        // No [mixer] yet (stock firmware, fresh install), or one of a test build: the stock
        // behaviour of [record] audio_source, whatever the section had.
        mixer = defaults;
        for (size_t i = 0; i < sizeof(old_keys) / sizeof(old_keys[0]); i++)
            ini_puts(AUDIO_MIXER_SECTION, old_keys[i], NULL, SETTING_INI);
        for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++)
            ini_putl(AUDIO_MIXER_SECTION, field_key[i], mixer.value[i], SETTING_INI);
        ini_putl(AUDIO_MIXER_SECTION, "version", AUDIO_MIXER_VERSION, SETTING_INI);
        LOGI("audio mixer: migrated from record/audio_source=%ld (mixer version %ld)", audio_source, version);
    } else {
        for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++) {
            const long value = ini_getl(AUDIO_MIXER_SECTION, field_key[i], defaults.value[i], SETTING_INI);
            mixer.value[i] = clamp_field(i, value);
        }
    }
    saved = mixer;
    // stock: the line out comes on with the first switch to an analog source (dvr_enable_line_out)
    context.line_out = mixer.value[AUDIO_MIXER_PHONES_AT_BOOT] != 0;

    pthread_mutex_unlock(&mixer_mutex);

    start_worker();
}

static bool is_headphone_level(audio_mixer_field_t field) {
    return field == AUDIO_MIXER_HEADPHONES || field == AUDIO_MIXER_MIC || field == AUDIO_MIXER_LINE_IN ||
           field == AUDIO_MIXER_AV || field == AUDIO_MIXER_HDMI;
}

int audio_mixer_get(audio_mixer_field_t field) {
    if (field >= AUDIO_MIXER_FIELD_COUNT)
        return 0;
    return mixer.value[field];
}

void audio_mixer_set(audio_mixer_field_t field, int value) {
    if (field >= AUDIO_MIXER_FIELD_COUNT)
        return;

    pthread_mutex_lock(&mixer_mutex);
    const int old = mixer.value[field];
    mixer.value[field] = clamp_field(field, value);
    if (is_headphone_level(field) && mixer.value[field] != old && !mixer.value[AUDIO_MIXER_PHONES_AT_BOOT]) {
        // the pilot sets the headphones up: what the page shows is heard, now and from every boot;
        // a line out that a source muted (HDMI in) stays muted
        mixer.value[AUDIO_MIXER_PHONES_AT_BOOT] = 1;
        if (!line_out_by_source)
            context.line_out = true;
    }
    pthread_mutex_unlock(&mixer_mutex);
}

void audio_mixer_save(void) {
    pthread_mutex_lock(&mixer_mutex);
    const audio_mixer_settings_t current = mixer;
    pthread_mutex_unlock(&mixer_mutex);

    for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++) {
        if (current.value[i] != saved.value[i]) {
            ini_putl(AUDIO_MIXER_SECTION, field_key[i], current.value[i], SETTING_INI);
            saved.value[i] = current.value[i];
        }
    }
}

bool audio_mixer_unsaved(void) {
    pthread_mutex_lock(&mixer_mutex);
    const bool unsaved = memcmp(&mixer, &saved, sizeof(mixer)) != 0;
    pthread_mutex_unlock(&mixer_mutex);
    return unsaved;
}

// Write one state to the codec: one amixer process for the whole batch.
static void write_batch(const audio_mixer_settings_t *settings, const audio_mixer_context_t *ctx) {
    static char cmd[4096];

    pthread_mutex_lock(&batch_mutex);

    const size_t len = audio_mixer_build_commands(settings, ctx, cmd, sizeof(cmd));
    const uint8_t *v = settings->value;
    LOGI("audio mixer: mic=%d line_in=%d av=%d hdmi=%d boost=%d/%d phones=%d playback=%d rec mix=%d direct=%d%d%d level=%d rx=%d line_out=%d playing=%d live_dac=%d",
         v[AUDIO_MIXER_MIC], v[AUDIO_MIXER_LINE_IN], v[AUDIO_MIXER_AV], v[AUDIO_MIXER_HDMI],
         v[AUDIO_MIXER_MIC_BOOST], v[AUDIO_MIXER_LINE_IN_BOOST],
         v[AUDIO_MIXER_HEADPHONES], v[AUDIO_MIXER_PLAYBACK],
         v[AUDIO_MIXER_REC_MIX], v[AUDIO_MIXER_REC_MIC], v[AUDIO_MIXER_REC_LINE_IN], v[AUDIO_MIXER_REC_AV],
         v[AUDIO_MIXER_DVR_LEVEL], v[AUDIO_MIXER_RX_AUDIO],
         ctx->line_out, ctx->playback, ctx->live_dac);

    // what the codec should have from now on, whatever happens below: the watcher compares the
    // codec with it, and applies again (with a backoff) when amixer did not get it there
    if (len < sizeof(cmd)) {
        memcpy(last_batch, cmd, len + 1);
        have_batch = true;
    }
    bool ok = false;
    FILE *fp = fopen(AUDIO_MIXER_CMD_FILE, "w");
    if (!fp || len >= sizeof(cmd)) {
        LOGE("audio mixer: cannot write %s", AUDIO_MIXER_CMD_FILE);
        if (fp)
            fclose(fp);
    } else {
        const bool written = fwrite(cmd, 1, len, fp) == len;
        if (fclose(fp) != 0 || !written) {
            LOGE("audio mixer: cannot write %s", AUDIO_MIXER_CMD_FILE);
        } else {
            const int status = system_exec(AUDIO_MIXER_AMIXER);
            ok = status == 0;
            if (!ok)
                LOGE("audio mixer: amixer failed (status %d): the codec may not have this routing", status);
        }
    }
    if (ok && batch_failed)
        LOGI("audio mixer: amixer works again");
    batch_failed = !ok;

    pthread_mutex_unlock(&batch_mutex);
}

// Apply the latest state; called with mixer_mutex held, returns with it held.
static void apply_latest(void) {
    const unsigned generation = requested;
    const audio_mixer_settings_t settings = mixer;
    const audio_mixer_context_t ctx = context;

    pthread_mutex_unlock(&mixer_mutex);
    write_batch(&settings, &ctx);
    pthread_mutex_lock(&mixer_mutex);

    if ((int)(generation - applied) > 0)
        applied = generation;
    pthread_cond_broadcast(&done_cond);
}

bool audio_mixer_batch_value(const char *batch, const char *name, int *value) {
    char line_name[64];
    int line_value;
    for (const char *line = batch; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        if (sscanf(line, "cset name='%63[^']' %d", line_name, &line_value) == 2 && strcmp(line_name, name) == 0) {
            *value = line_value;
            return true;
        }
    }
    return false;
}

static bool is_reset_control(const char *name) {
    for (size_t i = 0; i < sizeof(reset_controls) / sizeof(reset_controls[0]); i++)
        if (strcmp(reset_controls[i], name) == 0)
            return true;
    return false;
}

// Does the codec still have the values of the last batch (full: every control; otherwise the ones
// a libmedia_mpp reset writes)? -1: the codec cannot be read (emulator, no ALSA device).
static int codec_has_last_batch(bool full) {
    int result = 1;

    pthread_mutex_lock(&batch_mutex);
    for (const char *line = have_batch ? last_batch : ""; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        char name[64];
        int want, have;
        if (sscanf(line, "cset name='%63[^']' %d", name, &want) != 2)
            continue;
        if (!full && !is_reset_control(name))
            continue;
        codec_reads++;
        if (audio_mixer_codec_read(name, &have) != 0) {
            result = -1;
            break;
        }
        if (have != want) {
            LOGI("audio mixer: codec changed by someone else ('%s' = %d, want %d), applying again", name, have, want);
            result = 0;
            break;
        }
    }
    pthread_mutex_unlock(&batch_mutex);

    return result;
}

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// Wait on cond (mixer_mutex held) at most ms; ETIMEDOUT when the time is up.
static int wait_ms(pthread_cond_t *cond, unsigned ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += ms / 1000;
    deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(cond, &mixer_mutex, &deadline);
}

static unsigned next_backoff(unsigned ms) {
    if (ms < AUDIO_MIXER_BACKOFF_MS)
        return AUDIO_MIXER_BACKOFF_MS;
    return ms >= AUDIO_MIXER_BACKOFF_MAX_MS / 2 ? AUDIO_MIXER_BACKOFF_MAX_MS : 2 * ms;
}

static bool recheck_now; // audio_mixer_recheck(): a full pass at once (mixer_mutex)

static void *worker_thread(void *arg) {
    (void)arg;
    unsigned pass = 0;       // quick passes since the last full one
    unsigned backoff = 0;    // ms between passes while something fails; 0: AUDIO_MIXER_WATCH_MS
    int retries = 0;         // applies in a row by the watcher for the same state
    unsigned seen_ui = 0;    // ui_requests at the last apply
    bool unreadable = false; // the codec could not be read (logged once)

    pthread_mutex_lock(&mixer_mutex);
    for (;;) {
        while (applied == requested) {
            // Nothing to do: look at the codec now and then. The record daemon's AW_MPI_SYS_Init
            // (boot, SD check, card insertion, SD repair / format, DVR playback) resets part of the
            // mixer whenever it runs; whatever the timing, it is undone here.
            const bool rechecking = mono_ms() < recheck_until_ms;
            if (!recheck_now) {
                const unsigned period = backoff ? backoff : (rechecking ? AUDIO_MIXER_RECHECK_MS : AUDIO_MIXER_WATCH_MS);
                if (wait_ms(&work_cond, period) != ETIMEDOUT || applied != requested)
                    continue;
            }
            const bool full = recheck_now || rechecking || backoff || ++pass >= AUDIO_MIXER_WATCH_FULL;
            if (full)
                pass = 0;
            recheck_now = false;

            pthread_mutex_unlock(&mixer_mutex);
            const int has = codec_has_last_batch(full);
            pthread_mutex_lock(&mixer_mutex);
            pthread_mutex_lock(&batch_mutex);
            const bool failed = batch_failed;
            pthread_mutex_unlock(&batch_mutex);
            if (applied != requested)
                continue; // a new state came meanwhile

            if (has == 0 || (has < 0 && failed)) {
                // someone else changed the codec, or amixer did not get the batch there: again,
                // at once a few times, then with a backoff
                if (retries < AUDIO_MIXER_WATCH_RETRIES) {
                    retries++;
                    backoff = 0;
                } else {
                    if (retries == AUDIO_MIXER_WATCH_RETRIES)
                        LOGE("audio mixer: the codec does not keep the routing, applying it again with a backoff (up to %u s)",
                             AUDIO_MIXER_BACKOFF_MAX_MS / 1000);
                    retries = AUDIO_MIXER_WATCH_RETRIES + 1;
                    backoff = next_backoff(backoff);
                }
                requested++;
            } else if (has < 0) {
                if (!unreadable)
                    LOGE("audio mixer: cannot read the codec controls, trying again with a backoff (up to %u s)",
                         AUDIO_MIXER_BACKOFF_MAX_MS / 1000);
                unreadable = true;
                backoff = next_backoff(backoff);
            } else {
                if (unreadable || retries > AUDIO_MIXER_WATCH_RETRIES)
                    LOGI("audio mixer: the codec has the routing (watching every %u ms again)", AUDIO_MIXER_WATCH_MS);
                unreadable = false;
                retries = 0;
                backoff = 0;
            }
        }
        if (ui_requests != seen_ui) {
            // a new state: watched afresh
            seen_ui = ui_requests;
            retries = 0;
            backoff = 0;
        }
        apply_latest();
    }
    return NULL;
}

static void start_worker(void) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&work_cond, &attr);
    pthread_cond_init(&done_cond, &attr);
    pthread_condattr_destroy(&attr);

    pthread_t tid;
    if (pthread_create(&tid, NULL, worker_thread, NULL) == 0) {
        pthread_detach(tid);
        pthread_mutex_lock(&mixer_mutex);
        worker_running = true;
        pthread_mutex_unlock(&mixer_mutex);
    } else {
        LOGE("audio mixer: no worker thread, applying synchronously");
    }
}

// Ask for an apply; returns the generation that has the current state.
static unsigned request_apply(void) {
    pthread_mutex_lock(&mixer_mutex);
    const unsigned generation = ++requested;
    ui_requests++;
    if (worker_running)
        pthread_cond_signal(&work_cond);
    else
        apply_latest();
    pthread_mutex_unlock(&mixer_mutex);
    return generation;
}

void audio_mixer_apply(void) {
    request_apply();
}

bool audio_mixer_apply_wait(unsigned timeout_ms) {
    const unsigned generation = request_apply();

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&mixer_mutex);
    while ((int)(applied - generation) < 0) {
        if (pthread_cond_timedwait(&done_cond, &mixer_mutex, &deadline) == ETIMEDOUT)
            break;
    }
    const bool done = (int)(applied - generation) >= 0;
    pthread_mutex_unlock(&mixer_mutex);

    if (!done)
        LOGW("audio mixer: codec not updated within %u ms", timeout_ms);
    return done;
}

void audio_mixer_set_line_out(bool enable) {
    pthread_mutex_lock(&mixer_mutex);
    const bool changed = context.line_out != enable;
    context.line_out = enable;
    line_out_by_source = true;
    pthread_mutex_unlock(&mixer_mutex);

    if (changed)
        audio_mixer_apply();
}

void audio_mixer_recheck(void) {
    pthread_mutex_lock(&mixer_mutex);
    recheck_until_ms = mono_ms() + AUDIO_MIXER_RECHECK_WINDOW;
    recheck_now = true;
    pthread_cond_signal(&work_cond);
    pthread_mutex_unlock(&mixer_mutex);
}

unsigned audio_mixer_codec_reads(void) {
    pthread_mutex_lock(&batch_mutex);
    const unsigned n = codec_reads;
    pthread_mutex_unlock(&batch_mutex);
    return n;
}

bool audio_mixer_is_reset_control(const char *name) {
    return is_reset_control(name);
}

void audio_mixer_set_live_dac(bool on) {
    pthread_mutex_lock(&mixer_mutex);
    const bool changed = context.live_dac != on;
    context.live_dac = on;
    pthread_mutex_unlock(&mixer_mutex);

    if (changed)
        audio_mixer_apply();
}

void audio_mixer_set_playback(bool active) {
    pthread_mutex_lock(&mixer_mutex);
    context.playback = active;
    pthread_mutex_unlock(&mixer_mutex);

    // in place before the file starts, and the live mix back before its stream closes
    audio_mixer_apply_wait(1000);
}

bool audio_mixer_rx_audio(void) {
    return mixer.value[AUDIO_MIXER_RX_AUDIO] != 0;
}

bool audio_mixer_recorded_via_mix(audio_mixer_field_t rec_field) {
    pthread_mutex_lock(&mixer_mutex);
    const bool in_mix = audio_mixer_in_recorded_mix(&mixer, rec_field);
    pthread_mutex_unlock(&mixer_mutex);
    return in_mix;
}

static void db_str(int tenths, char *buf, size_t size) {
    const int abs_tenths = tenths < 0 ? -tenths : tenths;
    snprintf(buf, size, "%s%d.%d dB", tenths < 0 ? "-" : (tenths > 0 ? "+" : ""), abs_tenths / 10, abs_tenths % 10);
}

void audio_mixer_value_str(audio_mixer_field_t field, int value, char *buf, size_t size) {
    switch (field) {
    case AUDIO_MIXER_MIC:
    case AUDIO_MIXER_LINE_IN:
    case AUDIO_MIXER_AV:
    case AUDIO_MIXER_HDMI:
        // 0 = off, 1..8 = output mixer gain register 0..7: -4.5 dB .. +6 dB in 1.5 dB steps
        if (value <= 0)
            snprintf(buf, size, "%s", _lang("Off"));
        else
            db_str(-45 + 15 * (value - 1), buf, size);
        break;
    case AUDIO_MIXER_DVR_LEVEL:
        // ADC input gain register 0..7: -4.5 dB .. +6 dB
        db_str(-45 + 15 * value, buf, size);
        break;
    case AUDIO_MIXER_HEADPHONES:
    case AUDIO_MIXER_PLAYBACK:
        if (value <= 0)
            snprintf(buf, size, "%s", _lang("Off"));
        else
            snprintf(buf, size, "%d", value);
        break;
    default:
        // preamp boost: the driver's dB scale is not trustworthy, see docs/audio.md
        snprintf(buf, size, "%d", value);
        break;
    }
}
