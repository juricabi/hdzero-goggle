#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define MAX_pathLEN         128
#define MAX_streamNameLEN   16

#define LOCKFILE "/tmp/live.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)

#define REC_diskPATH        "/mnt/extsd"
#define REC_diskFULL        50  //MB
#define REC_diskDURATION    5000//ms
#define REC_dataFILE        "/tmp/live.dat"
#define REC_confPATH        "/data/confs"
#define REC_confFILE        "record.conf"
#define REC_confPathFILE    "/confs/record.conf"
#define REC_confDEFAULT     REC_confPATH"/record.conf"

#define LIVE_vencH265       true
#define LIVE_streamNAME     "hdzero"
#define LIVE_rtspPORT       8554
#define LIVE_rtspMULTICAST  true
#define LIVE_AUDIO          false
#define LIVE_audioRATE      48000
#define LIVE_audioCHANNELS  2

#define ZeroMemory(p, size) memset(p, 0, size)

#define LOG_PRIFIX          "[MEDIA SERVER]"
#define aloge(fmt, args...) printf("%s" fmt, LOG_PRIFIX, ##args)
#define alogd(fmt, args...) printf("%s" fmt, LOG_PRIFIX, ##args)

typedef struct
{
    void*      server;
    bool       h265;
    bool       runDynamicRTSPServer;
    bool       runLive;
    bool       runThread;
    uint16_t   port;

    //live thread
    pthread_mutex_t mutex;
    pthread_t   threadId;
    bool        bExit;

    char confFile[MAX_pathLEN];
    char streamName[MAX_streamNameLEN+1];

    //for multicast
    bool  IsMulticast;
    bool  isSSM;
    char  ipMulticast[16];

    // live stream audio (RTSP AAC track): [live] audio, default off
    bool     audio;
    unsigned audioSampleRate;   // [ai] sample_rate, used if the encoder format cannot be read
    unsigned audioChannels;     // [ai] channels
} LiveContext_t;
