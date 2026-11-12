#include <unistd.h>

#include "core/common.hh"
#include "core/settings.h"

#include "rapidfire.h"
#include "softspi.h"

// ImmersionRC rapidFIRE SPI API on the bay pins (CS, DATA, CLK), with pkendall's
// timing from his analog-module-control branch (used on a Goggle 1).  The goggle
// only writes: nothing here reads a pin or depends on an answer, so a module that
// only listens (a True-D in its rapidFIRE mode) gets the same as a rapidFIRE.
//
//   2.0 s after bay power-on (the ExpressLRS backpack's VRX_BOOT_DELAY):
//   init      pins from input straight to all three high, 200 ms; all low, 200 ms
//             (the SPI mode entry, as the ExpressLRS backpack does it); then CS
//             high, CLK and DATA low (idle, kept while the module is controlled)
//   mode      100 ms later 'D' '=' 0x01 crc mode
//   channel   right after: 'B' '=' 0x01 crc band (IMRC numbering), 100 ms, 'C' '='
//             0x01 crc channel (1-8); crc = sum of bytes 0-3 with the value in byte
//             3, the value again in byte 4
//   again     5.0 s after bay power-on: band and channel once more (a module that
//             boots slower); channel changes in between wait for it
// Each command goes out 3 times, 10 ms apart: CS low, 16 us, 5 bytes MSB first
// (DATA set while CLK is low, CLK high >= 9 us, low >= 8 us, each plus one sysfs
// write), 16 us, CS high.  The receiver should sample DATA on the rising CLK edge
// while CS is low and should not rely on the SPI mode entry (it may boot after it).

#define SPAM_COUNT 3

#define RF_API_DIR_GRTHAN  '>'
#define RF_API_DIR_EQUAL   '='
#define RF_API_BEEP_CMD    'S'
#define RF_API_BAND_CMD    'B'
#define RF_API_CHANNEL_CMD 'C'
#define RF_API_MODE_CMD    'D'

#define SPAM_GAP_MS 10 // between the repeats of a command
#define CMD_GAP_MS  100 // between two commands

// Every wait is an abort point (module_sleep_ms()): a release request stops a
// transfer after the SPI frame on the wire.
static bool send_cmd(uint8_t command, uint8_t value);

static void rapidfire_set_channel(int index) {
    // convert standard band index (A B E F R L) to IMRC band index
    static const uint8_t band_map[] = {5, 4, 3, 1, 2, 6};
    const int band = index / 8;
    if (index < 0 || band >= (int)ARRAY_SIZE(band_map))
        return;
    if (!send_cmd(RF_API_BAND_CMD, band_map[band]) || !module_sleep_ms(CMD_GAP_MS))
        return; // pins to be released
    send_cmd(RF_API_CHANNEL_CMD, index % 8 + 1); // IMRC channel is 1-based
}

static void rapidfire_set_mode() {
    if (!module_sleep_ms(CMD_GAP_MS))
        return;
    send_cmd(RF_API_MODE_CMD, g_setting.module.setting);
    // the worker sends the channel next
}

static void rapidfire_init() {
    softspi_open();       // inputs
    softspi_drive_all(1); // straight to high, no low step before the entry
    if (!module_sleep_ms(200))
        return;
    softspi_set_pin(SOFTSPI_CLK, 0);
    softspi_set_pin(SOFTSPI_CS, 0);
    softspi_set_pin(SOFTSPI_DAT, 0);
    if (!module_sleep_ms(200))
        return;
    softspi_set_pin(SOFTSPI_CS, 1);

    rapidfire_set_mode();
}

static void rapidfire_close() {
    softspi_close();
}

// CRC function for IMRC rapidfire API
// Input: byte array, array length
// Output: crc byte
static uint8_t crc8(uint8_t *buf, uint8_t bufLen) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < bufLen; ++i) {
        sum += buf[i];
    }
    return sum & 0xFF;
}

static void send_spi(uint8_t *buf, uint8_t len) {
    softspi_set_pin(SOFTSPI_CS, 0);
    usleep(16);

    for (int i = 0; i < len; ++i) {
        uint8_t byte = buf[i];
        for (uint8_t k = 0; k < 8; k++) {
            softspi_set_pin(SOFTSPI_DAT, byte & 0x80);
            softspi_set_pin(SOFTSPI_CLK, 1);
            usleep(9);
            softspi_set_pin(SOFTSPI_CLK, 0);
            usleep(8);
            byte <<= 1;
        }
    }
    usleep(16);
    softspi_set_pin(SOFTSPI_CS, 1);
    softspi_set_pin(SOFTSPI_DAT, 0); // idle: CS high, CLK and DATA low
}

// One command, SPAM_COUNT times (the rapidFIRE sometimes misses a packet).  False
// when it was cut short because the pins are to be released.
static bool send_cmd(uint8_t command, uint8_t value) {
    uint8_t cmd[5];
    cmd[0] = command;
    cmd[1] = RF_API_DIR_EQUAL;
    cmd[2] = 0x01;         // len
    cmd[3] = value;        // temporarily set byte 4 to the value for crc calc
    cmd[3] = crc8(cmd, 4); // reset byte 4 to crc
    cmd[4] = value;        // assign the value to byte 5

    for (int i = 0; i < SPAM_COUNT; i++) {
        send_spi(cmd, 5);
        if (!module_sleep_ms(SPAM_GAP_MS))
            return false;
    }
    return true;
}

// 48 channels, A B E F R L: the analog channel setting, its range check at load,
// the internal receiver and the ELRS VTX channel are all 48 wide (pkendall's
// table also had the IMRC X band as channels 49-56, which nothing else can hold).
module_def_t rapidfire_module = {
    .num_channels = 48,
    .name = module_standard_channel_name,
    .init = rapidfire_init,
    .close = rapidfire_close,
    .set_channel = rapidfire_set_channel,
    .set_mode = rapidfire_set_mode,
    .mode_names = "legacy\nrapidFIRE 1\nrapidFIRE 2",
    .boot_ms = 2000,        // as the ExpressLRS backpack (VRX_BOOT_DELAY)
    .boot_resend_ms = 5000, // a module that boots slower (a True-D's boot screen ends ~3.5 s)
};
