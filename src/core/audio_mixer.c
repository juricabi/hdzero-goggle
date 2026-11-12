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
static audio_mixer_context_t context = {.line_out = true, .playback = false};
static unsigned requested; // apply generations: requested by the UI ...
static unsigned applied;   // ... and in the codec
static bool worker_running;

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
    LOGI("audio mixer: mic=%d line_in=%d av=%d boost=%d/%d phones=%d playback=%d dvr=%s(%d%d%d) dvr_level=%d rx=%d line_out=%d playing=%d",
         v[AUDIO_MIXER_MIC], v[AUDIO_MIXER_LINE_IN], v[AUDIO_MIXER_AV],
         v[AUDIO_MIXER_MIC_BOOST], v[AUDIO_MIXER_LINE_IN_BOOST],
         v[AUDIO_MIXER_HEADPHONES], v[AUDIO_MIXER_PLAYBACK],
         v[AUDIO_MIXER_DVR_MODE] == AUDIO_MIXER_DVR_AS_HEARD ? "as_heard" : "custom",
         v[AUDIO_MIXER_DVR_MIC], v[AUDIO_MIXER_DVR_LINE_IN], v[AUDIO_MIXER_DVR_AV],
         v[AUDIO_MIXER_DVR_LEVEL], v[AUDIO_MIXER_RX_AUDIO], ctx->line_out, ctx->playback);

    FILE *fp = fopen(AUDIO_MIXER_CMD_FILE, "w");
    if (!fp || len >= sizeof(cmd)) {
        LOGE("audio mixer: cannot write %s", AUDIO_MIXER_CMD_FILE);
        if (fp)
            fclose(fp);
    } else {
        fwrite(cmd, 1, len, fp);
        fclose(fp);
        system_exec(AUDIO_MIXER_AMIXER);
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

static void *worker_thread(void *arg) {
    (void)arg;

    pthread_mutex_lock(&mixer_mutex);
    for (;;) {
        while (applied == requested)
            pthread_cond_wait(&work_cond, &mixer_mutex);
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

static void *apply_later_thread(void *arg) {
    usleep((useconds_t)(size_t)arg * 1000);
    audio_mixer_apply();
    return NULL;
}

void audio_mixer_apply_later(unsigned ms) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, apply_later_thread, (void *)(size_t)ms) == 0)
        pthread_detach(tid);
    else
        audio_mixer_apply();
}

void audio_mixer_record_daemon_restarted(void) {
    // the scripts mount the card, sleep a second and start the daemon in the background
    audio_mixer_apply_later(4000);
    audio_mixer_apply_later(12000);
}

void audio_mixer_set_line_out(bool enable) {
    pthread_mutex_lock(&mixer_mutex);
    const bool changed = context.line_out != enable;
    context.line_out = enable;
    pthread_mutex_unlock(&mixer_mutex);

    if (changed)
        audio_mixer_apply();
}

void audio_mixer_set_playback(bool active) {
    pthread_mutex_lock(&mixer_mutex);
    context.playback = active;
    pthread_mutex_unlock(&mixer_mutex);

    // a file that starts playing should start with its routing in place
    if (active)
        audio_mixer_apply_wait(1000);
    else
        audio_mixer_apply();
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
