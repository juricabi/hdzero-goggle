// Host-side test of the real Switch, rapidFIRE and True-D types (src/module/
// switch.c, rapidfire.c, trued.c) driven by the real worker (module.c, built as
// for the Goggle 2 emulator), with the bay pins replaced by a recording model: an
// input reads high (the module's pull-ups), an output its level.  Real time,
// about 25 s.
//
// Switch: the code sequence at power-up, and a model of a True-D in its "1-8" bay
//   mode (loop from 3.1-3.9 s after power-on, a code taken after 300 ms the same,
//   7 only after another code) ends on the stored channel, channel 8 included.
// rapidFIRE: a listener decodes the SPI frames (CS low, DATA on rising CLK) and
//   checks the SPI mode entry, the set-up commands, the 5 s resend and their
//   timing; the trace's timing is printed for docs/module-bay.md.
// True-D: the 9600-baud bursts (count and time) after power-on.
// All: the pins are inputs again soon after power-off, also in mid-transfer.
//
//   make -C utilities/module_bay_test test

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include "core/common.hh"
#include "core/settings.h"
#include "module/module.h"
#include "module/module_def.h"
#include "module/softspi.h"

#define SLACK_MS 300
#define EARLY_US 2000 // the worker counts whole milliseconds from the power-on report

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

static void nop_init() {}
static void nop_channel(int ch) { (void)ch; }
module_def_t steadyview_module = {.num_channels = 48, .name = module_standard_channel_name, .init = nop_init,
                                  .close = nop_init, .set_channel = nop_channel, .set_mode = nop_init};
module_def_t steadyview_x_module = {.num_channels = 48, .name = module_standard_channel_name, .init = nop_init,
                                    .close = nop_init, .set_channel = nop_channel, .set_mode = nop_init};

// ---- bay pin model ----

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

#define MAX_EDGES 200000
typedef struct {
    uint64_t t;   // us since t0
    uint8_t line; // effective levels of the three lines after the change, bit i = pin i
    uint8_t out;  // which pins are outputs
} edge_t;

static pthread_mutex_t pin_lock = PTHREAD_MUTEX_INITIALIZER;
static edge_t edges[MAX_EDGES];
static int n_edges;
static uint64_t t0;
static int pin_out[SOFTSPI_PINS];   // output?
static int pin_level[SOFTSPI_PINS]; // output level
static bool pins_open;

static uint8_t lines_now(void) {
    uint8_t v = 0;
    for (int i = 0; i < SOFTSPI_PINS; i++)
        v |= (pin_out[i] ? pin_level[i] : 1) << i;
    return v;
}

static uint8_t outs_now(void) {
    uint8_t v = 0;
    for (int i = 0; i < SOFTSPI_PINS; i++)
        v |= pin_out[i] << i;
    return v;
}

static void pins_changed(void) {
    const uint8_t l = lines_now(), o = outs_now();
    if (n_edges && edges[n_edges - 1].line == l && edges[n_edges - 1].out == o)
        return;
    if (n_edges < MAX_EDGES)
        edges[n_edges++] = (edge_t){now_us() - t0, l, o};
}

static void set(int pin, int out, int level) {
    pthread_mutex_lock(&pin_lock);
    pin_out[pin] = out;
    if (out)
        pin_level[pin] = level;
    pins_changed();
    pthread_mutex_unlock(&pin_lock);
}

bool softspi_open() {
    pins_open = true;
    return true;
}
void softspi_init() {
    softspi_open();
    softspi_drive_all(0);
}
void softspi_drive_all(int level) {
    for (int i = 0; i < SOFTSPI_PINS; i++)
        set(i, 1, level);
}
void softspi_drive_pin(softspi_pin_t pin, int level) { set(pin, 1, level); }
void softspi_float_all() {
    for (int i = 0; i < SOFTSPI_PINS; i++)
        set(i, 0, 0);
}
void softspi_close() {
    softspi_float_all();
    pins_open = false;
}
void softspi_set_direction(softspi_direction_t direction) { set(SOFTSPI_DAT, direction == SOFTSPI_OUTPUT, 0); }
void softspi_release_gpio(int gpio) { (void)gpio; }
void softspi_set_pin(softspi_pin_t pin, int value) {
    pthread_mutex_lock(&pin_lock);
    pin_level[pin] = !!value; // an input keeps reading its pull-up
    pins_changed();
    pthread_mutex_unlock(&pin_lock);
}
int softspi_get_pin(softspi_pin_t pin) {
    pthread_mutex_lock(&pin_lock);
    const int v = (lines_now() >> pin) & 1;
    pthread_mutex_unlock(&pin_lock);
    return v;
}

