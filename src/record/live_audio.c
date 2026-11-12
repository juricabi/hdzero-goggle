// #define LOG_NDEBUG 0
#define LOG_TAG "live_audio"
#include <log/log.h>

#include <string.h>
#include <time.h>

#include "avshare.h"
#include "confparser.h"
#include "live_audio.h"

#define LIVE_AUDIO_STOP_DELAY_MS  3000 // keep the chain running across DESCRIBE -> SETUP
#define LIVE_AUDIO_RETRY_MS       5000 // after a failed start
#define LIVE_CLOCK_MAX_LATENCY_US 1000000

//----------------------------------------------------------------------------------------------
// timestamps
//----------------------------------------------------------------------------------------------
uint64_t liveclock_nowUs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static uint32_t liveclock_tickMs(void) {
    return (uint32_t)(liveclock_nowUs() / 1000);
}

void liveclock_reset(LiveClock_t *clock) {
    clock->valid = false;
    clock->offsetUs = 0;
}

uint64_t liveclock_stamp(LiveClock_t *clock, uint64_t ptsUs) {
    int64_t now = (int64_t)liveclock_nowUs();
    int64_t stamp = 0;

    if (clock->valid) {
        stamp = (int64_t)ptsUs + clock->offsetUs;
        // a frame cannot come from the future nor be very old: the encoder clock jumped
        if ((stamp > now + 200000) || (stamp < now - 2 * LIVE_CLOCK_MAX_LATENCY_US)) {
            LOGD("re-anchor live clock (pts=%llu, now=%lld)", ptsUs, now);
            clock->valid = false;
        }
    }

    if (!clock->valid) {
        int64_t delta = now - (int64_t)ptsUs;
        // MPP stamps captured frames with CLOCK_MONOTONIC: then the pts is the capture time,
        // slightly in the past, and is used as it is. Any other clock is anchored to the
        // arrival time of this frame and then follows the encoder pts.
        clock->offsetUs = ((delta >= 0) && (delta < LIVE_CLOCK_MAX_LATENCY_US)) ? 0 : delta;
        clock->valid = true;
        stamp = (int64_t)ptsUs + clock->offsetUs;
    }

    return (uint64_t)stamp;
}

//----------------------------------------------------------------------------------------------
// audio chain
//----------------------------------------------------------------------------------------------
static int liveaudio_onFrame(void *aaPtr, uint8_t *frameData, int frameLen, uint64_t pts, void *context) {
    LiveAudio_t *la = (LiveAudio_t *)context;
    uint64_t ts = liveclock_stamp(&la->clock, pts);

    if (la->nbFrames == 0) {
        LOGD("first frame (len=%d, pts=%llu, ts=%llu)", frameLen, pts, ts);
    }
    la->nbFrames++;

    int ret = avshare_addAudioFrame(frameData, frameLen, ts);
    if (ret == 1) {
        // ring full: the reader is not keeping up (or paused), drop this frame
        if ((la->nbDropped++ % 100) == 0) {
            LOGD("audio ring full, dropped %u frames", la->nbDropped);
        }
    } else if ((ret != 0) && (ret != -3)) {
        LOGE("queue audio frame failed: %d (len=%d)", ret, frameLen);
    }

    return 0;
}

bool liveaudio_init(LiveAudio_t *la) {
    memset(la, 0, sizeof(LiveAudio_t));
    la->aa = ai2aenc_initSys(liveaudio_onFrame, la);
    return (la->aa != NULL);
}

void liveaudio_deinit(LiveAudio_t *la) {
    if (la->aa != NULL) {
        ai2aenc_deinitSys(la->aa);
        la->aa = NULL;
    }
}

bool liveaudio_running(LiveAudio_t *la) {
    return la->running;
}

static bool liveaudio_start(LiveAudio_t *la, char *confFile, bool devInUse) {
    AiParams_t aiParams;
    AencParams_t aeParams;
    ERRORTYPE ret;

    memset(&aiParams, 0, sizeof(aiParams));
    memset(&aeParams, 0, sizeof(aeParams));
    conf_loadAiParams(confFile, &aiParams);
    conf_loadAencParams(confFile, &aeParams);
    aeParams.codecType = PT_AAC; // the RTSP track is AAC (MPEG4-GENERIC), whatever the recording uses

    liveclock_reset(&la->clock);
    la->nbFrames = 0;
    la->nbDropped = 0;

    ret = ai2aenc_prepare(la->aa, &aiParams, &aeParams);
    if (ret == SUCCESS) {
        ret = ai2aenc_start(la->aa);
    }
    if (ret != SUCCESS) {
        LOGE("start failed: %x", ret);
        ai2aenc_stop(la->aa, !devInUse);
        return false;
    }

    LOGD("started: dev=%d, %d Hz, %d ch, %d bits, %d bps",
         aiParams.aiDev, aiParams.sampleRate, aiParams.channels, aiParams.bitsPerSample, aeParams.bitRate);
    la->running = true;
    return true;
}

void liveaudio_stop(LiveAudio_t *la, bool devInUse) {
    if (!la->running || (la->aa == NULL)) {
        return;
    }

    ai2aenc_stop(la->aa, !devInUse);
    la->running = false;
    LOGD("stopped (%u frames, %u dropped)", la->nbFrames, la->nbDropped);
}

void liveaudio_run(LiveAudio_t *la, char *confFile, bool devInUse) {
    if (la->aa == NULL) {
        return;
    }

    uint32_t tickNow = liveclock_tickMs();
    bool connected = avshare_connected(MEDIA_AUDIO);

    if (connected) {
        if (!la->connected) {
            la->connected = true;
            // rtspLive only connects the audio track when the setting is on; check it anyway
            la->enabled = conf_loadLiveAudio(confFile);
            if (!la->enabled) {
                LOGD("audio reader connected, but [live] audio is off");
            }
            la->tickRetry = tickNow;
        }

        if (la->enabled && !la->running && ((int32_t)(tickNow - la->tickRetry) >= 0)) {
            if (!liveaudio_start(la, confFile, devInUse)) {
                la->tickRetry = tickNow + LIVE_AUDIO_RETRY_MS;
            }
        }
    } else {
        if (la->connected) {
            la->connected = false;
            la->tickDisconnected = tickNow;
        }

        if (la->running && ((tickNow - la->tickDisconnected) >= LIVE_AUDIO_STOP_DELAY_MS)) {
            liveaudio_stop(la, devInUse);
        }
    }
}
