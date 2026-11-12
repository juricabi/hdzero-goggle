#pragma once

// Live stream audio (RTSP AAC track): on-demand subsession announcing
// "MPEG4-GENERIC/<rate>/<channels>" (RFC 3640, mode AAC-hbr) fed by LiveAudioSource.
//
// The format (object type, rate, channels -> "config=") is read from the first ADTS header the
// record process queues once we connect. If no frame comes in time the subsession leaves the
// SDP (live555 skips a subsession without SDP lines): the session is then the video-only one,
// and the next DESCRIBE tries again.

#include <OnDemandServerMediaSubsession.hh>

class AACLiveAudioServerMediaSubsession: public OnDemandServerMediaSubsession {
public:
    static AACLiveAudioServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource);

protected:
    AACLiveAudioServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource);
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
    char     fConfigStr[8];
    char     fDoneFlag;
    unsigned fWaitedMs;
};
