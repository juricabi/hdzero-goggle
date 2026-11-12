/* ===========================================================================
 * PCM bridge for HDMI audio (EXPERIMENTAL, default off). See pcmbridge.h and
 * docs/hdmi-audio.md. Nothing in here runs unless the app sends HDMIA_cmdSTART,
 * which it only does when setting.ini [hdmi] audio_out / audio_in is 1.
 * ======================================================================== */
#define LOG_TAG "pcmbridge"
#include <log/log.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <minIni.h>

#include "pcmbridge.h"
#include "pcmbridge_alsa.h"

#define PB_SECTION      "hdmi_audio"
#define PB_NAME_LEN     64
#define PB_MAX_SINKS    2
#define PB_CHANNELS     2     // sinks are always opened stereo; mono sources are duplicated
#define PB_CHUNK_FRAMES 4096  // largest block handled in one go
#define PB_WAIT_MS      200

// I2S0 control register (sunxi-daudio SUNXI_DAUDIO_CTL), see docs/hdmi-audio.md:
// bit18 BCLK_OUT, bit17 LRCK_OUT (SoC is clock master), bits[5:4] MODE_SEL (0 PCM, 1 I2S/left-justified).
// Vendor note mkapp/app/app/record/audio/i2s/i2s-daudio-setup.txt: "must do: aww 0x05090000 0x00060010".
#define PB_I2S0_CTL      0x05090000u
#define PB_I2S0_MODE_I2S 0x00000010u
#define PB_I2S0_MASTER   0x00060000u
#define PB_SUNXI_DUMP    "/sys/class/sunxi_dump/"

struct PcmBridge {
    pthread_t tid;
    volatile bool bExit;
    bool running;       // started and not yet stopped
    bool threadStarted; // tid is valid

    char src[PB_NAME_LEN];
    char sink[PB_MAX_SINKS][PB_NAME_LEN];
    bool i2s0Fix;
    unsigned latencyUs;

    unsigned rate;
    unsigned srcChannels;

    bool usesAi;
    int aiDev;
    AI_CHN aiChn;
    AIO_ATTR_S aiAttr;

    snd_pcm_t *cap;
    snd_pcm_t *out[PB_MAX_SINKS];

    int16_t buf[PB_CHUNK_FRAMES * PB_CHANNELS];
    unsigned long long framesIn, framesDropped;
};

/* ---- register poke through the sunxi_dump sysfs interface (same as /usr/sbin/awr, aww) ---- */
static int pb_sysfs_write(const char *file, const char *text) {
    int fd = open(file, O_WRONLY);
    if (fd < 0)
        return -1;
    int r = write(fd, text, strlen(text));
    close(fd);
    return r < 0 ? -1 : 0;
}

static int pb_reg_read(uint32_t addr, uint32_t *val) {
    char text[32];
    snprintf(text, sizeof(text), "0x%08x", addr);
    if (pb_sysfs_write(PB_SUNXI_DUMP "dump", text) < 0)
        return -1;
    int fd = open(PB_SUNXI_DUMP "dump", O_RDONLY);
    if (fd < 0)
        return -1;
    int n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    text[n] = 0;
    *val = (uint32_t)strtoul(text, NULL, 16);
    return 0;
}

static int pb_reg_write(uint32_t addr, uint32_t val) {
    char text[40];
    snprintf(text, sizeof(text), "0x%08x 0x%08x", addr, val);
    return pb_sysfs_write(PB_SUNXI_DUMP "write", text);
}

static void pb_fix_i2s0(void) {
    uint32_t v = 0, w;
    if (pb_reg_read(PB_I2S0_CTL, &v) < 0) {
        LOGE("i2s0 fix: cannot read 0x%08x", PB_I2S0_CTL);
        return;
    }
    w = (v & ~0x30u) | PB_I2S0_MODE_I2S | PB_I2S0_MASTER;
    if (w != v)
        pb_reg_write(PB_I2S0_CTL, w);
    LOGD("i2s0 fix: CTL 0x%08x -> 0x%08x", v, w);
}

