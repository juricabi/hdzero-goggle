#pragma once

/*
 * Audio mixer for the V536 internal codec (all targets).
 *
 * Every source has its own level in the analog output mixer, which feeds the
 * headphone / line-out jack. The DVR either records that same mix ("as heard",
 * the ADC is fed from the output mixer) or a custom set of sources that go
 * straight into the ADC (the path the old "Audio Source" setting used).
 * See docs/audio.md for the signal paths and the register ranges.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_MIXER_DVR_AS_HEARD = 0, // DVR records the headphone mix
    AUDIO_MIXER_DVR_CUSTOM = 1,   // DVR records the selected sources directly
} audio_mixer_dvr_mode_t;

// Fields of the [mixer] section of setting.ini
typedef enum {
    AUDIO_MIXER_MIC = 0,       // mix level, 0 = off, 1..8 = -4.5..+6 dB
    AUDIO_MIXER_LINE_IN,       // mix level
    AUDIO_MIXER_AV,            // mix level
    AUDIO_MIXER_MIC_BOOST,     // MIC1 preamp 0..7
    AUDIO_MIXER_LINE_IN_BOOST, // MIC2 preamp 0..7
    AUDIO_MIXER_HEADPHONES,    // line-out volume 0..31, 0 = off
    AUDIO_MIXER_PLAYBACK,      // line-out volume during DVR playback 0..31, 0 = off
    AUDIO_MIXER_DVR_MODE,      // audio_mixer_dvr_mode_t
    AUDIO_MIXER_DVR_MIC,       // custom DVR mode: record the mic, 0/1
    AUDIO_MIXER_DVR_LINE_IN,   // custom DVR mode: record line in, 0/1
    AUDIO_MIXER_DVR_AV,        // custom DVR mode: record A/V, 0/1
    AUDIO_MIXER_DVR_LEVEL,     // ADC input gain 0..7 = -4.5..+6 dB
    AUDIO_MIXER_RX_AUDIO,      // audio output of the built-in analog receiver (RTC6715), 0/1;
                               // not a codec control (Goggle 2 and BoxPro only)
    AUDIO_MIXER_FIELD_COUNT
} audio_mixer_field_t;

#define AUDIO_MIXER_LEVEL_MAX     8
#define AUDIO_MIXER_BOOST_MAX     7
#define AUDIO_MIXER_VOLUME_MAX    31
#define AUDIO_MIXER_DVR_LEVEL_MAX 7

typedef struct {
    uint8_t value[AUDIO_MIXER_FIELD_COUNT];
} audio_mixer_settings_t;

typedef struct {
    bool line_out; // false: line out muted (HDMI in)
    bool playback; // DVR playback is running
} audio_mixer_context_t;

// Load the [mixer] settings; a setting.ini without them is migrated from [record] audio_source,
// and so is every field missing from the section.
void audio_mixer_init(void);

int audio_mixer_get(audio_mixer_field_t field);
int audio_mixer_max(audio_mixer_field_t field);
// Clamp, store and save one value; call audio_mixer_apply() afterwards.
void audio_mixer_set(audio_mixer_field_t field, int value);

// Write the whole routing to the codec (one amixer process).
void audio_mixer_apply(void);
// Apply again after `ms` milliseconds (from a separate thread).
void audio_mixer_apply_later(unsigned ms);
// The record daemon was (re)started: its AW_MPI_SYS_Init resets part of the codec mixer
// (libmedia_mpp alsaOpenMixer), so apply again once it is up.
void audio_mixer_record_daemon_restarted(void);

void audio_mixer_set_line_out(bool enable);
void audio_mixer_set_playback(bool active);

// Audio output of the built-in analog receiver: the audio_on of every rtc6715.init(1, ...)
// for the live picture (Goggle 2 and BoxPro; the Analog RSSI page keeps it off).
bool audio_mixer_rx_audio(void);

// Human readable value of a field ("Off", "-4.5 dB", "27", ...)
void audio_mixer_value_str(audio_mixer_field_t field, int value, char *buf, size_t size);

// The amixer batch (amixer -s syntax) for a state; returns the length like snprintf.
// Every control the mixer owns is set once. Order: a line out that goes off first, then
// gains and every path that is on, the line out, and last the paths that are off, so a
// change never leaves the output or the ADC without its sources in between (docs/audio.md).
size_t audio_mixer_build_commands(const audio_mixer_settings_t *settings, const audio_mixer_context_t *context, char *buf, size_t size);

// Defaults for a setting.ini that still has the old [record] audio_source (0 = Mic, 1 = Line In, 2 = A/V In).
void audio_mixer_defaults_from_audio_source(audio_mixer_settings_t *settings, long audio_source);

#ifdef __cplusplus
}
#endif
