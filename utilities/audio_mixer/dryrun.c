/*
 * Host-side dry run of the audio mixer routing (src/core/audio_mixer_routing.c).
 *
 *   dryrun [source=0|1|2] [<field>=<value> ...] [line_out=0|1] [playing=0|1]
 *
 * Starts from the migration defaults of the old "Audio Source" setting `source`
 * (default 0 = Mic), applies the given [mixer] fields (setting.ini key names),
 * line_out=0 (HDMI in) / playing=1 (DVR playback running), and prints the
 * amixer batch the firmware feeds to `amixer -q -s`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/audio_mixer.h"

static const char *const keys[AUDIO_MIXER_FIELD_COUNT] = {
    "mic", "line_in", "av", "mic_boost", "line_in_boost", "headphones",
    "playback", "dvr_mode", "dvr_mic", "dvr_line_in", "dvr_av", "dvr_level"};

int main(int argc, char *argv[]) {
    audio_mixer_settings_t settings;
    audio_mixer_context_t context = {.line_out = true, .playback = false};
    int source = 0;

    for (int i = 1; i < argc; i++)
        if (strncmp(argv[i], "source=", 7) == 0)
            source = atoi(argv[i] + 7);
    audio_mixer_defaults_from_audio_source(&settings, source);

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) {
            fprintf(stderr, "bad argument %s\n", argv[i]);
            return 2;
        }
        *eq = 0;
        const int value = atoi(eq + 1);
        if (strcmp(argv[i], "source") == 0)
            continue;
        if (strcmp(argv[i], "line_out") == 0) {
            context.line_out = value;
            continue;
        }
        if (strcmp(argv[i], "playing") == 0) {
            context.playback = value;
            continue;
        }
        int field = 0;
        while (field < AUDIO_MIXER_FIELD_COUNT && strcmp(keys[field], argv[i]) != 0)
            field++;
        if (field == AUDIO_MIXER_FIELD_COUNT) {
            fprintf(stderr, "unknown field %s\n", argv[i]);
            return 2;
        }
        settings.value[field] = (uint8_t)value;
    }

    char buf[4096];
    const size_t len = audio_mixer_build_commands(&settings, &context, buf, sizeof(buf));
    if (len >= sizeof(buf)) {
        fprintf(stderr, "command buffer too small (%zu)\n", len);
        return 1;
    }
    fputs(buf, stdout);
    return 0;
}