/* ---- ALSA helpers ---- */
static snd_pcm_t *pb_open(const char *name, snd_pcm_stream_t dir, unsigned channels, unsigned rate, unsigned latencyUs) {
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, name, dir, SND_PCM_NONBLOCK);
    if (err < 0) {
        LOGE("open %s %s failed: %s", dir == SND_PCM_STREAM_CAPTURE ? "capture" : "playback", name, snd_strerror(err));
        return NULL;
    }
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, latencyUs);
    if (err < 0) {
        LOGE("%s: S16_LE %uch %uHz not accepted: %s", name, channels, rate, snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }
    LOGD("opened %s (%s, %uch, %uHz)", name, dir == SND_PCM_STREAM_CAPTURE ? "capture" : "playback", channels, rate);
    return pcm;
}

static void pb_write_sinks(PcmBridge_t *pb, const int16_t *data, unsigned frames) {
    for (int i = 0; i < PB_MAX_SINKS; i++) {
        if (pb->out[i] == NULL)
            continue;
        snd_pcm_sframes_t r = snd_pcm_writei(pb->out[i], data, frames);
        if (r == -EPIPE || r == -ESTRPIPE) {
            snd_pcm_recover(pb->out[i], (int)r, 1);
            r = snd_pcm_writei(pb->out[i], data, frames);
        }
        if (r < 0)
            pb->framesDropped += frames; // -EAGAIN: sink not running (no clock) or buffer full
        else if ((unsigned)r < frames)
            pb->framesDropped += frames - (unsigned)r;
    }
}

// Stereo-ize (mono source) into pb->buf and forward.
static void pb_forward(PcmBridge_t *pb, const int16_t *data, unsigned frames) {
    pb->framesIn += frames;
    if (pb->srcChannels == PB_CHANNELS) {
        pb_write_sinks(pb, data, frames);
        return;
    }
    while (frames > 0) {
        unsigned n = frames > PB_CHUNK_FRAMES ? PB_CHUNK_FRAMES : frames;
        for (unsigned k = 0; k < n; k++)
            pb->buf[2 * k] = pb->buf[2 * k + 1] = data[k];
        pb_write_sinks(pb, pb->buf, n);
        data += n;
        frames -= n;
    }
}

static void *pb_thread(void *arg) {
    PcmBridge_t *pb = (PcmBridge_t *)arg;
    bool firstFrame = true;
    unsigned long long lastLog = 0;

    LOGD("bridge thread: %s -> %s %s", pb->src, pb->sink[0], pb->sink[1]);
    while (!pb->bExit) {
        if (pb->usesAi) {
            AUDIO_FRAME_S frm;
            if (AW_MPI_AI_GetFrame(pb->aiDev, pb->aiChn, &frm, NULL, PB_WAIT_MS) != SUCCESS)
                continue;
            // mLen is the byte count of the interleaved frame (as in the Allwinner AI samples).
            unsigned frames = frm.mLen / (2 * pb->srcChannels);
            if (firstFrame) {
                LOGD("first AI frame: len=%u bytes, bitwidth=%d, soundmode=%d -> %u frames",
                     frm.mLen, (int)frm.mBitwidth, (int)frm.mSoundmode, frames);
                firstFrame = false;
            }
            pb_forward(pb, (const int16_t *)frm.mpAddr, frames);
            AW_MPI_AI_ReleaseFrame(pb->aiDev, pb->aiChn, &frm, NULL);
        } else {
            if (snd_pcm_wait(pb->cap, PB_WAIT_MS) <= 0)
                continue; // timeout: no clock from the I2S master (e.g. no HDMI audio)
            snd_pcm_sframes_t n = snd_pcm_readi(pb->cap, pb->buf, PB_CHUNK_FRAMES / 2);
            if (n == -EAGAIN)
                continue;
            if (n < 0) {
                snd_pcm_recover(pb->cap, (int)n, 1);
                snd_pcm_start(pb->cap);
                continue;
            }
            if (firstFrame) {
                LOGD("first capture block: %ld frames", (long)n);
                firstFrame = false;
            }
            pb_write_sinks(pb, pb->buf, (unsigned)n);
            pb->framesIn += (unsigned)n;
        }
        if (pb->framesIn - lastLog >= 10ULL * pb->rate) { // every ~10 s of audio
            LOGD("bridge: %llu frames in, %llu dropped", pb->framesIn, pb->framesDropped);
            lastLog = pb->framesIn;
        }
    }
    LOGD("bridge thread exit: %llu frames in, %llu dropped", pb->framesIn, pb->framesDropped);
    return NULL;
}

