#include "system.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <time.h>

#include "log/log.h"
#include "record/soc_reg.h"
#include "util/filesystem.h"

int system_exec(const char *command) {
    LOGI("System Execute: %s", command);
    return system(command);
}

static uint32_t system_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

int system_exec_sunxi_dump(const char *command) {
    static uint32_t last_timeout_ms = 0; // when the lock last could not be taken within 1 s
    static int timed_out = 0;
    const int quiet = strncmp(command, "awr ", 4) == 0;
    if (!quiet)
        LOGI("System Execute: %s", command);
    const int recent = __atomic_load_n(&timed_out, __ATOMIC_RELAXED) &&
                       (uint32_t)(system_now_ms() - __atomic_load_n(&last_timeout_ms, __ATOMIC_RELAXED)) < 10000u;
    const int fd = soc_reg_lock_n(recent ? 1 : SOC_REG_LOCK_TRIES);
    if (fd < 0) {
        if (!recent) { // logged once per 10 s window, not at every polled read
            __atomic_store_n(&last_timeout_ms, system_now_ms(), __ATOMIC_RELAXED);
            __atomic_store_n(&timed_out, 1, __ATOMIC_RELAXED);
            LOGW("sunxi_dump lock not available within 1 s: '%s' runs without it (as stock); "
                 "for 10 s the lock is tried once per command",
                 command);
        }
    } else {
        __atomic_store_n(&timed_out, 0, __ATOMIC_RELAXED);
    }
    const int r = system(command);
    soc_reg_unlock(fd);
    return r;
}

int system_script(const char *command) {
    LOGI("System Script: %s", command);

    // basename may edit argument
    const char *script = fs_basename(command);

    // Modify command to log std out and error to a temporary file
    char buffer[256];
    snprintf(buffer,
             sizeof(buffer), "%s > /tmp/%s.log 2>&1",
             command,
             script);
    int retval = system(buffer);

    // Read contents of temporary file and use logging framework
    snprintf(buffer, sizeof(buffer), "/tmp/%s.log", script);
    FILE *fp = fopen(buffer, "r");
    if (fp) {
        char *line = NULL;
        size_t len = 0;
        ssize_t read = 0;
        while ((read = getline(&line, &len, fp)) != -1) {
            line[strcspn(line, "\r\n")] = 0;
            LOGD("%s", line);
        }
        if (line) {
            free(line);
        }
        fclose(fp);
    }

    return retval;
}
