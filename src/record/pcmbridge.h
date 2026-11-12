#pragma once
/* ===========================================================================
 * PCM bridge for HDMI audio (EXPERIMENTAL, default off, docs/hdmi-audio.md).
 * Copies one audio source to up to two ALSA playback devices, one route at a time:
 *   src = "ai"          : an extra channel on the MPP AI device (codec ADC, i.e.
 *                         exactly what the DVR records); coexists with ai2aenc
 *                         and ai2ao, which share the same AI device (HDMI out route).
 *   src = <ALSA name>   : an ALSA capture device, e.g. hw:snddaudio2 (I2S2, HDMI in),
 *                         played into the codec DAC; the DVR capture keeps running.
 * Started / stopped by HDMIA_cmdSTART / HDMIA_cmdSTOP (appmsg.h). The route comes from
 * HDMIA_REQ_FILE (written by src/core/hdmi_audio.c); the result goes to
 * HDMIA_STATE_FILE. record.conf [hdmi_audio] latency_ms=60 stays a user setting.
 * Every SoC register it changes (PH1-PH3 function, I2S0 CTL fields) is put back on stop.
 * ======================================================================== */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ai2aenc.h"

typedef struct PcmBridge PcmBridge_t;

PcmBridge_t *pcmbridge_initSys(void);
void pcmbridge_deinitSys(PcmBridge_t *pb, bool disableAiDev);
// aiDevFree: nobody else (DVR, ai2ao) holds the AI device: a failed start or a replaced
// route also disables it
int pcmbridge_start(PcmBridge_t *pb, const char *confFile, AiParams_t *aiParams, bool aiDevFree);
void pcmbridge_stop(PcmBridge_t *pb, bool disableAiDev);
bool pcmbridge_usesAi(PcmBridge_t *pb); // true while it holds an MPP AI channel

#ifdef __cplusplus
}
#endif
