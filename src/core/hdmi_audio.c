/* ===========================================================================
 * HDMI audio routing (EXPERIMENTAL, default off). See hdmi_audio.h and
 * docs/hdmi-audio.md. Kept in its own file so the default code paths stay
 * untouched: every entry point returns early while both settings are 0.
 * ======================================================================== */
#include "hdmi_audio.h"

#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <log/log.h>
#include <minIni.h>

#include "driver/it66121.h"
#include "record/appmsg.h"
#include "ui/page_common.h"
#include "util/system.h"

#define HDMI_AUDIO_SECTION    "hdmi"
#define HDMI_AUDIO_REC_SECTION "hdmi_audio" // read by the record daemon (src/record/pcmbridge.c)
#define HDMI_AUDIO_MSG_KEY    0x5680        // src/record/gogglemsg.c GoggleMSG_KEY (record queue)

// ALSA names: card ids come from the sunxi snddaudio machine driver ("snddaudio" + tdm_num),
// see docs/hdmi-audio.md. I2S0 = daudio@0x05090000 (tdm_num 0), I2S2 = daudio@0x05092000 (tdm_num 2).
#define HDMI_AUDIO_I2S0_PCM  "hw:snddaudio0"
#define HDMI_AUDIO_I2S2_PCM  "hw:snddaudio2"
#define HDMI_AUDIO_CODEC_PCM "plughw:audiocodec"

typedef enum {
    HDMI_AUDIO_ROUTE_NONE = 0,
    HDMI_AUDIO_ROUTE_DVR_TO_HDMI_OUT, // codec ADC (MPP AI, shared with the DVR) -> I2S0 -> IT66121
    HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR, // I2S2 (IT66021) -> codec DAC -> line out
} hdmi_audio_route_t;

typedef struct {
    long mtype;
    long cmd;
} hdmi_audio_msg_t; // same layout as CommandMsg_t in src/record/gogglemsg.h

static bool cfg_out = false;
static bool cfg_in = false;
static hdmi_audio_route_t route = HDMI_AUDIO_ROUTE_NONE;

static int hdmi_audio_send(long cmd) {
    hdmi_audio_msg_t msg = {MTYPE_RECORD, cmd};
    int q = msgget(HDMI_AUDIO_MSG_KEY, 0); // created by the record daemon, never by the app
    if (q < 0) {
        LOGE("hdmi audio: record daemon queue not available");
        return -1;
    }
    return msgsnd(q, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT);
}

static void hdmi_audio_line_out_dac(bool on) {
    char buf[128];
    if (on) {
        snprintf(buf, sizeof(buf), "%s out_on", AUDIO_SEL_SH);
        system_exec(buf);
    }
    snprintf(buf, sizeof(buf), "%s %s", AUDIO_SEL_SH, on ? "out_dac_on" : "out_dac_off");
    system_exec(buf);
}

void hdmi_audio_load(void) {
    cfg_out = ini_getl(HDMI_AUDIO_SECTION, "audio_out", 0, SETTING_INI) != 0;
    cfg_in = ini_getl(HDMI_AUDIO_SECTION, "audio_in", 0, SETTING_INI) != 0;
    IT66121_audio_config(cfg_out, IT66121_AUDIO_I2S);
    if (cfg_out || cfg_in)
        LOGI("hdmi audio (experimental): audio_out=%d audio_in=%d", cfg_out, cfg_in);
}

bool hdmi_audio_out_enabled(void) {
    return cfg_out;
}

bool hdmi_audio_in_enabled(void) {
    return cfg_in;
}

void hdmi_audio_set(bool out, bool in) {
    ini_putl(HDMI_AUDIO_SECTION, "audio_out", out, SETTING_INI);
    ini_putl(HDMI_AUDIO_SECTION, "audio_in", in, SETTING_INI);
    cfg_out = out;
    cfg_in = in;
    IT66121_audio_config(out, IT66121_AUDIO_I2S);
    if (IT66121_is_active() && IT66121_audio_is_on() != out)
        IT66121_audio(out);
    hdmi_audio_apply();
}

void hdmi_audio_apply(void) {
    hdmi_audio_route_t want = HDMI_AUDIO_ROUTE_NONE;

    if (!cfg_out && !cfg_in && route == HDMI_AUDIO_ROUTE_NONE)
        return; // default: nothing to do

    if (cfg_in && g_source_info.source == SOURCE_HDMI_IN)
        want = HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR; // HDMI out is closed while HDMI in is shown (app_switch_to_hdmi_in)
    else if (cfg_out && IT66121_is_active())
        want = HDMI_AUDIO_ROUTE_DVR_TO_HDMI_OUT;

    if (want == route)
        return;

    if (route != HDMI_AUDIO_ROUTE_NONE) {
        hdmi_audio_send(HDMIA_cmdSTOP);
        if (route == HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR) {
            hdmi_audio_line_out_dac(false);
            if (g_source_info.source == SOURCE_HDMI_IN)
                system_exec(AUDIO_SEL_SH " out_off"); // the state app_switch_to_hdmi_in() leaves
        }
    }

    if (want == HDMI_AUDIO_ROUTE_DVR_TO_HDMI_OUT) {
        ini_puts(HDMI_AUDIO_REC_SECTION, "src", "ai", REC_CONF);
        ini_puts(HDMI_AUDIO_REC_SECTION, "sink", HDMI_AUDIO_I2S0_PCM, REC_CONF);
        ini_puts(HDMI_AUDIO_REC_SECTION, "sink2", "", REC_CONF);
        ini_putl(HDMI_AUDIO_REC_SECTION, "i2s0_fix", 1, REC_CONF);
        hdmi_audio_send(HDMIA_cmdSTART);
    } else if (want == HDMI_AUDIO_ROUTE_HDMI_IN_MONITOR) {
        ini_puts(HDMI_AUDIO_REC_SECTION, "src", HDMI_AUDIO_I2S2_PCM, REC_CONF);
        ini_puts(HDMI_AUDIO_REC_SECTION, "sink", HDMI_AUDIO_CODEC_PCM, REC_CONF);
        ini_puts(HDMI_AUDIO_REC_SECTION, "sink2", "", REC_CONF);
        ini_putl(HDMI_AUDIO_REC_SECTION, "i2s0_fix", 0, REC_CONF);
        hdmi_audio_send(HDMIA_cmdSTART);
        hdmi_audio_line_out_dac(true);
    }

    LOGI("hdmi audio: route %d -> %d", route, want);
    route = want;
}
