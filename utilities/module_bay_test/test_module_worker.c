// Host-side test of the module bay worker (src/module/module.c, built as for the
// Goggle 2 emulator) with recording stand-ins for the module types, in real time
// (about 15 s).  The stand-ins' init() never sends a channel, so the checks below
// catch a worker that relies on a type's init to put the module on its channel.
//
// Checks: the True-D power-on sequence (init 1 s after bay power-on, the channel
// right after it and again at 3.5 s, changes in between held back for that
// resend), the pin release on power-off, Pin Control, the WiFi interlock, that no
// request blocks its caller, the stored channels (Switch list, type round trips,
// built-in receiver vs bay module on the Goggle 2, nothing stored while the bay is
// not controlled) and the source change.
//
// Timing: lower bounds are exact (the worker must not act early), upper bounds
// have SLACK_MS for a loaded host.
//
//   make -C utilities/module_bay_test test

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../src/module/module.c"

#define SLACK_MS 300

// ---- stand-ins for the rest of the firmware ----

setting_t g_setting;

int log_printf(const char *file, const char *func, int line, const int level, const char *fmt, ...) {
    (void)file, (void)func, (void)line, (void)level, (void)fmt;
    return 0;
}

static int ini_writes;
int ini_putl(const char *Section, const char *Key, long Value, const char *Filename) {
    (void)Section, (void)Key, (void)Value, (void)Filename;
    ini_writes++;
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

#define MAX_EVENTS 256
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

static void rec_init() { record('i', 0); } // takes the pins, sends no channel
static void rec_close() { record('c', 0); }
// a True-D burst: 5 packets of ~11 ms (not interruptible), 30 ms apart (abort points)
static void rec_channel(int ch) {
    record('h', ch);
    for (int i = 0; i < 5; i++) {
        if (i && !module_sleep_ms(30)) {
            record('a', i); // cut short: pins to be released
            return;
        }
        usleep(11000);
    }
}
static void rec_mode() { record('m', g_setting.module.setting); }
static char *rec_name(int ch) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "n%d", ch);
    return buf;
}

#define STAND_IN(n, modes, resend) \
    {.num_channels = n, .name = rec_name, .init = rec_init, .close = rec_close, .set_channel = rec_channel, \
     .set_mode = rec_mode, .mode_names = modes, .boot_ms = 0, .boot_resend_ms = resend}
module_def_t switch_module = STAND_IN(8, "a\nb\nc", 0);
module_def_t rapidfire_module = STAND_IN(48, "a\nb\nc", 0);
module_def_t steadyview_module = STAND_IN(48, NULL, 0);
module_def_t steadyview_x_module = STAND_IN(48, NULL, 0);
module_def_t trued_module = STAND_IN(48, NULL, 3500);

// ---- test ----

static int failures;

static void check(int ok, const char *what) {
    printf("  %-74s %s\n", what, ok ? "ok" : "FAIL");
    failures += !ok;
}

static uint64_t t_now(void) { return now_ms() - t0; }

