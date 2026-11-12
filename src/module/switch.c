#include <stdint.h>
#include <time.h>

#include "core/common.hh"
#include "core/settings.h"

#include "softspi.h"
#include "switch.h"

// Static 3-bit channel code on the bay pins, active high: code n = channel n+1 of
// the module's own band (Fat Shark style switch selection; also the "1-8" bay mode
// of the Furious True-D).  The Mode list picks which SoC pin carries which bit,
// because the bay wiring is not known (docs/module-bay.md): all six orders.  A
// label gives the weight of each pin's bit, "PG0=4 PG1=1 PG2=2": PG1 is bit 0,
// PG2 bit 1, PG0 bit 2.  The first three are the orders offered before, so their
// setting.ini values keep their meaning.
static const softspi_pin_t pin_orders[][3] = {
    // bit 0       bit 1        bit 2
    {SOFTSPI_CLK, SOFTSPI_DAT, SOFTSPI_CS},  // PG0=1 PG1=2 PG2=4 (pkendall's order)
    {SOFTSPI_DAT, SOFTSPI_CS, SOFTSPI_CLK},  // PG0=4 PG1=1 PG2=2 (rapidFIRE convention CH1 = DATA, CH2 = CS, CH3 = CLK)
    {SOFTSPI_CS, SOFTSPI_DAT, SOFTSPI_CLK},  // PG0=4 PG1=2 PG2=1 (ELRS rapidFIRE backpack: CS1 = NSS, CS2 = DATA, CS3 = CLK)
    {SOFTSPI_CLK, SOFTSPI_CS, SOFTSPI_DAT},  // PG0=1 PG1=4 PG2=2
    {SOFTSPI_DAT, SOFTSPI_CLK, SOFTSPI_CS},  // PG0=2 PG1=1 PG2=4
    {SOFTSPI_CS, SOFTSPI_CLK, SOFTSPI_DAT},  // PG0=2 PG1=4 PG2=1
};
#define SWITCH_MODE_NAMES "PG0=1 PG1=2 PG2=4\nPG0=4 PG1=1 PG2=2\nPG0=4 PG1=2 PG2=1\n" \
                          "PG0=1 PG1=4 PG2=2\nPG0=2 PG1=1 PG2=4\nPG0=2 PG1=4 PG2=1"

// At init the pins go from inputs straight to code 2 for 200 ms before the channel
// code: a state that is not part of the rapidFIRE SPI set-up (all three high,
// then all low within 100-400 ms), so a rapidFIRE in legacy mode stays in it.
#define SWITCH_PRE_CODE 2
#define SWITCH_PRE_MS   200

// A True-D in its "1-8" bay mode takes a code once it has read the same for
// 300 ms, and ignores code 7 (all high: what an unconnected bay reads) until it
// has taken another code since its power-up.  It reads its pins only once its loop
// runs: after its bootloader (~1 s), set-up and 2 s boot screen, so it takes its
// first code ~3.5-3.9 s after power-on.  So channel 8 (code 7) is preceded by
// code 6 (channel 7) until a True-D has surely taken that: SWITCH_TAKE_MS on the
// pins, and at least until SWITCH_READY_MS after bay power-on.  Other modules just
// show channel 7 that long first; codes 0-6 go out at once.
#define SWITCH_TAKE_MS  500
#define SWITCH_READY_MS 5000

static int code_now = -1;   // code on the pins, -1: none yet
static uint64_t code_since; // when it was put there (ms, CLOCK_MONOTONIC)
static bool other_taken;    // a code other than 7 stood long enough to be taken

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const softspi_pin_t *pin_order(void) {
    int order = g_setting.module.setting;
    if (order < 0 || order >= (int)ARRAY_SIZE(pin_orders))
        order = 0;
    return pin_orders[order];
}

// Each pin straight to its level (also from input), no intermediate all-low state.
static void drive_code(int code) {
    const softspi_pin_t *pins = pin_order();
    for (int bit = 0; bit < 3; bit++)
        softspi_drive_pin(pins[bit], (code >> bit) & 1);
    if (code != code_now) {
        code_now = code;
        code_since = mono_ms();
    }
}

// ms until the code on the pins has surely been taken by a True-D, 0: it has.
static unsigned until_taken(void) {
    const uint64_t now = mono_ms();
    const unsigned on = module_ms_since_power_on();
    unsigned wait = 0;
    if (now < code_since + SWITCH_TAKE_MS)
        wait = (unsigned)(code_since + SWITCH_TAKE_MS - now);
    if (on < SWITCH_READY_MS && SWITCH_READY_MS - on > wait)
        wait = SWITCH_READY_MS - on;
    return wait;
}

static void switch_set_channel(int channel) {
    const int code = channel % 8;
    // the code now on the pins (not 7) has stood long enough to be taken
    if (!other_taken && code_now >= 0 && code_now != 7 && until_taken() == 0)
        other_taken = true;
    if (code == 7 && !other_taken) {
        if (code_now != 6)
            drive_code(6);
        if (!module_sleep_ms(until_taken()))
            return; // pins to be released
        other_taken = true;
    }
    drive_code(code);
}

static void switch_set_mode() {
    // new pin order: the worker sends the channel code next, in that order; what
    // the module took before was read in the old one
    code_now = -1;
    other_taken = false;
}

static void switch_init() {
    code_now = -1;
    other_taken = false;
    softspi_open(); // inputs
    drive_code(SWITCH_PRE_CODE);
    module_sleep_ms(SWITCH_PRE_MS);
    // the worker sends the channel next
}

static void switch_close() {
    softspi_close();
    code_now = -1;
}

static char *switch_name(int channel) {
    static char *channel_name[] = {
        "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8",
    };
    if (channel < 0 || channel >= (int)ARRAY_SIZE(channel_name))
        return "";
    return channel_name[channel];
}

module_def_t switch_module = {
    .num_channels = 8,
    .name = switch_name,
    .init = switch_init,
    .close = switch_close,
    .set_channel = switch_set_channel,
    .set_mode = switch_set_mode,
    .mode_names = SWITCH_MODE_NAMES,
    .boot_ms = 0,
    .boot_resend_ms = 0,
};
