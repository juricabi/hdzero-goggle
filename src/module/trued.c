// Furious True-D in its "LINK" bay mode: channel commands at 9600 8N1 on one bay
// pin (the True-D's D12; it never answers, all its bay pins are inputs with
// ~35 k pull-ups to 5 V).  Which SoC pin reaches D12 is not known, so every
// command goes out on all three bay pins at once with the same waveform.
//
// The pins are outputs only for a burst (5 packets, ~170 ms) and inputs
// otherwise: idle high comes from the True-D's pull-ups, and a pin that meets a
// True-D output (D13 is driven in its AUTO mode) is not fought for longer than one
// burst.  A release request (bay off, Pin Control Off, WiFi on, type change) ends
// a burst after the packet on the wire (<= ~11 ms).
//
// Timing: each edge has an absolute CLOCK_MONOTONIC deadline from the packet's
// first start bit (bitbang_uart.c), busy-waited with the thread raised to
// SCHED_FIFO for the ~10.4 ms of one packet and put back afterwards; the 30 ms
// gaps between packets are plain (abortable) sleeps.
//
// Boot: the True-D shows a 2 s boot screen after power-on and buffers at most 64
// bytes meanwhile.  module.c sends the channel right after init (one burst = 50
// bytes) MODULE_BOOT_MS after bay power-on and again boot_resend_ms after
// power-on, and holds any channel change in between back for that resend, so no
// more than 50 bytes reach the True-D before its loop runs.

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>

#include "core/common.hh"
#include "core/settings.h"

#include "bitbang_uart.h"
#include "softspi.h"
#include "trued.h"
#include "trued_proto.h"

#define TRUED_REPEATS      5
#define TRUED_GAP_MS       30
#define TRUED_RT_PRIORITY  80
#define TRUED_IDLE_US      2000 // pins high before the first start bit

static bool pins_ok = false;

static uint64_t trued_now_ns(void *ctx) {
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void trued_write_line(void *ctx, int line, int level) {
    (void)ctx;
    softspi_set_pin((softspi_pin_t)line, level);
}

// One packet with the calling thread at SCHED_FIFO, restored afterwards.
static void trued_send_packet(const uint8_t *frame, size_t len) {
    const bitbang_uart_io_t io = {
        .ctx = NULL,
        .now_ns = trued_now_ns,
        .write_line = trued_write_line,
        .lines = SOFTSPI_PINS,
    };
    struct sched_param old_param, rt_param = {.sched_priority = TRUED_RT_PRIORITY};
    int old_policy;
    bool rt = false;

    if (pthread_getschedparam(pthread_self(), &old_policy, &old_param) == 0)
        rt = pthread_setschedparam(pthread_self(), SCHED_FIFO, &rt_param) == 0;

    uint64_t late = bitbang_uart_send(&io, TRUED_BAUD, frame, len);

    if (rt)
        pthread_setschedparam(pthread_self(), old_policy, &old_param);

    if (!rt || late > 30000)
        LOGW("trued: packet sent %s, worst edge %llu us late", rt ? "at SCHED_FIFO" : "without SCHED_FIFO",
             (unsigned long long)(late / 1000));
}

static void trued_set_channel(int index) {
    uint8_t frame[TRUED_CMD_LEN];

    if (index < 0 || index >= TRUED_NUM_CHANNELS)
        return;
    trued_build_channel_cmd(index, frame);
    LOGI("trued: channel %d (%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X)%s", index, frame[0], frame[1],
         frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9], pins_ok ? "" : ", no pins");
    if (!pins_ok)
        return;

    softspi_drive_all(1);
    usleep(TRUED_IDLE_US);
    for (int i = 0; i < TRUED_REPEATS; i++) {
        if (i && !module_sleep_ms(TRUED_GAP_MS))
            break; // pins to be released
        trued_send_packet(frame, sizeof(frame));
    }
    softspi_float_all();
}

static void trued_set_mode() {
    // no modes (the worker sends the channel after this)
}

static void trued_init() {
    pins_ok = softspi_open(); // inputs until the first burst, which the worker sends next
}

static void trued_close() {
    softspi_close();
    pins_ok = false;
}

module_def_t trued_module = {
    .num_channels = TRUED_NUM_CHANNELS,
    .name = module_standard_channel_name,
    .init = trued_init,
    .close = trued_close,
    .set_channel = trued_set_channel,
    .set_mode = trued_set_mode,
    .mode_names = NULL,
    .boot_ms = 0,
    .boot_resend_ms = 3500,
};
