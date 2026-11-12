#pragma once
/* ===========================================================================
 * HDMI audio routing (EXPERIMENTAL, default off). See docs/hdmi-audio.md.
 *
 * setting.ini (own section, read at start-up; the Audio page's HDMI Out Audio /
 * HDMI In Audio switches change it through hdmi_audio_set()):
 *   [hdmi]
 *   audio_out=0  1 = IT66121 sends 48 kHz stereo L-PCM taken from its I2S input,
 *                    and the record daemon copies the ADC capture (the same signal
 *                    that is recorded) to I2S0 (ALSA hw:snddaudio0).
 *   audio_in=0   1 = while HDMI in is shown, the record daemon plays I2S2 (ALSA
 *                    hw:snddaudio2, assumed IT66021 audio) into the codec DAC, which
 *                    the audio mixer takes in as a live source (the HDMI In level of
 *                    the Audio page; recorded with Record What I Hear); the line out
 *                    then stays on during HDMI in. The EDID offers 48 kHz L-PCM only,
 *                    from the next start (it is written when the IT66021 is set up).
 * With both keys 0 (default) nothing here touches any hardware or file, and the line
 * out is muted during HDMI in as in the stock firmware.
 * ======================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void hdmi_audio_load(void);           // read [hdmi] from setting.ini, configure the IT66121 driver
bool hdmi_audio_out_enabled(void);
bool hdmi_audio_in_enabled(void);
// persist + apply at once (Audio page): HDMI out audio on the running IT66121, the route; the
// 48 kHz-only EDID of audio_in from the next start
void hdmi_audio_set(bool out, bool in);
// re-evaluate the route after a source / HDMI out change; hdmi_in_shown = the source being
// switched TO is HDMI in (g_source_info.source is set only after app_switch_to_*() returns)
void hdmi_audio_apply(bool hdmi_in_shown);
void hdmi_audio_poll(void); // every ~0.5 s: re-send what the record daemon did not confirm

#ifdef __cplusplus
}
#endif
