#include "core/common.hh"
#include "core/settings.h"

#include "softspi.h"
#include "switch.h"

// Static 3-bit channel code on the bay pins, active high: code n = channel n+1 of
// the module's own band (Fat Shark style switch selection; also the "1-8" bay mode
// of the Furious True-D).  The Mode list picks which SoC pin carries which bit,
// because the bay wiring is not known (docs/module-bay.md):
//   0  CLK/DAT/CS  bit0 = CLK (PG0), bit1 = DAT (PG1), bit2 = CS (PG2) (as before)
//   1  DAT/CS/CLK  the rapidFIRE convention CH1 = DATA, CH2 = CS, CH3 = CLK
//   2  CS/DAT/CLK  CS1 = NSS, CS2 = DATA, CS3 = CLK (ExpressLRS rapidFIRE backpack wiring)
static const softspi_pin_t pin_orders[][3] = {
    {SOFTSPI_CLK, SOFTSPI_DAT, SOFTSPI_CS},
    {SOFTSPI_DAT, SOFTSPI_CS, SOFTSPI_CLK},
    {SOFTSPI_CS, SOFTSPI_DAT, SOFTSPI_CLK},
};

// At init the pins go from inputs straight to code 2 for 200 ms before the channel
// code: a state that is not part of the rapidFIRE SPI set-up (all three high,
// then all low within 100-400 ms), so a rapidFIRE in legacy mode stays in it.
#define SWITCH_PRE_CODE 2
#define SWITCH_PRE_MS   200

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
}

static void switch_set_channel(int channel) {
    drive_code(channel % 8);
}

static void switch_set_mode() {
    // new pin order: the worker sends the channel code next, in that order
}

static void switch_init() {
    softspi_open(); // inputs
    drive_code(SWITCH_PRE_CODE);
    module_sleep_ms(SWITCH_PRE_MS);
    // the worker sends the channel next
}

static void switch_close() {
    softspi_close();
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
    .mode_names = "CLK/DAT/CS\nDAT/CS/CLK\nCS/DAT/CLK",
    .boot_ms = 0,
    .boot_resend_ms = 0,
};
