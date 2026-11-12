#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include <log/log.h>

#include "core/common.hh"
#include "core/settings.h"

#include "module.h"
#include "rapidfire.h"
#include "steadyview.h"
#include "steadyview_x.h"
#include "switch.h"

static module_def_t *modules[] = {
    NULL, // unknown/uncontrolled
    &switch_module,
    &rapidfire_module,
    &steadyview_module,
    &steadyview_x_module
};

static module_def_t *module = NULL;

// Bay power as last switched by the power thread.  Starts at 1: batch 1 hardware
// has no bay power switch (always powered), and on batch 2 the power thread
// reports the real state right after start-up.
static int bay_powered = 1;

// The bay pins are driven from the input thread (dial), the ELRS thread (VTX
// channel from the radio), the menu and the bay power thread: one bit-banged
// transfer must not interleave with another.
static pthread_mutex_t module_mutex = PTHREAD_MUTEX_INITIALIZER;

// Time a module gets to boot after the goggle switches the bay power on,
// before its mode and channel are sent again.
#define MODULE_BOOT_MS 1000

static module_def_t *module_lookup(void) {
    int type = g_setting.module.type;
    if (type < 0 || type >= (int)ARRAY_SIZE(modules)) {
        LOGE("module: bad type %d, left uncontrolled", type);
        return NULL;
    }
    return modules[type];
}

// True when the analog picture comes from the module bay, so the bay module is
// the one to tune.  Goggle 2 also has an internal receiver, chosen in
// Source > Analog Module.
static bool module_bay_selected(void) {
#if defined(HDZGOGGLE2)
    return g_setting.source.analog_module == SETTING_SOURCES_ANALOG_MODULE_EXTERNAL;
#else
    return true;
#endif
}

// While the bay is unpowered init/mode wait for the power-on (module_power_changed).
void module_init() {
    pthread_mutex_lock(&module_mutex);
    module = module_lookup();
    if (module && bay_powered) {
        module->init();
    }
    pthread_mutex_unlock(&module_mutex);
}

void module_set_mode() {
    pthread_mutex_lock(&module_mutex);
    module = module_lookup();
    if (module && bay_powered) {
        module->set_mode();
    }
    pthread_mutex_unlock(&module_mutex);
}

void module_close() {
    pthread_mutex_lock(&module_mutex);
    if (module)
        module->close();
    pthread_mutex_unlock(&module_mutex);
}

void module_set_channel(int channel) {
    if (channel < 1 || channel > 48)
        return;
    if (!module_bay_selected())
        return;
    pthread_mutex_lock(&module_mutex);
    if (module && bay_powered) // (unpowered: sent by the power-on init)
        module->set_channel(channel - 1);
    pthread_mutex_unlock(&module_mutex);
}

// Called by the bay power thread (batch 2 hardware) when it switches the bay power.
// Off: the bay pins are released (inputs), so they do not feed the unpowered
// module.  On: the module missed every mode/channel sent meanwhile (and at boot
// the bay is only powered after module_init() ran), so once it has booted it is
// set up again with the current channel.
void module_power_changed(int on) {
    pthread_mutex_lock(&module_mutex);
    bay_powered = on;
    if (module) {
        if (!on) {
            LOGI("module: bay unpowered, pins released");
            module->close();
        } else {
            usleep(MODULE_BOOT_MS * 1000);
            LOGI("module: bay powered, init with channel %d", g_setting.source.analog_channel);
            module->init(); // pins, init sequence, mode and the current channel
            if (module_bay_selected())
                module->set_channel(g_setting.source.analog_channel - 1);
        }
    }
    pthread_mutex_unlock(&module_mutex);
}

int module_num_channels() {
    if (module)
        return module->num_channels;
    return 0;
}

char *module_channel_str(int channel) {
    if (module)
        return module->name(channel - 1);
    return "";
}
