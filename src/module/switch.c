#include <unistd.h>

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

static const softspi_pin_t *pin_order(void) {
    int order = g_setting.module.setting;
    if (order < 0 || order >= (int)ARRAY_SIZE(pin_orders))
        order = 0;
    return pin_orders[order];
}

static void switch_set_channel(int channel) {
    const softspi_pin_t *pins = pin_order();
    int code = channel % 8;
    for (int bit = 0; bit < 3; bit++)
        softspi_set_pin(pins[bit], (code >> bit) & 1);
}

static void switch_update() {
    // the pin order may have changed: all low, then the code in the new order
    softspi_drive_all(0);
    switch_set_channel(g_setting.source.analog_channel - 1);
}

static void switch_init() {
    softspi_init();
    // Set the swicthes/GPIO to a state that is NOT part of the rapidfire SPI setup.
    // (200 ms: shorter than the 300 ms a True-D in "1-8" mode needs to take a code)
    switch_set_channel(2);
    usleep(200000);
    switch_set_channel(g_setting.source.analog_channel - 1);
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
    .set_mode = switch_update,
    .mode_names = "CLK/DAT/CS\nDAT/CS/CLK\nCS/DAT/CLK",
    .boot_resend_ms = 0,
};
