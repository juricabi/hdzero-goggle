/*
 * Host harness for the audio mixer: runs the real src/core/audio_mixer.c and
 * audio_mixer_routing.c (worker thread, setting.ini through the real minIni) against a
 * simulated codec. Driven by test_mixer.py, one command per line on stdin:
 *
 *   ini <path>             setting.ini to use (before init)
 *   codec <name>=<value>   set a codec control behind the mixer's back (driver defaults,
 *                          libmedia_mpp reset, ...)
 *   stuck <name>=<value>   the codec does not keep that control: it is back at <value>
 *                          after every batch
 *   amixer_delay <ms>      every amixer run takes that long (Goggle 2 PA-off sleep)
 *   init | apply | save | wait | rx | dump | unsaved | mark (prints MARK)
 *   set <key> <value> | get <key> | line_out <0|1> | playback <0|1> | live_dac <0|1>
 *   sleep <ms>             let the worker run (watcher)
 *   time_apply <n>         n x (set + apply) like dial steps; prints the slowest call
 *
 * Output (stdout): "BATCH <n>" + the batch as written to the command file + "END" for
 * every amixer run (the command line must be the firmware's), and one line per query.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>

#include "core/audio_mixer.h"
#include "util/system.h"

const char *harness_setting_ini = "setting.ini";

static pthread_mutex_t out_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t codec_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned amixer_delay_ms;
static int batch_count;
static char stuck_name[64];
static int stuck_value;

static struct {
    char name[64];
    int value;
} codec[96];
static int codec_count;

static void codec_set(const char *name, int value) {
    for (int i = 0; i < codec_count; i++) {
        if (strcmp(codec[i].name, name) == 0) {
            codec[i].value = value;
            return;
        }
    }
    if (codec_count < (int)(sizeof(codec) / sizeof(codec[0]))) {
        snprintf(codec[codec_count].name, sizeof(codec[0].name), "%s", name);
        codec[codec_count++].value = value;
    }
}

int audio_mixer_codec_read(const char *name, int *value) {
    int result = -1;
    pthread_mutex_lock(&codec_mutex);
    for (int i = 0; i < codec_count; i++) {
        if (strcmp(codec[i].name, name) == 0) {
            *value = codec[i].value;
            result = 0;
        }
    }
    pthread_mutex_unlock(&codec_mutex);
    return result;
}

int log_printf(const char *file, const char *func, int line, const int level, const char *fmt, ...) {
    (void)file, (void)func, (void)line, (void)level;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "LOG ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    return 0;
}

// The firmware's amixer call: check the command line, record the batch, apply it to the codec.
int system_exec(const char *command) {
    static const char expected[] = "amixer -q -s < ./audio_mixer.cmd";
    if (strcmp(command, expected) != 0) {
        fprintf(stderr, "unexpected command: %s\n", command);
        exit(3);
    }

    char batch[8192];
    FILE *fp = fopen("audio_mixer.cmd", "r");
    if (!fp) {
        fprintf(stderr, "no command file\n");
        exit(3);
    }
    const size_t len = fread(batch, 1, sizeof(batch) - 1, fp);
    batch[len] = 0;
    fclose(fp);

    if (amixer_delay_ms)
        usleep(amixer_delay_ms * 1000);

    pthread_mutex_lock(&codec_mutex);
    for (char *line = batch; line && *line;) {
        char *next = strchr(line, '\n');
        char name[64];
        int value;
        if (sscanf(line, "cset name='%63[^']' %d", name, &value) == 2)
            codec_set(name, value);
        line = next ? next + 1 : NULL;
    }
    if (stuck_name[0])
        codec_set(stuck_name, stuck_value);
    pthread_mutex_unlock(&codec_mutex);

    pthread_mutex_lock(&out_mutex);
    printf("BATCH %d\n%sEND\n", ++batch_count, batch);
    fflush(stdout);
    pthread_mutex_unlock(&out_mutex);
    return 0;
}

static void out(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    pthread_mutex_lock(&out_mutex);
    vprintf(fmt, args);
    fflush(stdout);
    pthread_mutex_unlock(&out_mutex);
    va_end(args);
}

static int field_of(const char *key) {
    for (int i = 0; i < AUDIO_MIXER_FIELD_COUNT; i++)
        if (strcmp(audio_mixer_key(i), key) == 0)
            return i;
    fprintf(stderr, "unknown key %s\n", key);
    exit(2);
}

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int main(void) {
    static char path[4096];
    char line[512], arg1[256], name[64];
    int value;

    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#')
            continue;

        if (strncmp(line, "ini ", 4) == 0 && sscanf(line + 4, "%4095s", path) == 1) {
            harness_setting_ini = path;
        } else if (strncmp(line, "codec ", 6) == 0 && sscanf(line + 6, "%255[^=]=%d", arg1, &value) == 2) {
            pthread_mutex_lock(&codec_mutex);
            codec_set(arg1, value);
            pthread_mutex_unlock(&codec_mutex);
        } else if (strncmp(line, "stuck ", 6) == 0 && sscanf(line + 6, "%63[^=]=%d", name, &value) == 2) {
            pthread_mutex_lock(&codec_mutex);
            memcpy(stuck_name, name, sizeof(stuck_name));
            stuck_value = value;
            codec_set(stuck_name, stuck_value);
            pthread_mutex_unlock(&codec_mutex);
        } else if (sscanf(line, "amixer_delay %d", &value) == 1) {
            amixer_delay_ms = (unsigned)value;
        } else if (strcmp(line, "init") == 0) {
            audio_mixer_init();
        } else if (strcmp(line, "apply") == 0) {
            audio_mixer_apply();
        } else if (strcmp(line, "save") == 0) {
            audio_mixer_save();
        } else if (strcmp(line, "wait") == 0) {
            out("WAIT %s\n", audio_mixer_apply_wait(5000) ? "ok" : "timeout");
        } else if (strcmp(line, "rx") == 0) {
            out("RX %d\n", audio_mixer_rx_audio());
        } else if (strcmp(line, "mark") == 0) {
            out("MARK\n");
        } else if (strcmp(line, "unsaved") == 0) {
            out("UNSAVED %d\n", audio_mixer_unsaved());
        } else if (strcmp(line, "dump") == 0) {
            pthread_mutex_lock(&codec_mutex);
            pthread_mutex_lock(&out_mutex);
            for (int i = 0; i < codec_count; i++)
                printf("CODEC %s=%d\n", codec[i].name, codec[i].value);
            fflush(stdout);
            pthread_mutex_unlock(&out_mutex);
            pthread_mutex_unlock(&codec_mutex);
        } else if (sscanf(line, "set %255s %d", arg1, &value) == 2) {
            audio_mixer_set(field_of(arg1), value);
        } else if (sscanf(line, "get %255s", arg1) == 1) {
            out("GET %s %d\n", arg1, audio_mixer_get(field_of(arg1)));
        } else if (sscanf(line, "line_out %d", &value) == 1) {
            audio_mixer_set_line_out(value != 0);
        } else if (sscanf(line, "playback %d", &value) == 1) {
            audio_mixer_set_playback(value != 0);
        } else if (sscanf(line, "live_dac %d", &value) == 1) {
            audio_mixer_set_live_dac(value != 0);
        } else if (sscanf(line, "sleep %d", &value) == 1) {
            usleep((useconds_t)value * 1000);
        } else if (sscanf(line, "time_apply %d", &value) == 1) {
            long long slowest = 0;
            for (int i = 0; i < value; i++) {
                const long long start = now_us();
                audio_mixer_set(AUDIO_MIXER_HEADPHONES, i % 2 ? 30 : 29);
                audio_mixer_apply();
                const long long took = now_us() - start;
                if (took > slowest)
                    slowest = took;
                usleep(5000);
            }
            out("APPLY_US %lld\n", slowest);
        } else {
            fprintf(stderr, "bad command: %s\n", line);
            return 2;
        }
    }
    return 0;
}
