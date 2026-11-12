// Host-side test of the module bay worker (src/module/module.c, built as for the
// Goggle 2 emulator) with recording stand-ins for the module types, in real time
// (about 12 s).  Checks the True-D power-on sequence (init 1 s after bay power-on,
// channel again at 3.5 s, changes in between held back for that resend, so only
// one burst reaches the module while it boots), the pin release on power-off,
// Pin Control, the WiFi interlock and that no request blocks its caller.
//
//   make -C utilities/module_bay_test test

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../src/module/module.c"

// ---- stand-ins for the rest of the firmware ----

setting_t g_setting;

int log_printf(const char *file, const char *func, int line, const int level, const char *fmt, ...) {
    (void)file, (void)func, (void)line, (void)level, (void)fmt;
    return 0;
}

int ini_putl(const char *Section, const char *Key, long Value, const char *Filename) {
    (void)Section, (void)Key, (void)Value, (void)Filename;
    return 1;
}

char *channel2str(uint8_t is_hdzero, uint8_t is_lowband, uint8_t channel) {
    static char buf[8];
    (void)is_hdzero, (void)is_lowband;
    snprintf(buf, sizeof(buf), "#%d", channel);
    return buf;
}

hw_revision_t getHwRevision() {
    return HW_REV_2; // bay power switched by the power thread
}

// ---- recording module types ----

#define MAX_EVENTS 64
typedef struct {
    uint64_t t; // ms since the test's t0
    char op;    // 'i' init, 'c' close, 'h' channel, 'm' mode
    int arg;
} event_t;

static event_t events[MAX_EVENTS];
static int n_events;
static uint64_t t0;
static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;

static void record(char op, int arg) {
    pthread_mutex_lock(&ev_lock);
    if (n_events < MAX_EVENTS)
        events[n_events++] = (event_t){now_ms() - t0, op, arg};
    pthread_mutex_unlock(&ev_lock);
}

static void rec_init() { record('i', g_setting.source.analog_channel - 1); usleep(170000); } // a True-D burst
static void rec_close() { record('c', 0); }
static void rec_channel(int ch) { record('h', ch); usleep(170000); }
static void rec_mode() { record('m', g_setting.module.setting); }
static char *rec_name(int ch) { (void)ch; return "x"; }

module_def_t switch_module = {8, rec_name, rec_init, rec_close, rec_channel, rec_mode, "a\nb\nc", 0};
module_def_t rapidfire_module = {48, rec_name, rec_init, rec_close, rec_channel, rec_mode, NULL, 0};
module_def_t steadyview_module = {48, rec_name, rec_init, rec_close, rec_channel, rec_mode, NULL, 0};
module_def_t steadyview_x_module = {48, rec_name, rec_init, rec_close, rec_channel, rec_mode, NULL, 0};
module_def_t trued_module = {48, rec_name, rec_init, rec_close, rec_channel, rec_mode, NULL, 3500};

// ---- test ----

static int failures;

static void check(int ok, const char *what) {
    printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
    failures += !ok;
}

static void sleep_until(uint64_t t) {
    while (now_ms() - t0 < t)
        usleep(5000);
}

static int find(char op, int nth, event_t *out) {
    for (int i = 0; i < n_events; i++)
        if (events[i].op == op && nth-- == 0) {
            *out = events[i];
            return 1;
        }
    return 0;
}

static int count(char op, uint64_t from, uint64_t to) {
    int n = 0;
    for (int i = 0; i < n_events; i++)
        n += events[i].op == op && events[i].t >= from && events[i].t < to;
    return n;
}

static uint64_t timed_call_ms(void (*fn)(void)) {
    uint64_t a = now_ms();
    fn();
    return now_ms() - a;
}

static void power_on(void) { module_power_changed(1); }
static void power_off(void) { module_power_changed(0); }
static void wifi_changed(void) { module_wifi_changed(); }

