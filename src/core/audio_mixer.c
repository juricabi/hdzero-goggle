#include "audio_mixer.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
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
};

static pthread_mutex_t mixer_mutex = PTHREAD_MUTEX_INITIALIZER;
static audio_mixer_settings_t mixer;
static audio_mixer_context_t context = {.line_out = true, .playback = false};

static int clamp_field(audio_mixer_field_t field, int value) {
    if (value < 0)
        return 0;
    if (value > audio_mixer_max(field))
        return audio_mixer_max(field);
    return value;
}

void audio_mixer_init(void) {
    pthread_mutex_lock(&mixer_mutex);

    if (ini_getl(AUDIO_MIXER_SECTION, "version", 0, SETTING_INI) < AUDIO_MIXER_VERSION) {
        // First start with the mixer: take over the old single-source setting.
        const int audio_source = ini_getl("record", "audio_source", 0, SETTING_INI);
        audio_mixer_defaults_from_audio_source(&mixer, audio_source);
        for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++)
            ini_putl(AUDIO_MIXER_SECTION, field_key[i], mixer.value[i], SETTING_INI);
        ini_putl(AUDIO_MIXER_SECTION, "version", AUDIO_MIXER_VERSION, SETTING_INI);
        LOGI("audio mixer: migrated from record/audio_source=%d", audio_source);
    } else {
        audio_mixer_settings_t defaults;
        audio_mixer_defaults_from_audio_source(&defaults, 0);
        for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++) {
            const int value = ini_getl(AUDIO_MIXER_SECTION, field_key[i], defaults.value[i], SETTING_INI);
            mixer.value[i] = clamp_field(i, value);
        }
    }

    pthread_mutex_unlock(&mixer_mutex);
}

int audio_mixer_get(audio_mixer_field_t field) {
    if (field >= AUDIO_MIXER_FIELD_COUNT)
        return 0;
    return mixer.value[field];
}

void audio_mixer_set(audio_mixer_field_t field, int value) {
    if (field >= AUDIO_MIXER_FIELD_COUNT)
        return;

    value = clamp_field(field, value);

    pthread_mutex_lock(&mixer_mutex);
    const bool changed = mixer.value[field] != value;
    mixer.value[field] = value;
    pthread_mutex_unlock(&mixer_mutex);

    if (changed)
        ini_putl(AUDIO_MIXER_SECTION, field_key[field], value, SETTING_INI);
}

void audio_mixer_apply(void) {
    static char cmd[4096];

    pthread_mutex_lock(&mixer_mutex);

    const size_t len = audio_mixer_build_commands(&mixer, &context, cmd, sizeof(cmd));
    const uint8_t *v = mixer.value;
    LOGI("audio mixer: mic=%d line_in=%d av=%d boost=%d/%d phones=%d playback=%d dvr=%s(%d%d%d) dvr_level=%d line_out=%d playing=%d",
         v[AUDIO_MIXER_MIC], v[AUDIO_MIXER_LINE_IN], v[AUDIO_MIXER_AV],
         v[AUDIO_MIXER_MIC_BOOST], v[AUDIO_MIXER_LINE_IN_BOOST],
         v[AUDIO_MIXER_HEADPHONES], v[AUDIO_MIXER_PLAYBACK],
         v[AUDIO_MIXER_DVR_MODE] == AUDIO_MIXER_DVR_AS_HEARD ? "as_heard" : "custom",
         v[AUDIO_MIXER_DVR_MIC], v[AUDIO_MIXER_DVR_LINE_IN], v[AUDIO_MIXER_DVR_AV],
         v[AUDIO_MIXER_DVR_LEVEL], context.line_out, context.playback);

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

    pthread_mutex_unlock(&mixer_mutex);
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

    audio_mixer_apply();
}

bool audio_mixer_av_in_use(void) {
    return mixer.value[AUDIO_MIXER_AV] > 0 ||
           (mixer.value[AUDIO_MIXER_DVR_MODE] == AUDIO_MIXER_DVR_CUSTOM && mixer.value[AUDIO_MIXER_DVR_AV]);
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
