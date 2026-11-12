#include "LiveNalVideoSource.hh"

#include <stdio.h>
#include <string.h>

extern "C"
{
#include "avshare.h"
}

#define VIDEO_POLL_US       1000   // same polling as H264FramedLiveSource
#define VIDEO_IDLE_POLL_US  20000  // ring not attached / reader not connected

LiveNalVideoSource* LiveNalVideoSource::createNew(UsageEnvironment& env) {
    return new LiveNalVideoSource(env);
}

LiveNalVideoSource::LiveNalVideoSource(UsageEnvironment& env)
    : FramedSource(env), fFrame(new unsigned char[MAXSTREAMBUF]), fFrameLen(0), fPos(0),
      fLastNalOfFrame(False) {
    fFramePresentationTime.tv_sec = 0;
    fFramePresentationTime.tv_usec = 0;
    printf("[MEDIA SERVER]live NAL video source opened\n");
    avshare_startMedia(MEDIA_VIDEO);
}

LiveNalVideoSource::~LiveNalVideoSource() {
    envir().taskScheduler().unscheduleDelayedTask(nextTask());
    avshare_stopMedia(MEDIA_VIDEO);
    delete[] fFrame;
    printf("[MEDIA SERVER]live NAL video source closed\n");
}

void LiveNalVideoSource::doGetNextFrame() {
    deliverNal();
}

void LiveNalVideoSource::pollAgain(void* clientData) {
    ((LiveNalVideoSource*)clientData)->deliverNal();
}

// Next NAL unit of the current frame, without its start code and trailing zero bytes.
Boolean LiveNalVideoSource::nextNal(unsigned& offset, unsigned& length) {
    while (fPos < fFrameLen) {
        unsigned start = fPos;
        unsigned i = start;
        unsigned end = fFrameLen;

        // find the next 00 00 01 start code
        while (i + 2 < fFrameLen) {
            if (fFrame[i + 2] > 1) {
                i += 3;
            } else if (fFrame[i] == 0 && fFrame[i + 1] == 0 && fFrame[i + 2] == 1) {
                end = i;
                break;
            } else {
                ++i;
            }
        }
        fPos = (end < fFrameLen) ? end + 3 : fFrameLen;

        while (end > start && fFrame[end - 1] == 0) {
            --end; // leading zero of a 4-byte start code / trailing_zero_8bits
        }
        if (end > start) {
            offset = start;
            length = end - start;
            return True;
        }
    }

    return False;
}

void LiveNalVideoSource::deliverNal() {
    nextTask() = NULL;
    if (!isCurrentlyAwaitingData()) return;

    unsigned offset = 0, length = 0;
    while (!nextNal(offset, length)) {
        uint64_t ts = 0;
        int len = avshare_readMediaFrame(MEDIA_VIDEO, fFrame, MAXSTREAMBUF, &ts);
        if (len == 0 || len == -1) {
            nextTask() = envir().taskScheduler().scheduleDelayedTask((len == 0) ? VIDEO_POLL_US : VIDEO_IDLE_POLL_US,
                                                                     pollAgain, this);
            return;
        }
        if (len < 0) {
            continue; // frame dropped (too large)
        }

        fFrameLen = (unsigned)len;
        fPos = 0;
        avshare_timestampToTimeval(ts, &fFramePresentationTime);

        // skip the first start code; a frame without any is taken as a single NAL unit
        if (fFrameLen >= 3 && fFrame[0] == 0 && fFrame[1] == 0) {
            unsigned i = 0;
            while (i + 2 < fFrameLen && fFrame[i] == 0 && fFrame[i + 1] == 0 && fFrame[i + 2] != 1) {
                ++i;
            }
            if (i + 2 < fFrameLen && fFrame[i + 2] == 1) {
                fPos = i + 3;
            }
        }
    }

    fLastNalOfFrame = (fPos >= fFrameLen);

    if (length > fMaxSize) {
        fNumTruncatedBytes = length - fMaxSize;
        fFrameSize = fMaxSize;
        printf("[MEDIA SERVER]live NAL video source: NAL unit truncated [%u/%u]\n", length, fMaxSize);
    } else {
        fNumTruncatedBytes = 0;
        fFrameSize = length;
    }
    memcpy(fTo, fFrame + offset, fFrameSize);
    fPresentationTime = fFramePresentationTime;
    fDurationInMicroseconds = 0;

    nextTask() = envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)FramedSource::afterGetting, this);
}

//----------------------------------------------------------------------------------------------
LiveH264VideoStreamDiscreteFramer*
LiveH264VideoStreamDiscreteFramer::createNew(UsageEnvironment& env, LiveNalVideoSource* source) {
    return new LiveH264VideoStreamDiscreteFramer(env, source);
}

LiveH264VideoStreamDiscreteFramer::LiveH264VideoStreamDiscreteFramer(UsageEnvironment& env, LiveNalVideoSource* source)
    : H264VideoStreamDiscreteFramer(env, source, False, False), fLiveSource(source) {
}

Boolean LiveH264VideoStreamDiscreteFramer::nalUnitEndsAccessUnit(u_int8_t nal_unit_type) {
    return fLiveSource->lastNalOfFrame() && H264or5VideoStreamDiscreteFramer::nalUnitEndsAccessUnit(nal_unit_type);
}

LiveH265VideoStreamDiscreteFramer*
LiveH265VideoStreamDiscreteFramer::createNew(UsageEnvironment& env, LiveNalVideoSource* source) {
    return new LiveH265VideoStreamDiscreteFramer(env, source);
}

LiveH265VideoStreamDiscreteFramer::LiveH265VideoStreamDiscreteFramer(UsageEnvironment& env, LiveNalVideoSource* source)
    : H265VideoStreamDiscreteFramer(env, source, False, False), fLiveSource(source) {
}

Boolean LiveH265VideoStreamDiscreteFramer::nalUnitEndsAccessUnit(u_int8_t nal_unit_type) {
    return fLiveSource->lastNalOfFrame() && H264or5VideoStreamDiscreteFramer::nalUnitEndsAccessUnit(nal_unit_type);
}
