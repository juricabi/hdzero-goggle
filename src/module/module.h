#ifndef __MODULE_MODULE_H_
#define __MODULE_MODULE_H_

#include <stdbool.h>

// Module bay control (Goggle 1 and Goggle 2; nothing on BoxPro, it has no bay).
//
// The bay pins are SoC PG0-PG2, which are also the SDIO bus of the WiFi chip on
// both goggles, and they reach an analog module only through a wiring mod of the
// bay board (docs/module-bay.md).  The goggle drives them only while Module Bay >
// Pin Control is On, WiFi is off, a module type is set and the bay is powered.  A
// worker thread owns them, so none of the calls below waits for a transfer
// (except module_wifi_changed() before WiFi starts).

typedef enum {
    MODULE_UNKNOWN,
    MODULE_SWITCH,
    MODULE_RAPIDFIRE,
    MODULE_STEADYVIEW,
    MODULE_STEADYVIEW_X,
    MODULE_TRUED,
    MODULE_TYPES
} module_type_t;

typedef enum {
    MODULE_STATUS_NO_BAY,    // BoxPro
    MODULE_STATUS_NONE,      // type Unknown
    MODULE_STATUS_PINS_OFF,  // Pin Control Off
    MODULE_STATUS_WIFI,      // WiFi enabled: the pins are left to the SDIO bus
    MODULE_STATUS_SDIO,      // pins found in use by the SDIO host / WiFi driver
    MODULE_STATUS_UNPOWERED, // bay power off
    MODULE_STATUS_STARTING,  // waiting for the module to boot
    MODULE_STATUS_ACTIVE,
} module_status_t;

// Start-up: checks the settings, starts the worker.
void module_init();
// Module Bay page: type, mode or Pin Control changed (already in g_setting.module).
void module_settings_changed(bool mode_changed);
// Goggle 2, Source > Analog Module changed (already in g_setting.source): when the
// bay became the analog source its module gets its mode and channel again.
void module_source_changed();
// Bay power thread (batch 2 hardware): bay power switched.
void module_power_changed(int on);
// WiFi page: call after g_setting.wifi.enable changed, before WiFi is started
// (returns once the bay pins are released, at most a few seconds) and after it
// was stopped.
void module_wifi_changed();
module_status_t module_status();

// The configured module type (whatever the pins, power and source).
int module_num_channels();               // 0: Unknown
const char *module_mode_names(int type); // Mode list options, NULL: none
// The bay module's stored channel in the type's list ("CH5", "R5"), "" for Unknown.
const char *module_channel_name();

// The analog picture comes from the module bay: Goggle 1 always, Goggle 2 with
// Source > Analog Module = Expansion.
bool module_bay_is_source();
// A module type is set and the goggle may drive the bay pins.
bool module_bay_controlled();

// The channel of the analog receiver now selected (SOURCE_AV_MODULE): the built-in
// receiver (Goggle 2, BoxPro) or the bay module.  Each has its own stored channel,
// always one of the 48 standard channels (1..48 = A1..L8), never folded:
//   built-in receiver   [source] analog_channel (as upstream)
//   bay module          Goggle 1: [source] analog_channel (its only analog receiver)
//                       Goggle 2: [module] channel
// A bay type with fewer channels (Switch: 8, channels 1-8 of the module's own band)
// shows and sends the stored channel's place in its band, (ch - 1) % 8 + 1, and a
// choice in that list keeps the stored band; nothing is changed by switching types.
int analog_channel_get();                  // stored, 1..48
// Stores 1..48 (setting.ini too); the bay module follows.  Does nothing and returns
// false while the goggle does not tune the analog source (analog_channel_tunable()).
bool analog_channel_set(int channel);
int analog_channel_count();                // length of the list: 8 (Switch in the bay) or 48
int analog_channel_pos();                  // the stored channel's place in the list, 1..count
int analog_channel_at(int pos);            // the stored channel that selects list entry pos
const char *analog_channel_name(int pos);  // name of list entry pos (1-based)
const char *analog_channel_current_name(); // analog_channel_name(analog_channel_pos())
// The goggle can tune the analog source (and shows its channel): the built-in
// receiver always, the bay module only while it is controlled.
bool analog_channel_tunable();

#endif
