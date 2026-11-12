#pragma once

// Live stream video for the stream with audio (RTSP AAC track enabled).
//
// The default video path (H264FramedLiveSource + H264/H265VideoStreamFramer) parses the byte
// stream and makes up presentation times from the frame rate, which drift away from the real
// capture times audio is stamped with. This source instead delivers the NAL units of each frame
// in the ring with the frame's own timestamp, through a discrete framer: video and audio share
// one clock, and no frame waits for the next one's start code.

#include <FramedSource.hh>
#include <H264VideoStreamDiscreteFramer.hh>
#include <H265VideoStreamDiscreteFramer.hh>

class LiveNalVideoSource: public FramedSource {
public:
    static LiveNalVideoSource* createNew(UsageEnvironment& env);

    // the NAL unit delivered last is the last one of its ring frame (access unit)
    Boolean lastNalOfFrame() const { return fLastNalOfFrame; }

protected:
    LiveNalVideoSource(UsageEnvironment& env);
    virtual ~LiveNalVideoSource();

private:
    virtual void doGetNextFrame();
    static void pollAgain(void* clientData);
    void deliverNal();
    Boolean nextNal(unsigned& offset, unsigned& length);

private:
    unsigned char* fFrame;
    unsigned fFrameLen;
    unsigned fPos;        // parse position in fFrame
    struct timeval fFramePresentationTime;
    Boolean fLastNalOfFrame;
};

// Discrete framers that set the RTP marker on the last VCL NAL unit of each frame only
// (the default guesses "any VCL NAL unit", wrong for multi-slice pictures).
class LiveH264VideoStreamDiscreteFramer: public H264VideoStreamDiscreteFramer {
public:
    static LiveH264VideoStreamDiscreteFramer* createNew(UsageEnvironment& env, LiveNalVideoSource* source);

protected:
    LiveH264VideoStreamDiscreteFramer(UsageEnvironment& env, LiveNalVideoSource* source);
    virtual Boolean nalUnitEndsAccessUnit(u_int8_t nal_unit_type);

private:
    LiveNalVideoSource* fLiveSource;
};

class LiveH265VideoStreamDiscreteFramer: public H265VideoStreamDiscreteFramer {
public:
    static LiveH265VideoStreamDiscreteFramer* createNew(UsageEnvironment& env, LiveNalVideoSource* source);

protected:
    LiveH265VideoStreamDiscreteFramer(UsageEnvironment& env, LiveNalVideoSource* source);
    virtual Boolean nalUnitEndsAccessUnit(u_int8_t nal_unit_type);

private:
    LiveNalVideoSource* fLiveSource;
};
