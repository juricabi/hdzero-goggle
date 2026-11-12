#pragma once
/* ===========================================================================
 * HDMI audio routing (EXPERIMENTAL, default off). See docs/hdmi-audio.md.
 *
 * setting.ini (own section, read once at start-up):
 *   [hdmi]
 *   audio_out=0  1 = IT66121 sends 48 kHz stereo L-PCM taken from its I2S input,
 *                    and the record daemon copies the DVR audio input (codec ADC,
 *                    the same signal that is recorded) to I2S0 (ALSA hw:snddaudio0).
 *   audio_in=0   1 = while the source is HDMI in, the record daemon copies I2S2
 *                    (ALSA hw:snddaudio2, assumed IT66021 audio) to the codec DAC
 *                    and line out is switched to the DAC (monitor on the jack).
 * With both keys 0 (default) nothing here touches any hardware.
 * ======================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void hdmi_audio_load(void);           // read [hdmi] from setting.ini, configure the IT66121 driver
bool hdmi_audio_out_enabled(void);
bool hdmi_audio_in_enabled(void);
void hdmi_audio_set(bool out, bool in); // persist + apply (hook for a future UI switch)
void hdmi_audio_apply(void);          // re-evaluate routing after a source / HDMI out change

#ifdef __cplusplus
}
#endif
