#pragma once

// Live stream audio (RTSP AAC track): reads the ADTS frames queued by the record process in the
// shared ring and delivers raw AAC access units (ADTS header removed) for MPEG4GenericRTPSink.

#include <FramedSource.hh>

struct AdtsHeader {
    unsigned profile;       // audio object type - 1 (1 = AAC-LC)
    unsigned sfIndex;       // sampling frequency index
    unsigned channelConfig;
    unsigned headerLength;  // 7, or 9 with CRC
    unsigned frameLength;   // header + payload
    unsigned numRawBlocks;  // raw data blocks in this ADTS frame
};

// Returns True and fills "hdr" when "data" starts with a valid ADTS header.
Boolean aacParseAdtsHeader(unsigned char const* data, unsigned len, AdtsHeader& hdr);
// Sampling frequency of an ADTS/AudioSpecificConfig frequency index (0 if invalid).
unsigned aacSamplingFrequency(unsigned sfIndex);
// Frequency index of a sampling frequency (15 if not a standard one).
unsigned aacSamplingFrequencyIndex(unsigned frequency);
// 2-byte AudioSpecificConfig as the 4 hex digits of the SDP "config=" parameter.
void aacConfigString(unsigned audioObjectType, unsigned sfIndex, unsigned channelConfig, char* out /* >= 5 bytes */);

class LiveAudioSource: public FramedSource {
public:
    static LiveAudioSource* createNew(UsageEnvironment& env);

    // The stream format announced in the SDP: frames in another format are dropped,
    // because the format cannot change once a client has set up the stream.
    void setExpectedFormat(unsigned profile, unsigned sfIndex, unsigned channelConfig);

protected:
    LiveAudioSource(UsageEnvironment& env);
    virtual ~LiveAudioSource();

private:
    virtual void doGetNextFrame();
    static void pollAgain(void* clientData);
    void deliverFrame();

private:
    unsigned char* fBuffer;
    Boolean  fHaveExpectedFormat;
    AdtsHeader fExpected;
    unsigned fNbFrames;
    unsigned fNbDropped;
};
