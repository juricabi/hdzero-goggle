#ifndef _MODULE_DEF_H
#define _MODULE_DEF_H

// One module bay type.  All functions run on the module worker thread (module.c),
// only while the goggle may drive the bay pins and the bay is powered; channel
// numbers are 0-based here.
typedef struct {
    int num_channels;
    char *(*name)(int channel);
    void (*init)();             // take the pins, set mode and the current channel
    void (*close)();            // release the pins (inputs)
    void (*set_channel)(int channel);
    void (*set_mode)();
    const char *mode_names;     // options of the Module Bay "Mode" list, NULL: none
    int boot_resend_ms;         // > 0: the channel is sent again this long after bay power-on
} module_def_t;

char *module_standard_channel_name(int channel);

#endif
