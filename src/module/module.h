#ifndef __MODULE_MODULE_H_
#define __MODULE_MODULE_H_

#include <stdbool.h>

// Module bay control (Goggle 1 and Goggle 2; nothing on BoxPro, it has no bay).
//
// The bay pins are SoC PG0-PG2, which are also the SDIO bus of the WiFi chip on
// both goggles (docs/module-bay.md).  The goggle drives them only while Module Bay
// > Pin Control is On, WiFi is off and the bay is powered.  A worker thread owns
// them, so none of the calls below waits for a transfer (except
// module_wifi_changed() before WiFi starts).

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
// Dial / ELRS: the analog channel (1-based) changed; it reaches the bay module when
// the bay is the analog source.
void module_set_channel(int channel);
// Bay power thread (batch 2 hardware): bay power switched.
void module_power_changed(int on);
// WiFi page: call after g_setting.wifi.enable changed, before WiFi is started
// (returns once the bay pins are released, at most a few seconds) and after it
// was stopped.
void module_wifi_changed();
module_status_t module_status();

// The configured module type (whatever the pins, power and source).
int module_num_channels();               // 0: Unknown
char *module_channel_str(int channel);   // 1-based
const char *module_mode_names(int type); // Mode list options, NULL: none

// The analog picture comes from the module bay: Goggle 1 always, Goggle 2 with
// Source > Analog Module = Expansion.
bool module_bay_is_source();
// A module type is set and the goggle may drive the bay pins.
bool module_bay_controlled();

// The channel list of SOURCE_AV_MODULE: the bay module's when the bay is the
// source and a type is set, the 48 standard channels otherwise (1-based).
int analog_channel_count();
const char *analog_channel_name(int channel);
// The goggle can tune the analog source (and shows its channel).
bool analog_channel_tunable();
// channel brought into 1..analog_channel_count(): (channel - 1) % count + 1
int analog_channel_fit(int channel);
// The same applied to g_setting.source.analog_channel (stored if it changed).
void analog_channel_clamp();

#endif