int main(void) {
    event_t e;

    g_setting.module.type = MODULE_TRUED;
    g_setting.module.pins = true;
    g_setting.wifi.enable = false;
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_EXTERNAL;
    g_setting.source.analog_channel = 33; // R1

    printf("True-D, Goggle 2 with the bay power switch:\n");
    t0 = now_ms();
    module_init();
    usleep(300000);
    check(n_events == 0, "nothing sent while the bay is unpowered");
    check(module_status() == MODULE_STATUS_UNPOWERED, "status: waiting for bay power");

    t0 = now_ms();
    n_events = 0;
    check(timed_call_ms(power_on) < 5, "power-on report returns at once (power thread)");
    sleep_until(1500);
    module_set_channel(5); // dial during the True-D's boot screen
    check(find('i', 0, &e) && e.t >= 1000 && e.t < 1100, "init 1.0 s after power-on");
    check(e.arg == 32, "init with the stored channel (R1)");
    sleep_until(3000);
    check(count('h', 0, 3000) == 0, "channel change at 1.5 s held back during the boot screen");
    sleep_until(4000);
    check(find('h', 0, &e) && e.t >= 3500 && e.t < 3600 && e.arg == 4, "held channel (5) sent with the 3.5 s resend");
    check(count('i', 0, 3500) + count('h', 0, 3500) == 1, "one burst (50 bytes) before the True-D's loop runs");
    check(module_status() == MODULE_STATUS_ACTIVE, "status: active");

    module_set_channel(7);
    sleep_until(4300);
    check(find('h', 1, &e) && e.t < 4100 && e.arg == 6, "later channel change sent at once");

    check(timed_call_ms(power_off) < 5, "power-off report returns at once");
    sleep_until(4500);
    check(find('c', 0, &e) && e.t < 4400, "pins released after power-off");
    module_set_channel(9);
    sleep_until(4700);
    check(count('h', 4300, 4700) == 0, "nothing sent while unpowered");

    power_on();
    sleep_until(5000);
    power_off(); // quick off/on: the module starts over
    power_on();
    uint64_t on = now_ms() - t0;
    sleep_until(on + 1200);
    check(find('i', 1, &e) && e.t >= on + 1000, "power cycle: init again 1 s after the last power-on");

    printf("Pin Control and WiFi:\n");
    sleep_until(on + 3700); // past the boot resend
    int closes = count('c', 0, 1000000);
    module_set_channel(4); // a burst is going out when WiFi is switched on
    usleep(30000);
    g_setting.wifi.enable = true;
    uint64_t waited = timed_call_ms(wifi_changed);
    check(count('c', 0, 1000000) == closes + 1 && waited >= 100,
          "WiFi on during a burst: waits for it, pins released before returning");
    printf("    (waited %llu ms)\n", (unsigned long long)waited);
    check(module_status() == MODULE_STATUS_WIFI, "status: paused while WiFi is on");
    module_set_channel(3);
    usleep(300000);
    check(count('h', now_ms() - t0 - 300, 1000000) == 0 && count('i', now_ms() - t0 - 300, 1000000) == 0,
          "nothing sent while WiFi is on");
    g_setting.wifi.enable = false;
    uint64_t back = now_ms() - t0;
    wifi_changed();
    usleep(300000);
    check(count('i', back, 1000000) == 1, "WiFi off: module set up again (bay long powered: at once)");

    g_setting.module.pins = false;
    uint64_t off = now_ms() - t0;
    module_settings_changed(false);
    usleep(100000);
    check(count('c', off, 1000000) == 1 && module_status() == MODULE_STATUS_PINS_OFF, "Pin Control Off: pins released");

    printf("Switch (8 channels):\n");
    g_setting.module.pins = true;
    g_setting.module.type = MODULE_SWITCH;
    g_setting.source.analog_channel = 33;
    uint64_t sw = now_ms() - t0;
    module_settings_changed(false);
    usleep(300000);
    check(g_setting.source.analog_channel == 1, "stored channel R1 (33) brought into 1..8: CH1");
    check(analog_channel_count() == 8 && analog_channel_fit(17) == 1 && analog_channel_fit(24) == 8,
          "ELRS/elsewhere: (ch - 1) % 8 + 1");
    check(count('i', sw, 1000000) == 1, "switch init");
    g_setting.module.setting = 1;
    module_settings_changed(true);
    usleep(100000);
    check(find('m', 0, &e) && e.arg == 1, "pin order change applied as a mode change");
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_INTERNAL;
    check(analog_channel_count() == 48 && analog_channel_tunable(), "Goggle 2 internal receiver: 48 channels");
    module_set_channel(12);
    usleep(100000);
    check(count('h', now_ms() - t0 - 100, 1000000) == 0, "internal receiver selected: bay module not tuned");

    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
