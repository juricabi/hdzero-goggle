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

#include "driver/it66021.h"
#include "driver/it66121.h"
#include "record/appmsg.h"
#include "ui/page_common.h"
#include "util/system.h"

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

/* Line out and codec mixer for HDMI-in audio, the ONE place that switches them (the mixer
 * module of the audio branch replaces this: HDMI in becomes its DAC source with its own
 * level). On: the bridge plays I2S2 into the codec DAC; the DAC goes to both output mixers,
 * the output mixers to the line out (headphone jack) and into the ADC (so the DVR records
 * the HDMI audio together with its selected source); the analog line-in bypass is taken out
 * of the output mixers (it would be mixed in). Off: the state app_switch_to_hdmi_in() leaves
 * (line out off), DAC out of the mixers, ADC from its direct inputs only. */
static void hdmi_audio_line_out(bool hdmi_in_audio) {
    static const char *const on =
        "amixer -q cset name='Left Output Mixer LINEINL Switch' 0;"
        "amixer -q cset name='Right Output Mixer LINEINR Switch' 0;"
        "amixer -q cset name='DACL Mixer AIF1DA0L Switch' 1;"
        "amixer -q cset name='DACR Mixer AIF1DA0R Switch' 1;"
        "amixer -q cset name='DAC volume' 152;"
        "amixer -q cset name='Left Output Mixer DACL Switch' 1;"
        "amixer -q cset name='Right Output Mixer DACR Switch' 1;"
        "amixer -q cset name='LADC input Mixer l_output mixer Switch' 1;"
        "amixer -q cset name='RADC input Mixer r_output mixer Switch' 1;"
        "amixer -q cset name='LINEOUTL Mux' 1;"
        "amixer -q cset name='LINEOUTR Mux' 1;"
        "amixer -q cset name='lineout volume' 31;"
        "amixer -q cset name='Lineout Switch' 1";
    static const char *const off =
        "amixer -q cset name='Lineout Switch' 0;"
        "amixer -q cset name='LADC input Mixer l_output mixer Switch' 0;"
        "amixer -q cset name='RADC input Mixer r_output mixer Switch' 0;"
        "amixer -q cset name='Left Output Mixer DACL Switch' 0;"
        "amixer -q cset name='Right Output Mixer DACR Switch' 0;"
        "amixer -q cset name='DAC volume' 160;"
        "amixer -q cset name='Left Output Mixer LINEINL Switch' 1;"
        "amixer -q cset name='Right Output Mixer LINEINR Switch' 1";
    system_exec(hdmi_in_audio ? on : off);
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
    hdmi_audio_apply_locked(route == HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR);
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
            LOGI("hdmi audio: record daemon restarted (pid %ld)", pid);
            tries = 0;
            next_try_ms = now_ms();
            if (route == HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR)
                hdmi_audio_line_out(true); // its libmedia_mpp init reset the codec mixer
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
