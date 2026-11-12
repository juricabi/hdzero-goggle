#pragma once
/* ===========================================================================
 * PCM bridge for HDMI audio (EXPERIMENTAL, default off, docs/hdmi-audio.md).
 * Copies one audio source to up to two ALSA playback devices:
 *   src = "ai"          : an extra channel on the MPP AI device (codec ADC, i.e.
 *                         exactly what the DVR records); coexists with ai2aenc
 *                         and ai2ao, which share the same AI device.
 *   src = <ALSA name>   : an ALSA capture device, e.g. hw:snddaudio2 (I2S2).
 * Started / stopped by HDMIA_cmdSTART / HDMIA_cmdSTOP (appmsg.h); parameters
 * come from record.conf [hdmi_audio] (written by src/core/hdmi_audio.c):
 *   src=ai  sink=hw:snddaudio0  sink2=  i2s0_fix=1  latency_ms=60
 * ======================================================================== */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ai2aenc.h"

typedef struct PcmBridge PcmBridge_t;

PcmBridge_t *pcmbridge_initSys(void);
void pcmbridge_deinitSys(PcmBridge_t *pb, bool disableAiDev);
int pcmbridge_start(PcmBridge_t *pb, const char *confFile, AiParams_t *aiParams);
void pcmbridge_stop(PcmBridge_t *pb, bool disableAiDev);
bool pcmbridge_usesAi(PcmBridge_t *pb); // true while it holds an MPP AI channel

#ifdef __cplusplus
}
#endif
