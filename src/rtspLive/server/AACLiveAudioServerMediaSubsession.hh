#pragma once

// Live stream audio (RTSP AAC track): on-demand subsession announcing
// "MPEG4-GENERIC/<rate>/<channels>" (RFC 3640, mode AAC-hbr) fed by LiveAudioSource.

#include <OnDemandServerMediaSubsession.hh>

class AACLiveAudioServerMediaSubsession: public OnDemandServerMediaSubsession {
public:
    // defaultSampleRate/defaultChannels: the [ai] settings of the record conf, used when no
    // encoded frame shows up in time to read the real format from its ADTS header.
    static AACLiveAudioServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource,
                                                        unsigned defaultSampleRate, unsigned defaultChannels);

protected:
    AACLiveAudioServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource,
                                      unsigned defaultSampleRate, unsigned defaultChannels);
    virtual ~AACLiveAudioServerMediaSubsession();

protected: // redefined virtual functions
    virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate);
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic,
                                      FramedSource* inputSource);

private:
    void determineFormat();
    static void checkFormat(void* clientData);
    void checkFormat1();

private:
    Boolean  fFormatKnown;
    unsigned fProfile;       // audio object type - 1
    unsigned fSfIndex;
    unsigned fChannelConfig;
    unsigned fDefaultSampleRate;
    unsigned fDefaultChannels;
    char     fConfigStr[8];
    char     fDoneFlag;
    unsigned fWaitedMs;
};
