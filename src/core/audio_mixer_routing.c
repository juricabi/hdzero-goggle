/*
 * Audio mixer: translation of the mixer state into codec controls.
 *
 * Pure code without firmware dependencies, so that utilities/audio_mixer/dryrun.sh
 * can build it on the host and check every issued control against the device's
 * `amixer contents` dump (mkapp/app/app/record/audio/audiocodec/alsa-util/).
 *
 * Signal paths (sun8iw16 codec, see docs/audio.md):
 *   MIC1 (built-in mic) -> MIC1 boost -> L/R output mixer (MIC1_G gain) / ADC input mixer
 *   MIC2 ("Line In")    -> MIC2 boost -> L/R output mixer (MIC2 BST gain) / ADC input mixer
 *   LINEIN (A/V)        ------------> L/R output mixer (LINEIN gain)    / ADC input mixer
 *   DAC (playback)      ------------> L/R output mixer
 *   L/R output mixer -> LINEOUT (L+R sum) -> lineout volume -> headphone jack
 *                    -> ADC input mixer ("l_output/r_output mixer" switches)
 *   ADC input mixer -> ADC input gain -> ADC -> AIF1 -> record daemon (48 kHz stereo AAC)
 */

#include <stdio.h>

#include "audio_mixer.h"

#define OUTPUT_GAIN_0DB 3 // register value of the -4.5..+6 dB / 1.5 dB step gains for 0 dB
// 'ADC volume' / 'DAC volume' (digital, 0.75 dB steps): 160 is what libmedia_mpp sets and the
// old firmware recorded with. 0 dB by the A64 manual's layout (0 = mute, 1 = -119.25 dB),
// +0.75 dB by the BSP's TLV (-119.25 dB at 0); see docs/audio.md.
#define DIGITAL_VOLUME_OLD 160
#define DAC_PLAYBACK       152 // 8 steps (6 dB) less: line out sums the left and right output mixers

// Preamp gains the old firmware recorded with: the codec driver sets them at probe from the
// device tree (sys_config.fex [codec] maingain / headsetmicgain), libmedia_mpp sets MIC1
// to 4 again, nothing sets MIC2. Same value in the official images of all three targets:
// Goggle Rev20250319, Goggle 2 Rev20250715 and BoxPro Rev20250517 (docs/audio.md).
#if defined(HDZGOGGLE) || defined(HDZGOGGLE2) || defined(HDZBOXPRO)
#define MIC1_BOOST_OLD 4 // maingain
#define MIC2_BOOST_OLD 4 // headsetmicgain
#endif

static const uint8_t field_max[AUDIO_MIXER_FIELD_COUNT] = {
    [AUDIO_MIXER_MIC] = AUDIO_MIXER_LEVEL_MAX,
    [AUDIO_MIXER_LINE_IN] = AUDIO_MIXER_LEVEL_MAX,
    [AUDIO_MIXER_AV] = AUDIO_MIXER_LEVEL_MAX,
    [AUDIO_MIXER_MIC_BOOST] = AUDIO_MIXER_BOOST_MAX,
    [AUDIO_MIXER_LINE_IN_BOOST] = AUDIO_MIXER_BOOST_MAX,
    [AUDIO_MIXER_HEADPHONES] = AUDIO_MIXER_VOLUME_MAX,
    [AUDIO_MIXER_PLAYBACK] = AUDIO_MIXER_VOLUME_MAX,
    [AUDIO_MIXER_DVR_MODE] = AUDIO_MIXER_DVR_CUSTOM,
    [AUDIO_MIXER_DVR_MIC] = 1,
    [AUDIO_MIXER_DVR_LINE_IN] = 1,
    [AUDIO_MIXER_DVR_AV] = 1,
    [AUDIO_MIXER_DVR_LEVEL] = AUDIO_MIXER_DVR_LEVEL_MAX,
    [AUDIO_MIXER_RX_AUDIO] = 1,
};

int audio_mixer_max(audio_mixer_field_t field) {
    if (field >= AUDIO_MIXER_FIELD_COUNT)
        return 0;
    return field_max[field];
}

