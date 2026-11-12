#include "AACLiveAudioServerMediaSubsession.hh"
#include "LiveAudioSource.hh"

#include <MPEG4GenericRTPSink.hh>
#include <stdio.h>

extern "C"
{
#include "avshare.h"
}

#define FORMAT_CHECK_MS    10
#define FORMAT_TIMEOUT_MS  2500 // the record process starts its audio chain when we connect

AACLiveAudioServerMediaSubsession*
AACLiveAudioServerMediaSubsession::createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
    return new AACLiveAudioServerMediaSubsession(env, reuseFirstSource);
}

AACLiveAudioServerMediaSubsession::AACLiveAudioServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
    : OnDemandServerMediaSubsession(env, reuseFirstSource),
      fFormatKnown(False), fProfile(1), fSfIndex(3), fChannelConfig(2),
      fDoneFlag(0), fWaitedMs(0) {
    fConfigStr[0] = '\0';
    printf("[MEDIA SERVER]AAC server media subsession construct\n");
}

AACLiveAudioServerMediaSubsession::~AACLiveAudioServerMediaSubsession() {
    printf("[MEDIA SERVER]AAC server media subsession destruct\n");
}

FramedSource* AACLiveAudioServerMediaSubsession::createNewStreamSource(unsigned /*clientSessionId*/, unsigned& estBitrate) {
    estBitrate = 96; // kbps, estimate (AAC 64 kbps + RTP/UDP/IP overhead)
    return LiveAudioSource::createNew(envir());
}

void AACLiveAudioServerMediaSubsession::checkFormat(void* clientData) {
    ((AACLiveAudioServerMediaSubsession*)clientData)->checkFormat1();
}

void AACLiveAudioServerMediaSubsession::checkFormat1() {
    unsigned char hdr[16];
    AdtsHeader adts;
    int len = avshare_peekAudio(hdr, sizeof(hdr));

    if (len > 0 && aacParseAdtsHeader(hdr, (unsigned)len, adts)) {
        fProfile = adts.profile;
        fSfIndex = adts.sfIndex;
        fChannelConfig = adts.channelConfig;
        fFormatKnown = True;
        fDoneFlag = ~0;
        return;
    }

    fWaitedMs += FORMAT_CHECK_MS;
    if (fWaitedMs >= FORMAT_TIMEOUT_MS) {
        fDoneFlag = ~0;
        return;
    }

    nextTask() = envir().taskScheduler().scheduleDelayedTask(FORMAT_CHECK_MS * 1000, checkFormat, this);
}

void AACLiveAudioServerMediaSubsession::determineFormat() {
    // Read the format the encoder really produces from the first ADTS header (the source that
    // was just created makes the record process start encoding), like H264VideoFileServer-
    // MediaSubsession waits for SPS/PPS.
    fDoneFlag = 0;
    fWaitedMs = 0;
    checkFormat1();
    if (!fDoneFlag) {
        envir().taskScheduler().doEventLoop(&fDoneFlag);
    }

    if (fFormatKnown) {
        aacConfigString(fProfile + 1, fSfIndex, fChannelConfig, fConfigStr);
        printf("[MEDIA SERVER]live audio: AAC object type %u, %u Hz, %u channels, config=%s\n",
               fProfile + 1, aacSamplingFrequency(fSfIndex), fChannelConfig, fConfigStr);
    } else {
        printf("[MEDIA SERVER]live audio: no audio frame within %u ms, audio track not announced\n", FORMAT_TIMEOUT_MS);
    }
}

RTPSink* AACLiveAudioServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic,
                                                             FramedSource* inputSource) {
    if (!fFormatKnown) {
        determineFormat();
        if (!fFormatKnown) {
            return NULL; // no SDP lines: the session is announced without audio
        }
    }

    ((LiveAudioSource*)inputSource)->setExpectedFormat(fProfile, fSfIndex, fChannelConfig);

    unsigned numChannels = (fChannelConfig == 7) ? 8 : fChannelConfig;
    return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
                                          aacSamplingFrequency(fSfIndex), "audio", "AAC-hbr",
                                          fConfigStr, numChannels);
}