static void trace_reset(void) {
    pthread_mutex_lock(&pin_lock);
    t0 = now_us();
    n_edges = 0;
    pins_changed();
    pthread_mutex_unlock(&pin_lock);
}

// ---- test helpers ----

static int failures;

static void check(int ok, const char *what) {
    printf("  %-76s %s\n", what, ok ? "ok" : "FAIL");
    failures += !ok;
}

static void sleep_ms(unsigned ms) { usleep(ms * 1000); }

static uint64_t t_ms(void) { return (now_us() - t0) / 1000; }

static void sleep_until_ms(uint64_t t) {
    while (t_ms() < t)
        usleep(2000);
}

// the three line levels at time t (us)
static uint8_t lines_at(uint64_t t) {
    uint8_t v = 7;
    for (int i = 0; i < n_edges && edges[i].t <= t; i++)
        v = edges[i].line;
    return v;
}

// time (us) from which all three pins are inputs for good, 0: they never are
static uint64_t floating_since(void) {
    for (int i = n_edges - 1; i >= 0; i--)
        if (edges[i].out)
            return i + 1 < n_edges ? edges[i + 1].t : 0;
    return edges[0].t;
}

// ---- True-D "1-8" model (MOD 1.16 bayPoll()), code bit i = line pin_of_bit[i] ----

static int trued18_channel(const softspi_pin_t pin_of_bit[3], uint64_t loop_start_ms, uint64_t end_ms, int start_channel,
                           int *taken) {
    int applied = -1, cand = -1, ch = start_channel;
    uint64_t cand_t = 0;
    *taken = 0;
    for (uint64_t t = loop_start_ms; t <= end_ms; t += 20) { // one loop pass every 20 ms
        const uint8_t l = lines_at(t * 1000);
        int n = 0;
        for (int b = 0; b < 3; b++)
            n |= ((l >> pin_of_bit[b]) & 1) << b;
        if (n != cand) {
            cand = n;
            cand_t = t;
        } else if (t - cand_t >= 300 && n != applied && (n != 7 || applied != -1)) {
            applied = n;
            ch = n; // channel n + 1 of its band
            (*taken)++;
        }
    }
    return ch;
}

// ---- rapidFIRE listener ----

typedef struct {
    uint64_t start, end; // us: CS low, CS high
    int bits;
    uint8_t b[8];
} frame_t;

static int decode_spi(frame_t *frames, int max, uint64_t from_us) {
    int n = 0, in = 0;
    uint8_t prev = 7;
    frame_t f;
    for (int i = 0; i < n_edges; i++) {
        const uint8_t l = edges[i].line;
        const int cs = (l >> SOFTSPI_CS) & 1, clk = (l >> SOFTSPI_CLK) & 1, dat = (l >> SOFTSPI_DAT) & 1;
        const int pcs = (prev >> SOFTSPI_CS) & 1, pclk = (prev >> SOFTSPI_CLK) & 1;
        if (edges[i].t >= from_us) {
            if (!in && pcs && !cs) {
                in = 1;
                memset(&f, 0, sizeof(f));
                f.start = edges[i].t;
            } else if (in && !cs && clk && !pclk) {
                if (f.bits < 64)
                    f.b[f.bits / 8] |= dat << (7 - f.bits % 8);
                f.bits++;
            } else if (in && cs) {
                in = 0;
                f.end = edges[i].t;
                if (f.bits && n < max) // (the all-low step of the entry clocks nothing)
                    frames[n++] = f;
            }
        }
        prev = l;
    }
    return n;
}

static int frame_ok(const frame_t *f, char cmd, int value) {
    const uint8_t crc = (uint8_t)(f->b[0] + f->b[1] + f->b[2] + f->b[4]);
    return f->bits == 40 && f->b[0] == (uint8_t)cmd && f->b[1] == '=' && f->b[2] == 1 && f->b[3] == crc &&
           f->b[4] == value;
}