/* ---- MPP AI channel (shares AI device 0 with ai2aenc / ai2ao) ---- */
static int pb_ai_open(PcmBridge_t *pb, AiParams_t *ai) {
    memset(&pb->aiAttr, 0, sizeof(pb->aiAttr));
    pb->aiAttr.u32ChnCnt = ai->channels;
    pb->aiAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)ai->sampleRate;
    pb->aiAttr.enBitwidth = (AUDIO_BIT_WIDTH_E)(ai->bitsPerSample / 8 - 1);
    pb->aiDev = ai->aiDev;
    AW_MPI_AI_SetPubAttr(pb->aiDev, &pb->aiAttr); // fails harmlessly if the DVR already enabled the device
    AW_MPI_AI_Enable(pb->aiDev);

    for (AI_CHN chn = 0; chn < AIO_MAX_CHN_NUM; chn++) {
        ERRORTYPE ret = AW_MPI_AI_CreateChn(pb->aiDev, chn);
        if (ret == SUCCESS) {
            pb->aiChn = chn;
            if (AW_MPI_AI_EnableChn(pb->aiDev, chn) != SUCCESS) {
                LOGE("AI chn %d enable failed", chn);
                AW_MPI_AI_DestroyChn(pb->aiDev, chn);
                pb->aiChn = MM_INVALID_CHN;
                return -1;
            }
            LOGD("AI dev %d chn %d for HDMI audio", pb->aiDev, chn);
            return 0;
        }
        if (ret != ERR_AI_EXIST)
            LOGD("AI chn %d create ret 0x%x, trying next", chn, ret);
    }
    LOGE("no free AI channel");
    return -1;
}

static void pb_ai_close(PcmBridge_t *pb, bool disableAiDev) {
    if (pb->aiChn == MM_INVALID_CHN)
        return;
    AW_MPI_AI_DisableChn(pb->aiDev, pb->aiChn);
    AW_MPI_AI_ResetChn(pb->aiDev, pb->aiChn);
    AW_MPI_AI_DestroyChn(pb->aiDev, pb->aiChn);
    if (disableAiDev)
        AW_MPI_AI_Disable(pb->aiDev);
    pb->aiChn = MM_INVALID_CHN;
}

/* ---- public API ---- */
PcmBridge_t *pcmbridge_initSys(void) {
    PcmBridge_t *pb = (PcmBridge_t *)calloc(1, sizeof(PcmBridge_t));
    if (pb != NULL)
        pb->aiChn = MM_INVALID_CHN;
    return pb;
}

void pcmbridge_deinitSys(PcmBridge_t *pb, bool disableAiDev) {
    if (pb == NULL)
        return;
    pcmbridge_stop(pb, disableAiDev);
    free(pb);
}

bool pcmbridge_usesAi(PcmBridge_t *pb) {
    return pb != NULL && pb->running && pb->usesAi;
}