void audio_mixer_defaults_from_audio_source(audio_mixer_settings_t *settings, long audio_source) {
    uint8_t *v = settings->value;

    // The old firmware passed the value to dvr_select_audio_source(uint8_t), which records
    // A/V In for anything above 2, and compared it with A/V In (2) for the receiver audio.
    const int old_value = (int)audio_source;
    uint8_t dvr_source = (uint8_t)audio_source;
    if (dvr_source > 2)
        dvr_source = 2;

    // Headphones: what the old firmware did (out_on): A/V at +6 dB, line out 31, no mic.
    v[AUDIO_MIXER_MIC] = 0;
    v[AUDIO_MIXER_LINE_IN] = 0;
    v[AUDIO_MIXER_AV] = 8;
    // Preamps and ADC gain as set by libmedia_mpp / the codec driver until now.
    v[AUDIO_MIXER_MIC_BOOST] = MIC1_BOOST_OLD;
    v[AUDIO_MIXER_LINE_IN_BOOST] = MIC2_BOOST_OLD;
    v[AUDIO_MIXER_DVR_LEVEL] = OUTPUT_GAIN_0DB;
    v[AUDIO_MIXER_HEADPHONES] = 31;
    // libmedia_mpp's line-out volume, which DVR playback used until now.
    v[AUDIO_MIXER_PLAYBACK] = 27;

    // DVR: exactly the one source of the old "Audio Source" setting, straight into the ADC.
    v[AUDIO_MIXER_DVR_MODE] = AUDIO_MIXER_DVR_CUSTOM;
    v[AUDIO_MIXER_DVR_MIC] = dvr_source == 0;
    v[AUDIO_MIXER_DVR_LINE_IN] = dvr_source == 1;
    v[AUDIO_MIXER_DVR_AV] = dvr_source == 2;

    // Built-in analog receiver audio: the old rtc6715.init(1, audio_source == A/V In)
    // (off with the old default, Mic).
    v[AUDIO_MIXER_RX_AUDIO] = old_value == 2;
}

typedef struct {
    char *buf;
    size_t size;
    size_t len;
} cmd_buf_t;

static void cset(cmd_buf_t *cb, const char *name, int value) {
    size_t room = cb->len < cb->size ? cb->size - cb->len : 0;
    int n = snprintf(cb->buf ? cb->buf + cb->len : NULL, room, "cset name='%s' %d\n", name, value);
    if (n > 0)
        cb->len += (size_t)n;
}

static int clamp(int value, int max) {
    if (value < 0)
        return 0;
    if (value > max)
        return max;
    return value;
}

