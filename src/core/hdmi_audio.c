/* ===========================================================================
 * HDMI audio routing (EXPERIMENTAL, default off). See hdmi_audio.h and
 * docs/hdmi-audio.md. Kept in its own file so the default code paths stay
 * untouched: every entry point returns early while both settings are 0.
 * ======================================================================== */
#include "hdmi_audio.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>

#include <log/log.h>
#include <minIni.h>

#include "core/app_state.h"
#include "core/audio_mixer.h"
#include "driver/it66021.h"
#include "driver/it66121.h"
#include "record/appmsg.h"
#include "ui/page_common.h"

#define HDMI_AUDIO_SECTION     "hdmi"
#define HDMI_AUDIO_REQ_SECTION "hdmi_audio" // HDMIA_REQ_FILE / HDMIA_STATE_FILE (src/record/pcmbridge.c)
#define HDMI_AUDIO_MSG_KEY     0x5680       // src/record/gogglemsg.c GoggleMSG_KEY (record queue)

// ALSA names: card ids come from the sunxi snddaudio machine driver ("snddaudio" + tdm_num),
// see docs/hdmi-audio.md. I2S0 = daudio@0x05090000 (tdm_num 0), I2S2 = daudio@0x05092000 (tdm_num 2).
// The codec is opened at 48 kHz like the DVR capture: the codec's AIF1 has one sample-rate field
// for playback and capture (sun8iw16-codec.c codec_hw_params, SYS_SR_CTRL AIF1_FS).
#define HDMI_AUDIO_I2S0_PCM  "hw:snddaudio0"
#define HDMI_AUDIO_I2S2_PCM  "hw:snddaudio2"
#define HDMI_AUDIO_CODEC_PCM "plughw:audiocodec"

typedef enum {
    HDMI_AUDIO_ROUTE_NONE = 0,
    HDMI_AUDIO_ROUTE_DVR_TO_HDMI_OUT, // codec ADC (MPP AI, shared with the DVR) -> I2S0 -> IT66121
    HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR, // I2S2 (IT66021) -> codec DAC -> output mixer (jack, DVR)
} hdmi_audio_route_t;

typedef struct {
    long mtype;
    long cmd;
} hdmi_audio_msg_t; // same layout as CommandMsg_t in src/record/gogglemsg.h

static pthread_mutex_t ha_lock = PTHREAD_MUTEX_INITIALIZER; // UI, HDMI-in detect, ELRS and peripheral threads
static bool cfg_out = false;
static bool cfg_in = false;
static hdmi_audio_route_t route = HDMI_AUDIO_ROUTE_NONE; // the route the app wants
static bool line_out_hdmi = false;                       // hdmi_audio_line_out(true) is in effect
static long req_seq = 0;                                 // number of the request in HDMIA_REQ_FILE
static uint32_t next_try_ms = 0;                         // reconcile: when to send again
static unsigned tries = 0;
static long daemon_pid = 0; // record daemon pid last seen in HDMIA_STATE_FILE

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static int hdmi_audio_send(long cmd) {
    hdmi_audio_msg_t msg = {MTYPE_RECORD, cmd};
    int q = msgget(HDMI_AUDIO_MSG_KEY, 0); // created by the record daemon, never by the app
    if (q < 0)
        return -1; // not there yet (or restarting): hdmi_audio_poll() sends again
    return msgsnd(q, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT);
}

/* HDMI-in audio in the codec: the ONE place that switches it, and it only tells the audio mixer
 * (src/core/audio_mixer.c), which alone writes the codec's controls. On: the bridge plays I2S2
 * into the codec DAC, and the mixer takes the DAC in as a live source at the HDMI In level of the
 * Audio page, next to the analog sources at theirs; the line out stays on although HDMI in is the
 * source (Volume permitting); Record What I Hear records it with the rest of the headphone mix.
 * Off: the stock state of HDMI in (line out off), the DAC out of the mix. Off waits until the codec
 * has it, so the DAC leaves the mix before the bridge stops playing into it. */
static void hdmi_audio_line_out(bool hdmi_in_audio) {
    audio_mixer_set_live_dac(hdmi_in_audio);
    if (!hdmi_in_audio)
        audio_mixer_apply_wait(1000);
    line_out_hdmi = hdmi_in_audio;
}

// the request the record daemon applies on HDMIA_cmdSTART / HDMIA_cmdSTOP (RAM file, atomic)
static void hdmi_audio_write_request(void) {
    const char *tmp = HDMIA_REQ_FILE ".tmp";
    const bool act = route != HDMI_AUDIO_ROUTE_NONE;
    const bool out = route == HDMI_AUDIO_ROUTE_DVR_TO_HDMI_OUT;
    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        LOGE("hdmi audio: cannot write %s", tmp);
        return;
    }
    fprintf(f, "[%s]\nseq=%ld\nactive=%d\nsrc=%s\nsink=%s\nsink2=\ni2s0_fix=%d\n", HDMI_AUDIO_REQ_SECTION, req_seq, act,
            out ? "ai" : HDMI_AUDIO_I2S2_PCM, out ? HDMI_AUDIO_I2S0_PCM : HDMI_AUDIO_CODEC_PCM, out);
    fclose(f);
    rename(tmp, HDMIA_REQ_FILE);
}

static void hdmi_audio_request(void) { // new request: write it, send it, check the answer later
    req_seq++;
    hdmi_audio_write_request();
    if (hdmi_audio_send(route != HDMI_AUDIO_ROUTE_NONE ? HDMIA_cmdSTART : HDMIA_cmdSTOP) < 0)
        LOGI("hdmi audio: record daemon queue not available, will retry");
    tries = 0;
    next_try_ms = now_ms() + 3000; // the daemon answers in HDMIA_STATE_FILE
}

