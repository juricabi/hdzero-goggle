#pragma once
/* ===========================================================================
 * HDMI audio routing (EXPERIMENTAL, default off). See docs/hdmi-audio.md.
 *
 * setting.ini (own section, read once at start-up):
 *   [hdmi]
 *   audio_out=0  1 = IT66121 sends 48 kHz stereo L-PCM taken from its I2S input,
 *                    and the record daemon copies the DVR audio input (codec ADC,
 *                    the same signal that is recorded) to I2S0 (ALSA hw:snddaudio0).
 *   audio_in=0   1 = while HDMI in is shown, the record daemon plays I2S2 (ALSA
 *                    hw:snddaudio2, assumed IT66021 audio) into the codec DAC: heard on
 *                    the line-out jack and recorded by the DVR with its own source; the
 *                    EDID offers 48 kHz L-PCM only.
 * With both keys 0 (default) nothing here touches any hardware or file.
 * ======================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void hdmi_audio_load(void);           // read [hdmi] from setting.ini, configure the IT66121 driver
bool hdmi_audio_out_enabled(void);
bool hdmi_audio_in_enabled(void);
void hdmi_audio_set(bool out, bool in); // persist + apply (hook for a future UI switch)
// re-evaluate the route after a source / HDMI out change; hdmi_in_shown = the source being
// switched TO is HDMI in (g_source_info.source is set only after app_switch_to_*() returns)
void hdmi_audio_apply(bool hdmi_in_shown);
void hdmi_audio_poll(void); // every ~0.5 s: re-send what the record daemon did not confirm

#ifdef __cplusplus
}
#endif