size_t audio_mixer_build_commands(const audio_mixer_settings_t *settings, const audio_mixer_context_t *context, char *buf, size_t size) {
    cmd_buf_t cb = {.buf = buf, .size = size, .len = 0};
    int v[AUDIO_MIXER_FIELD_COUNT];

    if (buf && size)
        buf[0] = 0;

    for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++)
        v[i] = clamp(settings->value[i], field_max[i]);

    const bool playback = context->playback;
    const bool as_heard = v[AUDIO_MIXER_DVR_MODE] == AUDIO_MIXER_DVR_AS_HEARD;

    // Sources in the output mixer: muted while a DVR file plays, so only the file is heard.
    const int mon_mic = playback ? 0 : v[AUDIO_MIXER_MIC];
    const int mon_line = playback ? 0 : v[AUDIO_MIXER_LINE_IN];
    const int mon_av = playback ? 0 : v[AUDIO_MIXER_AV];

    // Sources straight into the ADC (custom DVR mode only)
    const int adc_mic = !as_heard && v[AUDIO_MIXER_DVR_MIC];
    const int adc_line = !as_heard && v[AUDIO_MIXER_DVR_LINE_IN];
    const int adc_av = !as_heard && v[AUDIO_MIXER_DVR_AV];

    // Line out volume; 0 switches the line out off
    const int volume = playback ? v[AUDIO_MIXER_PLAYBACK] : v[AUDIO_MIXER_HEADPHONES];
    const int line_out = (context->line_out || playback) && volume > 0;

    // Every switch the mixer owns, with the value it gets. They are written in two passes, the
    // ones that end up on before the ones that end up off: a path that stays never drops out,
    // and a change (another source, playback on/off, As Heard/Custom) never leaves the line
    // out or the ADC without a source in between. Without a connected source, DAPM powers the
    // line out down (PA off, then on again: a click, and on the Goggle 2 the driver sleeps
    // 160 ms in the PA-off event).
    const struct {
        const char *name;
        int on;
    } sw[] = {
        // output mixers: headphone mix, and the recording in As Heard
        {"Left Output Mixer MIC1Booststage Switch", mon_mic > 0},
        {"Right Output Mixer MIC1Booststage Switch", mon_mic > 0},
        {"Left Output Mixer MIC2Booststage Switch", mon_line > 0},
        {"Right Output Mixer MIC2Booststage Switch", mon_line > 0},
        {"Left Output Mixer LINEINL Switch", mon_av > 0},
        {"Right Output Mixer LINEINR Switch", mon_av > 0},
        {"Left Output Mixer DACL Switch", playback},
        {"Right Output Mixer DACR Switch", playback},
        {"Left Output Mixer DACR Switch", 0},
        {"Right Output Mixer DACL Switch", 0},
        {"DACL Mixer AIF1DA0L Switch", 1},
        {"DACR Mixer AIF1DA0R Switch", 1},
        // ADC input mixers: the recording
        {"LADC input Mixer MIC1 boost Switch", adc_mic},
        {"RADC input Mixer MIC1 boost Switch", adc_mic},
        {"LADC input Mixer MIC2 boost Switch", adc_line},
        {"RADC input Mixer MIC2 boost Switch", adc_line},
        {"LADC input Mixer LINEINL Switch", adc_av},
        {"RADC input Mixer LINEINR Switch", adc_av},
        {"LADC input Mixer l_output mixer Switch", as_heard},
        {"RADC input Mixer r_output mixer Switch", as_heard},
        {"LADC input Mixer r_output mixer Switch", 0},
        {"RADC input Mixer l_output mixer Switch", 0},
        {"AIF1 AD0L Mixer ADCL Switch", 1},
        {"AIF1 AD0R Mixer ADCR Switch", 1},
    };
    const size_t sw_count = sizeof(sw) / sizeof(sw[0]);

    // 1. a line out that goes off goes off first, so nothing below is heard
    if (!line_out)
        cset(&cb, "Lineout Switch", 0);

    // 2. gains: shared ones, and the output mixer gains of the sources that are on
    cset(&cb, "MIC1 boost AMP gain control", v[AUDIO_MIXER_MIC_BOOST]);
    cset(&cb, "MIC2 boost AMP gain control", v[AUDIO_MIXER_LINE_IN_BOOST]);
    if (mon_mic)
        cset(&cb, "MIC1_G boost stage output mixer control", mon_mic - 1);
    if (mon_line)
        cset(&cb, "MIC2 BST stage to L_R outp mixer gain", mon_line - 1);
    if (mon_av)
        cset(&cb, "LINEINL/R to L_R output mixer gain", mon_av - 1);
    cset(&cb, "ADC input gain control", v[AUDIO_MIXER_DVR_LEVEL]);
    cset(&cb, "ADC volume", DIGITAL_VOLUME_OLD);
    cset(&cb, "DAC volume", playback ? DAC_PLAYBACK : DIGITAL_VOLUME_OLD);

    // 3. every path that is on
    for (size_t i = 0; i < sw_count; i++)
        if (sw[i].on)
            cset(&cb, sw[i].name, 1);

    // 4. line out (mono: both outputs carry the sum of the left and right output mixers);
    //    one that is on is switched on once its sources are connected
    cset(&cb, "LINEOUTL Mux", 1);
    cset(&cb, "LINEOUTR Mux", 1);
    cset(&cb, "lineout volume", volume);
    if (line_out)
        cset(&cb, "Lineout Switch", 1);

    // 5. every path that is off, then the gains of the sources that are off back to 0 dB
    for (size_t i = 0; i < sw_count; i++)
        if (!sw[i].on)
            cset(&cb, sw[i].name, 0);
    if (!mon_mic)
        cset(&cb, "MIC1_G boost stage output mixer control", OUTPUT_GAIN_0DB);
    if (!mon_line)
        cset(&cb, "MIC2 BST stage to L_R outp mixer gain", OUTPUT_GAIN_0DB);
    if (!mon_av)
        cset(&cb, "LINEINL/R to L_R output mixer gain", OUTPUT_GAIN_0DB);

    return cb.len;
}
