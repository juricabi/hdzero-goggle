#pragma once

/*
 * Live stream audio (RTSP AAC track).
 *
 * When rtspLive sets up the audio track of the live stream it connects the audio side of the
 * shared ring (avshare). The record process then runs a dedicated AI -> AENC (AAC, ADTS) chain,
 * independent of recording and of the "Record Audio" setting, and queues every encoded frame in
 * the ring. Both live video and live audio frames are stamped with CLOCK_MONOTONIC microseconds
 * (see liveclock_stamp()), the clock rtspLive turns into wall-clock presentation times.
 *
 * Nothing here runs unless rtspLive connects the audio stream, which it only does when
 * [live] audio=1 in the record conf (WiFi Module > System > Stream Audio).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ai2aenc.h"

typedef struct {
    bool valid;
    int64_t offsetUs; /* encoder pts + offset = CLOCK_MONOTONIC us */
} LiveClock_t;

typedef struct {
    Ai2Aenc_t *aa; /* dedicated AI + AENC chain for the live stream */
    bool running;
    bool enabled;              /* [live] audio, read when the reader connects */
    bool connected;            /* audio reader (rtspLive) connected, last poll */
    uint32_t tickDisconnected; /* when the reader went away */
    uint32_t tickRetry;        /* no start attempt before this tick after a failure */
    uint32_t nbFrames;
    uint32_t nbDropped;
    LiveClock_t clock;
} LiveAudio_t;

uint64_t liveclock_nowUs(void);
void liveclock_reset(LiveClock_t *clock);
/* Map an encoder pts (us) to CLOCK_MONOTONIC us, continuous within one stream. */
uint64_t liveclock_stamp(LiveClock_t *clock, uint64_t ptsUs);

bool liveaudio_init(LiveAudio_t *la);
void liveaudio_deinit(LiveAudio_t *la);
/* Poll from the record main loop: starts/stops the chain following the audio reader.
 * devInUse: another user of the AI device is active (recording audio, AI->AO loop). */
void liveaudio_run(LiveAudio_t *la, char *confFile, bool devInUse);
void liveaudio_stop(LiveAudio_t *la, bool devInUse);
bool liveaudio_running(LiveAudio_t *la);

#ifdef __cplusplus
}
#endif