static void pb_close_all(PcmBridge_t *pb, bool disableAiDev) {
    for (int i = 0; i < PB_MAX_SINKS; i++) {
        if (pb->out[i]) {
            snd_pcm_drop(pb->out[i]);
            snd_pcm_close(pb->out[i]);
            pb->out[i] = NULL;
        }
    }
    if (pb->cap) {
        snd_pcm_close(pb->cap);
        pb->cap = NULL;
    }
    if (pb->usesAi)
        pb_ai_close(pb, disableAiDev);
    pb->usesAi = false;
}

void pcmbridge_stop(PcmBridge_t *pb, bool disableAiDev) {
    if (pb == NULL || !pb->running)
        return;
    pb->bExit = true;
    if (pb->threadStarted)
        pthread_join(pb->tid, NULL);
    pb->threadStarted = false;
    pb_close_all(pb, disableAiDev);
    pb->running = false;
    LOGD("stopped");
}

int pcmbridge_start(PcmBridge_t *pb, const char *confFile, AiParams_t *aiParams) {
    if (pb == NULL)
        return -1;
    if (pb->running)
        pcmbridge_stop(pb, false); // the caller decides about the AI device on its own stop

    ini_gets(PB_SECTION, "src", "ai", pb->src, PB_NAME_LEN, confFile);
    ini_gets(PB_SECTION, "sink", "hw:snddaudio0", pb->sink[0], PB_NAME_LEN, confFile);
    ini_gets(PB_SECTION, "sink2", "", pb->sink[1], PB_NAME_LEN, confFile);
    pb->i2s0Fix = ini_getl(PB_SECTION, "i2s0_fix", 1, confFile) != 0;
    pb->latencyUs = (unsigned)ini_getl(PB_SECTION, "latency_ms", 60, confFile) * 1000u;
    pb->rate = aiParams->sampleRate > 0 ? (unsigned)aiParams->sampleRate : 48000u;
    pb->usesAi = strcmp(pb->src, "ai") == 0;
    pb->framesIn = pb->framesDropped = 0;

    LOGD("start: src=%s sink=%s sink2=%s i2s0_fix=%d", pb->src, pb->sink[0], pb->sink[1], pb->i2s0Fix);

    if (pb->usesAi) {
        pb->srcChannels = (aiParams->channels == 1) ? 1 : 2;
        if (aiParams->bitsPerSample != 16) {
            LOGE("AI must be 16 bit, is %d", aiParams->bitsPerSample);
            pb->usesAi = false;
            return -1;
        }
        if (pb_ai_open(pb, aiParams) < 0) {
            pb_close_all(pb, false);
            return -1;
        }
    } else {
        pb->srcChannels = PB_CHANNELS;
        pb->cap = pb_open(pb->src, SND_PCM_STREAM_CAPTURE, PB_CHANNELS, pb->rate, pb->latencyUs);
        if (pb->cap == NULL)
            return -1;
        snd_pcm_start(pb->cap);
    }

    int nSinks = 0;
    for (int i = 0; i < PB_MAX_SINKS; i++) {
        if (pb->sink[i][0] == 0)
            continue;
        pb->out[i] = pb_open(pb->sink[i], SND_PCM_STREAM_PLAYBACK, PB_CHANNELS, pb->rate, pb->latencyUs);
        if (pb->out[i] == NULL)
            continue;
        nSinks++;
        if (pb->i2s0Fix && strstr(pb->sink[i], "snddaudio0") != NULL)
            pb_fix_i2s0(); // after hw_params, before the first write starts the stream
    }

    if (nSinks == 0) {
        LOGE("no sink could be opened");
        pb_close_all(pb, false);
        return -1;
    }

    pb->bExit = false;
    if (pthread_create(&pb->tid, NULL, pb_thread, pb) != 0) {
        LOGE("thread create failed");
        pb_close_all(pb, false);
        return -1;
    }
    pb->threadStarted = true;
    pb->running = true;
    return 0;
}
/* ===== end PCM bridge ===== */
