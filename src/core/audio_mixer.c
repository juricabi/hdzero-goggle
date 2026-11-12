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
#define AUDIO_MIXER_VERSION 1

// The last batch stays there, handy to see what was sent to the codec.
#ifndef EMULATOR_BUILD
#define AUDIO_MIXER_CMD_FILE TMP_DIR "/audio_mixer.cmd"
#else
#define AUDIO_MIXER_CMD_FILE "audio_mixer.cmd"
#endif
#define AUDIO_MIXER_AMIXER "amixer -q -s < " AUDIO_MIXER_CMD_FILE

// How often the worker compares the codec with the last batch (audio_mixer_codec.c).
#ifndef AUDIO_MIXER_WATCH_MS
#define AUDIO_MIXER_WATCH_MS 2000
#endif
// Applies in a row for the same state when the codec does not keep it, before giving up.
#define AUDIO_MIXER_WATCH_RETRIES 3

static const char *const field_key[AUDIO_MIXER_FIELD_COUNT] = {
    [AUDIO_MIXER_MIC] = "mic",
    [AUDIO_MIXER_LINE_IN] = "line_in",
    [AUDIO_MIXER_AV] = "av",
    [AUDIO_MIXER_MIC_BOOST] = "mic_boost",
    [AUDIO_MIXER_LINE_IN_BOOST] = "line_in_boost",
    [AUDIO_MIXER_HEADPHONES] = "headphones",
    [AUDIO_MIXER_PLAYBACK] = "playback",
    [AUDIO_MIXER_DVR_MODE] = "dvr_mode",
    [AUDIO_MIXER_DVR_MIC] = "dvr_mic",
    [AUDIO_MIXER_DVR_LINE_IN] = "dvr_line_in",
    [AUDIO_MIXER_DVR_AV] = "dvr_av",
    [AUDIO_MIXER_DVR_LEVEL] = "dvr_level",
    [AUDIO_MIXER_RX_AUDIO] = "rx_audio",
    [AUDIO_MIXER_HDMI] = "hdmi",
};

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
static audio_mixer_context_t context = {.line_out = true, .playback = false, .live_dac = false};
static unsigned requested; // apply generations: requested by the UI ...
static unsigned applied;   // ... and in the codec
static bool worker_running;
static char last_batch[4096]; // what the codec should have (batch_mutex)
static bool have_batch;

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

    if (ini_getl(AUDIO_MIXER_SECTION, "version", 0, SETTING_INI) < AUDIO_MIXER_VERSION) {
        mixer = defaults;
        for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++)
            ini_putl(AUDIO_MIXER_SECTION, field_key[i], mixer.value[i], SETTING_INI);
        ini_putl(AUDIO_MIXER_SECTION, "version", AUDIO_MIXER_VERSION, SETTING_INI);
        LOGI("audio mixer: migrated from record/audio_source=%ld", audio_source);
    } else {
        for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++) {
            const long value = ini_getl(AUDIO_MIXER_SECTION, field_key[i], defaults.value[i], SETTING_INI);
            mixer.value[i] = clamp_field(i, value);
        }
    }
    saved = mixer;

    pthread_mutex_unlock(&mixer_mutex);

    start_worker();
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
    mixer.value[field] = clamp_field(field, value);
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
    LOGI("audio mixer: mic=%d line_in=%d av=%d boost=%d/%d phones=%d playback=%d dvr=%s(%d%d%d) dvr_level=%d rx=%d hdmi=%d line_out=%d playing=%d live_dac=%d",
         v[AUDIO_MIXER_MIC], v[AUDIO_MIXER_LINE_IN], v[AUDIO_MIXER_AV],
         v[AUDIO_MIXER_MIC_BOOST], v[AUDIO_MIXER_LINE_IN_BOOST],
         v[AUDIO_MIXER_HEADPHONES], v[AUDIO_MIXER_PLAYBACK],
         v[AUDIO_MIXER_DVR_MODE] == AUDIO_MIXER_DVR_AS_HEARD ? "as_heard" : "custom",
         v[AUDIO_MIXER_DVR_MIC], v[AUDIO_MIXER_DVR_LINE_IN], v[AUDIO_MIXER_DVR_AV],
         v[AUDIO_MIXER_DVR_LEVEL], v[AUDIO_MIXER_RX_AUDIO], v[AUDIO_MIXER_HDMI],
         ctx->line_out, ctx->playback, ctx->live_dac);

    FILE *fp = fopen(AUDIO_MIXER_CMD_FILE, "w");
    if (!fp || len >= sizeof(cmd)) {
        LOGE("audio mixer: cannot write %s", AUDIO_MIXER_CMD_FILE);
        if (fp)
            fclose(fp);
    } else {
        fwrite(cmd, 1, len, fp);
        fclose(fp);
        system_exec(AUDIO_MIXER_AMIXER);
        memcpy(last_batch, cmd, len + 1);
        have_batch = true;
    }

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

// Does the codec still have every value of the last batch? -1: cannot read the codec
// (emulator, no ALSA device), the worker then stops watching.
static int codec_has_last_batch(void) {
    int result = 1;

    pthread_mutex_lock(&batch_mutex);
    for (const char *line = have_batch ? last_batch : ""; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        char name[64];
        int want, have;
        if (sscanf(line, "cset name='%63[^']' %d", name, &want) != 2)
            continue;
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

static void *worker_thread(void *arg) {
    (void)arg;
    bool watching = true;
    int retries = 0;

    pthread_mutex_lock(&mixer_mutex);
    for (;;) {
        while (applied == requested) {
            if (!watching) {
                pthread_cond_wait(&work_cond, &mixer_mutex);
                continue;
            }

            // Nothing to do: look at the codec now and then. The record daemon's
            // AW_MPI_SYS_Init (boot, SD check/format, DVR playback) resets part of
            // the mixer whenever it runs; whatever the timing, it is undone here.
            struct timespec deadline;
            clock_gettime(CLOCK_MONOTONIC, &deadline);
            deadline.tv_sec += AUDIO_MIXER_WATCH_MS / 1000;
            deadline.tv_nsec += (long)(AUDIO_MIXER_WATCH_MS % 1000) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&work_cond, &mixer_mutex, &deadline) != ETIMEDOUT || applied != requested)
                continue;

            pthread_mutex_unlock(&mixer_mutex);
            const int has = codec_has_last_batch();
            pthread_mutex_lock(&mixer_mutex);

            if (has < 0) {
                watching = false;
            } else if (has) {
                retries = 0;
            } else if (applied == requested && retries < AUDIO_MIXER_WATCH_RETRIES) {
                retries++;
                requested++;
            } else if (applied == requested && retries == AUDIO_MIXER_WATCH_RETRIES) {
                retries++;
                LOGE("audio mixer: the codec does not keep the routing, not applying it again until it changes");
            }
        }
        if (retries > AUDIO_MIXER_WATCH_RETRIES)
            retries = 0; // a new state: watch it again
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
    pthread_mutex_unlock(&mixer_mutex);

    if (changed)
        audio_mixer_apply();
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