// SPI mode entry: all three driven high, then all low, each >= 190 ms (the pins
// change one after the other); returns its start (us), 0: none
static uint64_t find_entry(uint64_t *low_at, uint64_t *low_end) {
    int i = 0;
    while (i < n_edges && !(edges[i].line == 7 && edges[i].out == 7))
        i++;
    if (i == n_edges)
        return 0;
    const uint64_t high = edges[i].t;
    while (i < n_edges && edges[i].line != 0)
        i++;
    if (i == n_edges)
        return 0;
    *low_at = edges[i].t;
    while (i < n_edges && edges[i].line == 0)
        i++;
    if (i == n_edges)
        return 0;
    *low_end = edges[i].t;
    return *low_at - high >= 190000 && *low_end - *low_at >= 190000 ? high : 0;
}

static void power(int on) { module_power_changed(on); }

static void select_type(int type, int setting) {
    g_setting.module.type = type;
    g_setting.module.setting = setting;
    module_settings_changed(false);
}

int main(void) {
    char what[160];
    g_setting.module.type = MODULE_UNKNOWN;
    g_setting.module.pins = true;
    g_setting.wifi.enable = false;
    g_setting.source.analog_module = SETTING_SOURCES_ANALOG_MODULE_EXTERNAL;
    g_setting.source.analog_channel = 1;
    g_setting.module.channel = 8; // A8
    module_init();
    static const softspi_pin_t order0[3] = {SOFTSPI_CLK, SOFTSPI_DAT, SOFTSPI_CS}; // pin order 0: bit i on PG i

    // ---- Switch ----
    printf("Switch, channel 8 (A8) right after bay power-on:\n");
    select_type(MODULE_SWITCH, 0);
    trace_reset();
    power(1);
    sleep_until_ms(6500);
    check(lines_at(999000) == 7 && (edges[1].t >= 1000000 - EARLY_US), "inputs (read 7) until the set-up 1.0 s after power-on");
    check(lines_at(1100000) == 2, "code 2 for 200 ms at init");
    check(lines_at(1300000) == 6 && lines_at(4900000) == 6, "then code 6 (channel 7) until 5 s after power-on");
    check(lines_at(5100000 + SLACK_MS * 1000) == 7, "then code 7 (channel 8)");
    for (int loop = 3100; loop <= 3900; loop += 400) {
        int taken;
        const int ch = trued18_channel(order0, loop, 6500, 3, &taken);
        snprintf(what, sizeof(what), "True-D 1-8 model, loop from %.1f s: ends on channel 8 (%d codes taken)", loop / 1000.0,
                 taken);
        check(ch == 7, what);
    }
    power(0);
    sleep_ms(100);
    check(floating_since() && floating_since() < 6500000 + 100000, "power-off: pins inputs again");

    printf("Switch, channel 4 (A4), pin order PG0=4 PG1=1 PG2=2:\n");
    g_setting.module.channel = 4;
    select_type(MODULE_SWITCH, 1);
    trace_reset();
    power(1);
    sleep_until_ms(4500);
    static const softspi_pin_t order1[3] = {SOFTSPI_DAT, SOFTSPI_CS, SOFTSPI_CLK};
    int taken;
    check(lines_at(1100000) == (1 << SOFTSPI_CS), "init: code 2 = bit 1 = PG2 high only");
    check(lines_at(1300000) == ((1 << SOFTSPI_DAT) | (1 << SOFTSPI_CS)) && trued18_channel(order1, 3500, 4500, 0, &taken) == 3,
          "code 3 (PG1, PG2) at once after it, taken by the model: channel 4");
    int low7 = 0; // rapidFIRE legacy guard: never all high -> all low
    for (int i = 1; i < n_edges; i++)
        low7 |= edges[i - 1].line == 7 && edges[i].line == 0;
    check(!low7, "never straight from all high to all low (rapidFIRE SPI entry)");
    power(0);
    sleep_ms(100);

    // ---- rapidFIRE ----
    printf("rapidFIRE, R1 (33), mode rapidFIRE 1:\n");
    g_setting.module.channel = 33;
    select_type(MODULE_RAPIDFIRE, 1);
    trace_reset();
    power(1);
    sleep_until_ms(5000 + 1000);
    uint64_t low_at = 0, low_end = 0;
    const uint64_t entry = find_entry(&low_at, &low_end);
    check(entry >= 2000000 - EARLY_US && entry < (2000 + SLACK_MS) * 1000ULL, "SPI mode entry 2.0 s after power-on (all high 200 ms, all low 200 ms)");
    int high_first = 1;
    for (int i = 0; i < n_edges && edges[i].t <= entry; i++)
        high_first &= edges[i].line == 7;
    check(high_first, "  pins inputs before it, straight to all high");
    static frame_t fr[64];
    const int nf = decode_spi(fr, 64, 0);
    int ok = nf == 15;
    const char cmds[15] = {'D', 'D', 'D', 'B', 'B', 'B', 'C', 'C', 'C', 'B', 'B', 'B', 'C', 'C', 'C'};
    const int vals[15] = {1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1}; // IMRC band 2 = RaceBand, channel 1
    for (int i = 0; ok && i < 15; i++)
        ok = frame_ok(&fr[i], cmds[i], vals[i]);
    snprintf(what, sizeof(what), "frames: D=1 x3, B=2 x3, C=1 x3 at set-up, B, C again (decoded %d)", nf);
    check(ok, what);
    if (nf == 15) {
        check(fr[0].start >= low_end + 100000 - EARLY_US && fr[0].start < low_end + (100 + SLACK_MS) * 1000ULL, "  mode 100 ms after the entry");
        check(fr[9].start >= 5000000 - EARLY_US && fr[9].start < (5000 + SLACK_MS) * 1000ULL, "  resend at 5.0 s after power-on");
        check(fr[6].start - fr[5].end >= 100000 - EARLY_US, "  100 ms between band and channel");
        check(fr[1].start - fr[0].end >= 10000 - EARLY_US, "  10 ms between repeats");
        printf("    trace (host, no sysfs cost): entry %.3f s, low %.3f-%.3f s, D %.3f s, B %.3f s, C %.3f s, "
               "resend B %.3f s C %.3f s, frame %llu us\n",
               entry / 1e6, low_at / 1e6, low_end / 1e6, fr[0].start / 1e6, fr[3].start / 1e6, fr[6].start / 1e6,
               fr[9].start / 1e6, fr[12].start / 1e6, (unsigned long long)(fr[0].end - fr[0].start));
    }
    check(lines_at(5900000) == (1 << SOFTSPI_CS), "idle between commands: CS high, CLK and DATA low");

    printf("rapidFIRE, channel change, then power-off in mid-command:\n");
    uint64_t t = now_us() - t0;
    analog_channel_set(28); // F4: IMRC band 1, channel 4
    sleep_ms(400);
    frame_t fr2[16];
    const int nf2 = decode_spi(fr2, 16, t);
    check(nf2 == 6 && frame_ok(&fr2[0], 'B', 1) && frame_ok(&fr2[5], 'C', 4), "F4: B=1 x3, C=4 x3");
    analog_channel_set(40); // L8
    sleep_ms(50);
    t = now_us() - t0;
    power(0);
    sleep_ms(100);
    const uint64_t fl = floating_since();
    if (getenv("TRACE"))
        for (int i = n_edges > 12 ? n_edges - 12 : 0; i < n_edges; i++)
            printf("    edge %d t=%llu line=%d out=%d (t_off=%llu)\n", i, (unsigned long long)edges[i].t, edges[i].line, edges[i].out, (unsigned long long)t);
    snprintf(what, sizeof(what), "pins inputs %.1f ms after the power-off report (< 30 ms)", fl > t ? (fl - t) / 1000.0 : 0.0);
    check(fl && fl < t + 30000, what);

    // ---- True-D ----
    printf("True-D (LINK), power-on:\n");
    g_setting.module.channel = 33;
    select_type(MODULE_TRUED, 0);
    trace_reset();
    power(1);
    sleep_until_ms(4200);
    if (getenv("TRACE"))
        for (int i = 0; i < n_edges && i < 12; i++)
            printf("    edge %d t=%llu line=%d out=%d\n", i, (unsigned long long)edges[i].t, edges[i].line, edges[i].out);
    int bursts = 0;
    uint64_t burst_t[8];
    for (int i = 1; i < n_edges; i++)
        if (edges[i].out && !edges[i - 1].out && bursts < 8)
            burst_t[bursts++] = edges[i].t;
    check(bursts == 2 && burst_t[0] >= 1000000 - EARLY_US && burst_t[0] < (1000 + SLACK_MS) * 1000ULL &&
              burst_t[1] >= 3500000 - EARLY_US,
          "a burst 1.0 s after power-on and the resend at 3.5 s, pins inputs in between");
    check(lines_at(2500000) == 7 && !(edges[n_edges - 1].out), "  inputs outside the bursts");
    power(0);
    sleep_ms(50);

    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
