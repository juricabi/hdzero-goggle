#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void IT66121_close();
void IT66121_init();
void IT66121_set_phase(uint8_t phase, uint8_t inv);

/* ===== HDMI out audio (experimental, default off, see docs/hdmi-audio.md) ===== */
typedef enum {
    IT66121_AUDIO_I2S = 0,   // SCK/WS/I2S0 pins (2-ch L-PCM, 48 kHz, 16 bit in 32-bit slots)
    IT66121_AUDIO_SPDIF = 1, // I2S3/SPDIF pin
} it66121_audio_input_t;

void IT66121_audio_config(bool enable, it66121_audio_input_t input); // applied by every IT66121_init()
bool IT66121_audio_configured(void);
bool IT66121_is_active(void);                       // between IT66121_init() and IT66121_close()
bool IT66121_audio_is_on(void);
int IT66121_audio(bool on);                         // program now; needs an initialised chip
void IT66121_audio_mute(bool mute);                 // flat samples, link stays up
uint32_t IT66121_audio_cts(void);                   // measured CTS, 0 = no audio clock on SCK/WS
void IT66121_audio_status(char *buf, size_t len);   // one-line register summary for logs
/* ===== end HDMI out audio ===== */

#ifdef __cplusplus
}
#endif