static void sleep_until(uint64_t t) {
    while (t_now() < t)
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

// first event of type op at or after from
static int find_from(char op, uint64_t from, event_t *out) {
    for (int i = 0; i < n_events; i++)
        if (events[i].op == op && events[i].t >= from) {
            *out = events[i];
            return 1;
        }
    return 0;
}

// the event right after the first `op` at or after from
static int next_after(char op, uint64_t from, event_t *out) {
    for (int i = 0; i + 1 < n_events; i++)
        if (events[i].op == op && events[i].t >= from) {
            *out = events[i + 1];
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

// waits until the worker has nothing left to do (at most ~2 s)
static void settle(void) {
    usleep(20000);
    for (int i = 0; i < 200; i++) {
        pthread_mutex_lock(&lock);
        const bool busy = channel_pending || mode_pending || setup_channel || release_requested() || (!active && wanted());
        pthread_mutex_unlock(&lock);
        if (!busy)
            break;
        usleep(10000);
    }
    usleep(200000); // a transfer in progress
}

static void power_on(void) { module_power_changed(1); }
static void power_off(void) { module_power_changed(0); }
static void wifi_changed(void) { module_wifi_changed(); }

int main(void) {
    event_t e;
    uint64_t t;

    g_setting.module.type = MODULE_TRUED;
    g_setting.module.pins = true;
    g_setting.wifi.enable = false;
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_EXTERNAL;
    g_setting.source.analog_channel = 12; // built-in receiver: B4
    g_setting.module.channel = 33;        // bay module: R1

    printf("True-D, Goggle 2 with the bay power switch:\n");
    t0 = now_ms();
    module_init();
    usleep(300000);
    check(n_events == 0, "nothing sent while the bay is unpowered");
    check(module_status() == MODULE_STATUS_UNPOWERED, "status: waiting for bay power");

    t0 = now_ms();
    n_events = 0;
    check(timed_call_ms(power_on) < 50, "power-on report returns at once (power thread)");
    sleep_until(1500);
    analog_channel_set(5); // dial during the True-D's boot screen
    check(find('i', 0, &e) && e.t >= 1000 && e.t < 1000 + SLACK_MS, "init 1.0 s after power-on");
    check(next_after('i', 0, &e) && e.op == 'h' && e.arg == 32,
          "right after init (which sends nothing): the stored channel R1 (set-up)");
    sleep_until(3000);
    check(count('h', 0, 3000) == 1, "channel change at 1.5 s held back during the boot screen");
    sleep_until(3500 + SLACK_MS);
    check(find('h', 1, &e) && e.t >= 3500 && e.t < 3500 + SLACK_MS && e.arg == 4,
          "held channel (5) sent with the 3.5 s resend");
    check(count('h', 0, 3500) == 1, "one burst (50 bytes) before the True-D's loop runs");
    check(module_status() == MODULE_STATUS_ACTIVE, "status: active");
    check(g_setting.module.channel == 5 && g_setting.source.analog_channel == 12,
          "dial stored the bay module's channel, not the built-in receiver's");

    t = t_now();
    analog_channel_set(7);
    sleep_until(t + SLACK_MS);
    check(find_from('h', t, &e) && e.t < t + SLACK_MS && e.arg == 6, "later channel change sent at once");

    t = t_now();
    analog_channel_set(8); // power-off during a burst
    usleep(50000);
    t = t_now();
    check(timed_call_ms(power_off) < 50, "power-off report returns at once");
    sleep_until(t + SLACK_MS);
    check(find_from('c', t, &e) && e.t < t + 11 + 50, "pins released after power-off, burst cut short (< 60 ms)");
    check(find_from('a', t, &e), "  (the burst stopped between packets)");
    analog_channel_set(9);
    t = t_now();
    usleep(200000);
    check(count('h', t, 1000000) == 0, "nothing sent while unpowered");

    power_on();
    usleep(300000);
    power_off(); // quick off/on: the module starts over
    power_on();
    uint64_t on = t_now();
    sleep_until(on + 1000 + SLACK_MS);
    check(find_from('i', on, &e) && e.t >= on + 1000 && count('i', on - 400, on + 1000) == 0,
          "power cycle: init again 1 s after the last power-on (not before)");
    check(next_after('i', on, &e) && e.op == 'h' && e.arg == 8, "  and the current channel (9) right after it");

    printf("Pin Control and WiFi:\n");
    sleep_until(on + 3500 + SLACK_MS); // past the boot resend
    int closes = count('c', 0, 1000000);
    analog_channel_set(4); // a burst is going out when WiFi is switched on
    usleep(50000);
    g_setting.wifi.enable = true;
    t = t_now();
    uint64_t waited = timed_call_ms(wifi_changed);
    check(count('c', 0, 1000000) == closes + 1, "WiFi on during a burst: pins released before returning");
    check(find_from('a', t - 100, &e) && waited < 11 + 50, "  the burst cut short after its packet (< 60 ms)");
    printf("    (waited %llu ms)\n", (unsigned long long)waited);
    check(module_status() == MODULE_STATUS_WIFI, "status: paused while WiFi is on");
    check(!analog_channel_set(3) && g_setting.module.channel == 4 && !analog_channel_tunable(),
          "WiFi on: bay module not tunable, its stored channel kept");
    t = t_now();
    usleep(300000);
    check(count('h', t, 1000000) == 0 && count('i', t, 1000000) == 0, "nothing sent while WiFi is on");
    g_setting.wifi.enable = false;
    uint64_t back = t_now();
    wifi_changed();
    usleep(300000);
    check(count('i', back, 1000000) == 1, "WiFi off: module set up again (bay long powered: at once)");
    check(next_after('i', back, &e) && e.op == 'h' && e.arg == 3, "  on its stored channel (4)");

    g_setting.module.pins = false;
    t = t_now();
    module_settings_changed(false);
    usleep(300000);
    check(count('c', t, 1000000) == 1 && module_status() == MODULE_STATUS_PINS_OFF, "Pin Control Off: pins released");
    int writes = ini_writes;
    check(!analog_channel_set(20) && g_setting.module.channel == 4 && ini_writes == writes,
          "Pin Control Off: ELRS/dial cannot change the stored bay channel");

    printf("Switch (8 channels) and type round trips:\n");
    g_setting.module.pins = true;
    g_setting.module.type = MODULE_SWITCH;
    g_setting.module.channel = 37; // R5
    t = t_now();
    module_settings_changed(false);
    settle();
    check(g_setting.module.channel == 37, "type Switch: stored channel R5 (37) kept");
    check(analog_channel_count() == 8 && analog_channel_pos() == 5, "  shown as list entry 5 (CH5)");
    check(count('i', t, 1000000) == 1 && next_after('i', t, &e) && e.op == 'h' && e.arg == 4,
          "  switch init, then code 4 (CH5)");
    t = t_now();
    analog_channel_set(analog_channel_at(2)); // dial: CH2
    settle();
    check(g_setting.module.channel == 34 && find_from('h', t, &e) && e.arg == 1, "dial CH2: stored R2 (band kept), code 1");
    analog_channel_set(28); // ELRS VTX F4
    settle();
    check(g_setting.module.channel == 28 && analog_channel_pos() == 4, "ELRS F4: stored F4 (28), shown CH4");
    g_setting.module.setting = 1;
    t = t_now();
    module_settings_changed(true);
    settle();
    check(find_from('m', t, &e) && e.arg == 1 && next_after('m', t, &e) && e.op == 'h' && e.arg == 3,
          "pin order change: mode, then the channel code again");
    g_setting.module.type = MODULE_RAPIDFIRE;
    g_setting.module.setting = 0;
    t = t_now();
    module_settings_changed(false);
    settle();
    check(g_setting.module.channel == 28 && analog_channel_count() == 48 && analog_channel_pos() == 28,
          "type rapidFIRE: back on F4 (48 channels)");
    check(next_after('i', t, &e) && e.op == 'h' && e.arg == 27, "  init, then F4");
    g_setting.module.type = MODULE_SWITCH;
    module_settings_changed(false);
    settle();
    g_setting.module.type = MODULE_TRUED;
    module_settings_changed(false);
    settle();
    check(g_setting.module.channel == 28, "Switch -> True-D round trip: F4 kept");

    printf("Goggle 2 built-in receiver and Source > Analog Module:\n");
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_INTERNAL;
    check(analog_channel_count() == 48 && analog_channel_tunable() && analog_channel_get() == 12,
          "built-in receiver: 48 channels, its own channel (B4)");
    t = t_now();
    analog_channel_set(40);
    settle();
    check(g_setting.source.analog_channel == 40 && g_setting.module.channel == 28 && count('h', t, 1000000) == 0,
          "built-in receiver tuned: bay module and its channel untouched");
    g_setting.module.type = MODULE_SWITCH;
    module_settings_changed(false);
    settle();
    check(analog_channel_count() == 48 && analog_channel_get() == 40, "type Switch with Built-in selected: built-in list unchanged");
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_EXTERNAL;
    t = t_now();
    module_source_changed();
    settle();
    check(find_from('m', t, &e) && next_after('m', t, &e) && e.op == 'h' && e.arg == 3,
          "toggled to Expansion: mode and channel sent again (CH4 of F4)");
    check(analog_channel_get() == 28 && analog_channel_pos() == 4 && g_setting.source.analog_channel == 40,
          "  bay F4 shown as CH4, built-in channel kept (L8)");
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_INTERNAL;
    module_source_changed();
    check(analog_channel_get() == 40, "toggled back to Built-in: L8 again");

    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
