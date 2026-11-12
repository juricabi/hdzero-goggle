#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>
#include <minIni.h>

#include "core/common.hh"
#include "core/osd.h"
#include "core/settings.h"
#include "ui/page_scannow.h"

#include "module.h"
#include "rapidfire.h"
#include "steadyview.h"
#include "steadyview_x.h"
#include "switch.h"
#include "trued.h"

#if defined(HDZGOGGLE) || defined(HDZGOGGLE2)
#define MODULE_BAY 1
#else
#define MODULE_BAY 0 // BoxPro: no bay, nothing is ever driven
#endif

static module_def_t *modules[MODULE_TYPES] = {
    NULL, // unknown/uncontrolled
    &switch_module,
    &rapidfire_module,
    &steadyview_module,
    &steadyview_x_module,
    &trued_module,
};

// Time a module gets to boot after the goggle switches the bay power on, before
// it is set up (pins, mode, channel).
#define MODULE_BOOT_MS 1000
// While the bay pins are found in use by the SDIO host, check again this often.
#define SDIO_RECHECK_MS 5000
// module_wifi_changed() waits at most this long for the pins to be released.
#define WIFI_RELEASE_MS 3000

// PIO PG_CFG0: function select of PG0..PG7, 4 bits each.  Function 2 of PG0-PG5
// is SDC1, the SDIO bus of the WiFi chip.
#define PIO_PG_CFG0     0x0300b0d8
#define PIO_FUNC_SDC1   2

