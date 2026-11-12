#pragma once

/*
 * Audio mixer for the V536 internal codec (all targets).
 *
 * Every source has its own level in the analog output mixers, which feed the
 * headphone / line-out jack (left + right summed, mono): "what I hear". The ADC
 * (recording, WiFi stream audio and HDMI-out audio alike) takes that mix ("Record
 * What I Hear": left and right separately, before the sum and before the line-out
 * volume) and/or each analog source straight from its input at full level ("Record
 * Mic / Line In / A/V In", the path the old "Audio Source" setting used). A source
 * that is already in the recorded mix is not added a second time.
 * See docs/audio.md for the signal paths and the register ranges.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Fields of the [mixer] section of setting.ini
typedef enum {
    AUDIO_MIXER_MIC = 0,       // headphone level, 0 = off, 1..8 = -4.5..+6 dB
    AUDIO_MIXER_LINE_IN,       // headphone level
    AUDIO_MIXER_AV,            // headphone level
    AUDIO_MIXER_MIC_BOOST,     // MIC1 preamp 0..7
    AUDIO_MIXER_LINE_IN_BOOST, // MIC2 preamp 0..7
    AUDIO_MIXER_HEADPHONES,    // line-out volume 0..31, 0 = off
    AUDIO_MIXER_PLAYBACK,      // line-out volume during DVR playback 0..31, 0 = off
    AUDIO_MIXER_REC_MIX,       // Record What I Hear: the headphone mix into the ADC, 0/1
    AUDIO_MIXER_REC_MIC,       // Record Mic: the mic straight into the ADC (unless in the recorded mix), 0/1
    AUDIO_MIXER_REC_LINE_IN,   // Record Line In, 0/1
    AUDIO_MIXER_REC_AV,        // Record A/V In, 0/1
    AUDIO_MIXER_DVR_LEVEL,     // Record Level: ADC input gain 0..7 = -4.5..+6 dB
    AUDIO_MIXER_RX_AUDIO,      // audio output of the built-in analog receiver (RTC6715), 0/1;
                               // not a codec control (Goggle 2 and BoxPro only)
    AUDIO_MIXER_HDMI,          // headphone level of HDMI-in audio (the live digital source in the DAC)
    AUDIO_MIXER_PHONES_AT_BOOT, // 0: as stock, the headphones get the mix from the first switch to an analog
                                // source on; 1: from boot. Set by the first change of a headphone level.
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
    bool live_dac; // a live digital source plays into the DAC (HDMI-in audio), see audio_mixer_set_live_dac()
} audio_mixer_context_t;

// setting.ini [mixer] key of a field
const char *audio_mixer_key(audio_mixer_field_t field);

// Load the [mixer] settings and start the worker that writes them to the codec.
// A setting.ini without them is migrated from [record] audio_source, and so is every
// field missing from the section.
void audio_mixer_init(void);

int audio_mixer_get(audio_mixer_field_t field);
int audio_mixer_max(audio_mixer_field_t field);
// Clamp and store one value (memory only): audio_mixer_apply() sends it to the codec,
// audio_mixer_save() writes it to setting.ini. Changing a headphone level (Volume, Mic, Line In,
// A/V In, HDMI In) also makes the headphones live now and from every boot (AUDIO_MIXER_PHONES_AT_BOOT).
void audio_mixer_set(audio_mixer_field_t field, int value);
// Write the values changed since the last save to setting.ini. Call it from the UI, which
// writes the other settings (under lvgl_mutex), never from the worker.
void audio_mixer_save(void);
bool audio_mixer_unsaved(void);

// Write the whole routing to the codec. Returns at once: a worker runs one amixer process
// for the latest state, so a burst of calls ends in one or two batches.
void audio_mixer_apply(void);
// Same, but wait (at most timeout_ms) until the codec has the current state.
bool audio_mixer_apply_wait(unsigned timeout_ms);
// While idle, the worker compares the codec with the last batch every 2 s (a few ioctls,
// no process) and applies again when it differs: libmedia_mpp's AW_MPI_SYS_Init() resets
// part of the mixer whenever the record daemon starts (boot, SD check, SD format) and
// when a DVR file is played, at a time the app does not know.

// Line out allowed (an analog source) or muted (HDMI in), as the old dvr_enable_line_out(). At
// boot it is muted until the first call with true, like the stock firmware that left the record
// daemon's (silent) mixer in place until then, unless AUDIO_MIXER_PHONES_AT_BOOT is set.
void audio_mixer_set_line_out(bool enable);
// HDMI-in audio (src/core/hdmi_audio.c, only with [hdmi] audio_in=1): a live digital source plays
// into the DAC. Its level is AUDIO_MIXER_HDMI; unlike playback it is mixed with the other live
// sources, the line out is on even while HDMI in is the source (Volume permitting), and Record What
// I Hear records it (there is no direct path from the DAC into the ADC).
void audio_mixer_set_live_dac(bool on);
// DVR playback on / off. Waits until the codec has it: call it after media_init() (whose
// AW_MPI_SYS_Init resets the mixer) and before media_exit(), so the line out always has a source.
void audio_mixer_set_playback(bool active);

// Audio output of the built-in analog receiver: the audio_on of every rtc6715.init(1, ...)
// for the live picture (Goggle 2 and BoxPro; the Analog RSSI page keeps it off).
bool audio_mixer_rx_audio(void);

// Human readable value of a field ("Off", "-4.5 dB", "27", ...)
void audio_mixer_value_str(audio_mixer_field_t field, int value, char *buf, size_t size);

// The headphone level field of a Record source switch (AUDIO_MIXER_REC_MIC -> AUDIO_MIXER_MIC, ...),
// AUDIO_MIXER_FIELD_COUNT for other fields.
audio_mixer_field_t audio_mixer_level_of(audio_mixer_field_t rec_field);
// Is that source (a Record source switch field) recorded through the headphone mix (Record What I
// Hear on and its level above 0)? Then its direct path stays off: it would be recorded twice.
bool audio_mixer_in_recorded_mix(const audio_mixer_settings_t *settings, audio_mixer_field_t rec_field);
// The same for the current settings.
bool audio_mixer_recorded_via_mix(audio_mixer_field_t rec_field);

// The amixer batch (amixer -s syntax) for a state; returns the length like snprintf.
// Every control the mixer owns is set once. Order: a line out that goes off first, then
// gains and every path that is on, the line out, and last the paths that are off, so a
// change never leaves the output or the ADC without its sources in between (docs/audio.md).
size_t audio_mixer_build_commands(const audio_mixer_settings_t *settings, const audio_mixer_context_t *context, char *buf, size_t size);

// Defaults for a setting.ini that still has the old [record] audio_source (0 = Mic, 1 = Line In, 2 = A/V In).
void audio_mixer_defaults_from_audio_source(audio_mixer_settings_t *settings, long audio_source);

// Value a batch sets for a control; false if the batch does not set it.
bool audio_mixer_batch_value(const char *batch, const char *name, int *value);

// Read one control of the codec (audio_mixer_codec.c): 0 = ok, -1 = not available.
int audio_mixer_codec_read(const char *name, int *value);

#ifdef __cplusplus
}
#endif