void hdmi_audio_load(void) {
    cfg_out = ini_getl(HDMI_AUDIO_SECTION, "audio_out", 0, SETTING_INI) != 0;
    cfg_in = ini_getl(HDMI_AUDIO_SECTION, "audio_in", 0, SETTING_INI) != 0;
    IT66121_audio_config(cfg_out, IT66121_AUDIO_I2S);
    IT66021_audio_config(cfg_in);
    if (cfg_out || cfg_in)
        LOGI("hdmi audio (experimental): audio_out=%d audio_in=%d", cfg_out, cfg_in);
}

bool hdmi_audio_out_enabled(void) {
    return cfg_out;
}

bool hdmi_audio_in_enabled(void) {
    return cfg_in;
}

static void hdmi_audio_apply_locked(bool hdmi_in_shown) {
    hdmi_audio_route_t want = HDMI_AUDIO_ROUTE_NONE;

    if (cfg_in && hdmi_in_shown)
        want = HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR; // HDMI out is closed while HDMI in is shown (app_switch_to_hdmi_in)
    else if (cfg_out && IT66121_is_active())
        want = HDMI_AUDIO_ROUTE_DVR_TO_HDMI_OUT;

    if (want == route)
        return;
    LOGI("hdmi audio: route %d -> %d", route, want);
    if (route == HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR && line_out_hdmi)
        hdmi_audio_line_out(false); // first: the DAC leaves the mixers before the bridge stops
    route = want;
    hdmi_audio_request(); // one request replaces the running route (the daemon stops it first)
    if (route == HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR)
        hdmi_audio_line_out(true);
}

void hdmi_audio_set(bool out, bool in) {
    ini_putl(HDMI_AUDIO_SECTION, "audio_out", out, SETTING_INI);
    ini_putl(HDMI_AUDIO_SECTION, "audio_in", in, SETTING_INI);
    pthread_mutex_lock(&ha_lock);
    cfg_out = out;
    cfg_in = in;
    IT66121_audio_config(out, IT66121_AUDIO_I2S);
    IT66021_audio_config(in); // the EDID is written by the next IT66021_edid()
    if (IT66121_is_active() && IT66121_audio_is_on() != out)
        IT66121_audio(out);
    // called outside a source switch: g_source_info.source is the shown source
    hdmi_audio_apply_locked(g_source_info.source == SOURCE_HDMI_IN && g_app_state == APP_STATE_VIDEO);
    pthread_mutex_unlock(&ha_lock);
}

void hdmi_audio_apply(bool hdmi_in_shown) {
    if (!cfg_out && !cfg_in && route == HDMI_AUDIO_ROUTE_NONE)
        return; // default: nothing to do
    pthread_mutex_lock(&ha_lock);
    hdmi_audio_apply_locked(hdmi_in_shown);
    pthread_mutex_unlock(&ha_lock);
}

/* Reconcile with what the record daemon reports (HDMIA_STATE_FILE): it may not have received
 * the request (queue not there yet), may have failed (sink missing or busy), or may have been
 * restarted (Storage page: formatsd.sh / chkfixsd.sh quit and start it, which also resets the
 * codec mixer). Then send again: after 3 s, 6 s, 12 s, 24 s, 30 s; after 6 failed starts give
 * up until the daemon restarts or the route changes. */
void hdmi_audio_poll(void) {
    char state[16];
    if (route == HDMI_AUDIO_ROUTE_NONE && req_seq == 0)
        return; // default: nothing was ever requested, no file access
    pthread_mutex_lock(&ha_lock);
    ini_gets(HDMI_AUDIO_REQ_SECTION, "state", "", state, sizeof(state), HDMIA_STATE_FILE);
    const long seq = ini_getl(HDMI_AUDIO_REQ_SECTION, "seq", -1, HDMIA_STATE_FILE);
    const long pid = ini_getl(HDMI_AUDIO_REQ_SECTION, "pid", 0, HDMIA_STATE_FILE);
    const bool alive = pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
    const bool want_on = route != HDMI_AUDIO_ROUTE_NONE;
    const bool done = alive && seq == req_seq && strcmp(state, want_on ? "running" : "stopped") == 0;

    if (alive && pid != daemon_pid) {
        if (daemon_pid != 0) {
            // (its libmedia_mpp init resets part of the codec mixer: the audio mixer's watcher
            // puts its state back within about 2 s, the DAC source included)
            LOGI("hdmi audio: record daemon restarted (pid %ld)", pid);
            tries = 0;
            next_try_ms = now_ms();
        }
        daemon_pid = pid;
    }
    if (done) {
        tries = 0;
    } else if (tries < 6 && (int32_t)(now_ms() - next_try_ms) >= 0) {
        if (alive && seq == req_seq && strcmp(state, "failed") == 0) {
            char err[64];
            ini_gets(HDMI_AUDIO_REQ_SECTION, "err", "", err, sizeof(err), HDMIA_STATE_FILE);
            LOGE("hdmi audio: record daemon could not start route %d: %s", route, err);
        }
        tries++;
        hdmi_audio_write_request();
        hdmi_audio_send(want_on ? HDMIA_cmdSTART : HDMIA_cmdSTOP);
        next_try_ms = now_ms() + (tries >= 4 ? 30000u : 3000u << tries);
        if (tries == 6)
            LOGE("hdmi audio: route %d not confirmed by the record daemon, giving up until it restarts", route);
    }
    pthread_mutex_unlock(&ha_lock);
}
