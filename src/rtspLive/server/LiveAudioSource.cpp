#include "LiveAudioSource.hh"

#include <stdio.h>
#include <string.h>
#include <time.h>

extern "C"
{
#include "avshare.h"
}

#define AUDIO_BUFFER_SIZE   (8 * 1024)  // > largest ADTS frame (13-bit length)
#define AUDIO_POLL_US       2000        // AAC frame: 1024 samples = 21.3 ms at 48 kHz
#define AUDIO_IDLE_POLL_US  20000       // ring not attached / reader not connected
#define AUDIO_MAX_AGE_US    1000000     // older frames are a backlog (paused reader): skip them
#define AUDIO_MAX_SKIP      64          // frames examined per poll

static unsigned const samplingFrequencyTable[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

unsigned aacSamplingFrequency(unsigned sfIndex) {
    return (sfIndex < 16) ? samplingFrequencyTable[sfIndex] : 0;
}

unsigned aacSamplingFrequencyIndex(unsigned frequency) {
    for (unsigned i = 0; i < 13; ++i) {
        if (samplingFrequencyTable[i] == frequency) return i;
    }
    return 15;
}

void aacConfigString(unsigned audioObjectType, unsigned sfIndex, unsigned channelConfig, char* out) {
    // AudioSpecificConfig: audioObjectType(5) samplingFrequencyIndex(4) channelConfiguration(4)
    // frameLengthFlag(1)=0 dependsOnCoreCoder(1)=0 extensionFlag(1)=0
    unsigned config = ((audioObjectType & 0x1F) << 11) | ((sfIndex & 0x0F) << 7) | ((channelConfig & 0x0F) << 3);
    snprintf(out, 5, "%04X", config & 0xFFFF);
}

Boolean aacParseAdtsHeader(unsigned char const* p, unsigned len, AdtsHeader& hdr) {
    if (len < 7) return False;
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return False; // syncword, layer 0

    hdr.profile = (p[2] >> 6) & 0x03;
    hdr.sfIndex = (p[2] >> 2) & 0x0F;
    hdr.channelConfig = ((p[2] & 0x01) << 2) | ((p[3] >> 6) & 0x03);
    hdr.headerLength = (p[1] & 0x01) ? 7 : 9;
    hdr.frameLength = ((p[3] & 0x03) << 11) | (p[4] << 3) | ((p[5] >> 5) & 0x07);
    hdr.numRawBlocks = (p[6] & 0x03) + 1;

    if (aacSamplingFrequency(hdr.sfIndex) == 0) return False;
    if (hdr.frameLength < hdr.headerLength) return False;
    return True;
}

LiveAudioSource* LiveAudioSource::createNew(UsageEnvironment& env) {
    return new LiveAudioSource(env);
}

LiveAudioSource::LiveAudioSource(UsageEnvironment& env)
    : FramedSource(env), fBuffer(new unsigned char[AUDIO_BUFFER_SIZE]),
      fHaveExpectedFormat(False), fNbFrames(0), fNbDropped(0) {
    memset(&fExpected, 0, sizeof(fExpected));
    printf("[MEDIA SERVER]live audio source opened\n");
    avshare_startMedia(MEDIA_AUDIO);
}

LiveAudioSource::~LiveAudioSource() {
    envir().taskScheduler().unscheduleDelayedTask(nextTask());
    avshare_stopMedia(MEDIA_AUDIO);
    delete[] fBuffer;
    printf("[MEDIA SERVER]live audio source closed (%u frames, %u dropped)\n", fNbFrames, fNbDropped);
}

void LiveAudioSource::setExpectedFormat(unsigned profile, unsigned sfIndex, unsigned channelConfig) {
    fExpected.profile = profile;
    fExpected.sfIndex = sfIndex;
    fExpected.channelConfig = channelConfig;
    fHaveExpectedFormat = True;
}

void LiveAudioSource::doGetNextFrame() {
    deliverFrame();
}

void LiveAudioSource::pollAgain(void* clientData) {
    ((LiveAudioSource*)clientData)->deliverFrame();
}

void LiveAudioSource::deliverFrame() {
    nextTask() = NULL;
    if (!isCurrentlyAwaitingData()) return;

    for (unsigned n = 0; n < AUDIO_MAX_SKIP; ++n) {
        uint64_t ts = 0;
        int len = avshare_readMediaFrame(MEDIA_AUDIO, fBuffer, AUDIO_BUFFER_SIZE, &ts);
        if (len == 0) {
            nextTask() = envir().taskScheduler().scheduleDelayedTask(AUDIO_POLL_US, pollAgain, this);
            return;
        }
        if (len == -1) {
            nextTask() = envir().taskScheduler().scheduleDelayedTask(AUDIO_IDLE_POLL_US, pollAgain, this);
            return;
        }
        if (len < 0) {
            ++fNbDropped;
            continue;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t nowUs = (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
        if (nowUs > ts + AUDIO_MAX_AGE_US) {
            ++fNbDropped;
            continue;
        }

        AdtsHeader hdr;
        if (!aacParseAdtsHeader(fBuffer, (unsigned)len, hdr) || hdr.frameLength > (unsigned)len || hdr.numRawBlocks != 1) {
            if ((fNbDropped++ % 100) == 0) {
                printf("[MEDIA SERVER]live audio: unexpected frame (len=%d, %02x %02x %02x)\n",
                       len, fBuffer[0], fBuffer[1], fBuffer[2]);
            }
            continue;
        }
        if (fHaveExpectedFormat &&
            (hdr.profile != fExpected.profile || hdr.sfIndex != fExpected.sfIndex || hdr.channelConfig != fExpected.channelConfig)) {
            if ((fNbDropped++ % 100) == 0) {
                printf("[MEDIA SERVER]live audio: format changed (profile %u, rate %u, channels %u), frame dropped\n",
                       hdr.profile, aacSamplingFrequency(hdr.sfIndex), hdr.channelConfig);
            }
            continue;
        }

        unsigned payloadLen = hdr.frameLength - hdr.headerLength;
        if (payloadLen > fMaxSize) {
            fNumTruncatedBytes = payloadLen - fMaxSize;
            fFrameSize = fMaxSize;
        } else {
            fNumTruncatedBytes = 0;
            fFrameSize = payloadLen;
        }
        memcpy(fTo, fBuffer + hdr.headerLength, fFrameSize);
        avshare_timestampToTimeval(ts, &fPresentationTime);
        fDurationInMicroseconds = 0; // live: send as soon as it is there
        ++fNbFrames;

        nextTask() = envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)FramedSource::afterGetting, this);
        return;
    }

    nextTask() = envir().taskScheduler().scheduleDelayedTask(AUDIO_POLL_US, pollAgain, this);
}