/*
 * Threads: the dial (input thread, lvgl_mutex held), ELRS, the menu and the bay
 * power thread (every 10 ms) post requests under `lock` and return; the worker
 * thread alone touches the pins, with `lock` released during a transfer.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake;  // to the worker: something changed
static pthread_cond_t idle;  // from the worker: nothing left to do
static bool started = false;

// requests
static bool bay_powered = false;
static uint64_t power_on_ms = 0; // when the bay was switched on, 0: before the app started
static bool reinit = false;      // power switched: set the module up again
static bool mode_pending = false;
static int channel_pending = 0;  // 1-based, 0: none

// worker state
static module_def_t *active = NULL; // initialised module, holds the pins
static uint64_t resend_at_ms = 0;   // boot resend (module_def_t.boot_resend_ms) due
static bool sdio_busy = false;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static module_def_t *module_lookup(void) {
    int type = g_setting.module.type;
    if (!MODULE_BAY || type < 0 || type >= MODULE_TYPES)
        return NULL;
    return modules[type];
}

static bool pins_allowed(void) {
    return MODULE_BAY && g_setting.module.pins && !g_setting.wifi.enable;
}

// The module the bay should be running now (lock held).
static module_def_t *wanted(void) {
    if (!bay_powered || !pins_allowed())
        return NULL;
    return module_lookup();
}

#ifndef EMULATOR_BUILD
// Reads an SoC register through the sunxi_dump driver (as the awr script does).
static bool read_reg(uint32_t addr, uint32_t *val) {
    char buf[64];
    bool ok = false;
    int fd = open("/sys/class/sunxi_dump/dump", O_RDWR);
    if (fd < 0)
        return false;
    int n = snprintf(buf, sizeof(buf), "0x%08x", addr);
    if (write(fd, buf, n) == n) {
        lseek(fd, 0, SEEK_SET);
        n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            // the value is the last hex number of the answer
            char *last = NULL;
            for (char *p = strstr(buf, "0x"); p; p = strstr(p + 2, "0x"))
                last = p;
            if (last) {
                char *end;
                unsigned long v = strtoul(last, &end, 16);
                ok = end != last;
                *val = v;
            }
        }
    }
    close(fd);
    return ok;
}
#endif

// PG0-PG2 belong to the WiFi SDIO bus right now: the WiFi driver is loaded or the
// SDIO host has them in its pin function.  Exporting them then would cut WiFi off.
static bool bay_pins_busy(void) {
#ifdef EMULATOR_BUILD
    return false;
#else
    if (access("/sys/module/xradio_wlan", F_OK) == 0)
        return true;
    uint32_t cfg;
    if (read_reg(PIO_PG_CFG0, &cfg)) {
        for (int pin = 0; pin < 3; pin++) {
            if (((cfg >> (4 * pin)) & 0x7) == PIO_FUNC_SDC1)
                return true;
        }
    }
    return false;
#endif
}

static int fit_channel(module_def_t *m, int channel) {
    if (channel < 1)
        return 1;
    if (channel > m->num_channels)
        return (channel - 1) % m->num_channels + 1;
    return channel;
}

static void timed_wait(pthread_cond_t *cond, uint64_t until_ms) {
    struct timespec ts = {.tv_sec = until_ms / 1000, .tv_nsec = (until_ms % 1000) * 1000000};
    pthread_cond_timedwait(cond, &lock, &ts);
}

static void *module_worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lock);
    for (;;) {
        const uint64_t now = now_ms();
        module_def_t *want = wanted();
        uint64_t wake_at = 0;

        // pins released: bay off, type changed, Pin Control off, WiFi on, power cycled
        if (active && (active != want || reinit)) {
            module_def_t *m = active;
            pthread_mutex_unlock(&lock);
            LOGI("module: bay pins released");
            m->close();
            pthread_mutex_lock(&lock);
            active = NULL;
            resend_at_ms = 0;
            continue;
        }

        if (!active && want) {
            const uint64_t ready = power_on_ms ? power_on_ms + MODULE_BOOT_MS : 0;
            if (now < ready) {
                wake_at = ready;
            } else {
                pthread_mutex_unlock(&lock);
                const bool busy = bay_pins_busy();
                pthread_mutex_lock(&lock);
                if (busy != sdio_busy)
                    LOGW("module: bay pins %s", busy ? "in use by the WiFi SDIO bus (driver loaded or PG0-2 in SDC1 function), left alone" : "free again");
                sdio_busy = busy;
                if (want != wanted())
                    continue; // changed meanwhile
                if (busy) {
                    wake_at = now + SDIO_RECHECK_MS;
                } else {
                    // init sets the mode and the current channel; while the module still
                    // boots, channel changes wait for the boot resend
                    reinit = false;
                    mode_pending = false;
                    channel_pending = 0;
                    resend_at_ms = 0;
                    if (want->boot_resend_ms > 0 && power_on_ms && power_on_ms + want->boot_resend_ms > now)
                        resend_at_ms = power_on_ms + want->boot_resend_ms;
                    active = want;
                    pthread_mutex_unlock(&lock);
                    LOGI("module: type %d init, channel %d", g_setting.module.type, g_setting.source.analog_channel);
                    want->init();
                    pthread_mutex_lock(&lock);
                    continue;
                }
            }
        }

        if (active) {
            const bool boot_hold = resend_at_ms && now < resend_at_ms;
            if (mode_pending && !boot_hold) {
                mode_pending = false;
                pthread_mutex_unlock(&lock);
                LOGI("module: mode %d", g_setting.module.setting);
                active->set_mode();
                pthread_mutex_lock(&lock);
                continue;
            }
            if (resend_at_ms && !boot_hold) {
                const int ch = fit_channel(active, channel_pending ? channel_pending : g_setting.source.analog_channel);
                resend_at_ms = 0;
                channel_pending = 0;
                pthread_mutex_unlock(&lock);
                LOGI("module: channel %d (boot resend)", ch);
                active->set_channel(ch - 1);
                pthread_mutex_lock(&lock);
                continue;
            }
            if (channel_pending && !boot_hold) {
                const int ch = fit_channel(active, channel_pending);
                channel_pending = 0;
                pthread_mutex_unlock(&lock);
                LOGI("module: channel %d", ch);
                active->set_channel(ch - 1);
                pthread_mutex_lock(&lock);
                continue;
            }
            if (boot_hold && (!wake_at || resend_at_ms < wake_at))
                wake_at = resend_at_ms;
        }

        pthread_cond_broadcast(&idle);
        if (wake_at)
            timed_wait(&wake, wake_at);
        else
            pthread_cond_wait(&wake, &lock);
    }
    return NULL;
}

static int count_modes(const module_def_t *m) {
    int modes = 0;
    if (m && m->mode_names) {
        modes = 1;
        for (const char *p = m->mode_names; *p; p++)
            modes += *p == '\n';
    }
    return modes;
}

static void poke(void) {
    pthread_cond_signal(&wake);
}

void module_init() {
    if (!MODULE_BAY || started)
        return;

    if (g_setting.module.type < 0 || g_setting.module.type >= MODULE_TYPES) {
        LOGE("module: bad type %d in setting.ini, set to Unknown", g_setting.module.type);
        g_setting.module.type = MODULE_UNKNOWN;
    }
    const int modes = count_modes(module_lookup());
    if (g_setting.module.setting < 0 || g_setting.module.setting >= (modes ? modes : 1))
        g_setting.module.setting = 0;
    analog_channel_clamp();

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&wake, &attr);
    pthread_cond_init(&idle, &attr);
    pthread_condattr_destroy(&attr);

    // Without the bay power switch (batch 1) the bay is powered with the goggle and
    // the module booted long ago; batch 2 waits for the power thread's report.
    bay_powered = getHwRevision() < HW_REV_2;
    power_on_ms = 0;

    pthread_t tid;
    if (pthread_create(&tid, NULL, module_worker, NULL) == 0) {
        pthread_detach(tid);
        started = true;
    } else {
        LOGE("module: worker thread not started");
    }
    LOGI("module: type %d, mode %d, pin control %s, WiFi %s, bay %s", g_setting.module.type, g_setting.module.setting,
         g_setting.module.pins ? "on" : "off", g_setting.wifi.enable ? "on" : "off",
         bay_powered ? "always powered" : "power switched");
}

void module_settings_changed(bool mode_changed) {
    analog_channel_clamp();
    if (!started)
        return;
    pthread_mutex_lock(&lock);
    if (mode_changed)
        mode_pending = true;
    poke();
    pthread_mutex_unlock(&lock);
}

void module_set_channel(int channel) {
    if (!started || !module_bay_is_source())
        return;
    if (channel < 1 || channel > ANALOG_CHANNEL_NUM)
        return;
    pthread_mutex_lock(&lock);
    channel_pending = channel; // (unpowered: the power-on init sends the current one)
    poke();
    pthread_mutex_unlock(&lock);
}

// Called by the bay power thread (batch 2 hardware) when it switches the bay power.
// Off: the bay pins are released (inputs), so they do not feed the unpowered
// module.  On: the module missed everything sent while it was off; it is set up
// again MODULE_BOOT_MS later (and gets its channel again boot_resend_ms after
// power-on if its type asks for that).
void module_power_changed(int on) {
    if (!started)
        return;
    pthread_mutex_lock(&lock);
    bay_powered = on;
    if (on) {
        power_on_ms = now_ms();
        if (!power_on_ms)
            power_on_ms = 1;
    }
    reinit = true;
    poke();
    pthread_mutex_unlock(&lock);
}

void module_wifi_changed() {
    if (!started)
        return;
    pthread_mutex_lock(&lock);
    poke();
    if (g_setting.wifi.enable) {
        // the SDIO host takes PG0-PG2 when WiFi starts: let go of them first
        const uint64_t deadline = now_ms() + WIFI_RELEASE_MS;
        while (active && now_ms() < deadline)
            timed_wait(&idle, deadline);
        if (active)
            LOGE("module: bay pins still held while WiFi starts");
    }
    pthread_mutex_unlock(&lock);
}

module_status_t module_status() {
    module_status_t status;
    if (!MODULE_BAY)
        return MODULE_STATUS_NO_BAY;
    if (!module_lookup())
        return MODULE_STATUS_NONE;
    if (!g_setting.module.pins)
        return MODULE_STATUS_PINS_OFF;
    if (g_setting.wifi.enable)
        return MODULE_STATUS_WIFI;
    pthread_mutex_lock(&lock);
    if (!bay_powered)
        status = MODULE_STATUS_UNPOWERED;
    else if (active)
        status = MODULE_STATUS_ACTIVE;
    else if (sdio_busy)
        status = MODULE_STATUS_SDIO;
    else
        status = MODULE_STATUS_STARTING;
    pthread_mutex_unlock(&lock);
    return status;
}

int module_num_channels() {
    module_def_t *m = module_lookup();
    return m ? m->num_channels : 0;
}

char *module_channel_str(int channel) {
    module_def_t *m = module_lookup();
    if (!m)
        return "";
    return m->name(fit_channel(m, channel) - 1);
}

const char *module_mode_names(int type) {
    if (!MODULE_BAY || type < 0 || type >= MODULE_TYPES || !modules[type])
        return NULL;
    return modules[type]->mode_names;
}

bool module_bay_is_source() {
#if defined(HDZGOGGLE2)
    return g_setting.source.analog_module == SETTING_SOURCES_ANALOG_MODULE_EXTERNAL;
#elif defined(HDZGOGGLE)
    return true;
#else
    return false;
#endif
}

bool module_bay_controlled() {
    return module_lookup() && pins_allowed();
}

int analog_channel_count() {
    if (module_bay_is_source() && module_num_channels() > 0)
        return module_num_channels();
    return ANALOG_CHANNEL_NUM;
}

const char *analog_channel_name(int channel) {
    channel = analog_channel_fit(channel);
    if (module_bay_is_source() && module_num_channels() > 0)
        return module_channel_str(channel);
    return channel2str(0, 0, channel);
}

bool analog_channel_tunable() {
    return !module_bay_is_source() || module_bay_controlled();
}

int analog_channel_fit(int channel) {
    const int n = analog_channel_count();
    if (channel < 1)
        return 1;
    if (channel > n)
        return (channel - 1) % n + 1;
    return channel;
}

void analog_channel_clamp() {
    const int ch = analog_channel_fit(g_setting.source.analog_channel);
    if (ch != g_setting.source.analog_channel) {
        LOGI("analog channel %d -> %d (%d channels)", g_setting.source.analog_channel, ch, analog_channel_count());
        g_setting.source.analog_channel = ch;
        ini_putl("source", "analog_channel", g_setting.source.analog_channel, SETTING_INI);
    }
}
