#ifndef __MODULE_BITBANG_UART_H_
#define __MODULE_BITBANG_UART_H_

// Transmit-only software UART (8N1, LSB first) for the module bay pins.
//
// Plain C with no firmware dependencies: the clock and the pin writes come in
// through bitbang_uart_io_t, so the same code runs on the goggle (sysfs GPIO,
// CLOCK_MONOTONIC) and in the host-side test (simulated clock and pins, see
// utilities/module_bay_test).

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *ctx;
    // monotonic time in ns
    uint64_t (*now_ns)(void *ctx);
    // drive one line low (0) or high (1)
    void (*write_line)(void *ctx, int line, int level);
    // number of lines; every line gets the same waveform, written in line order
    int lines;
} bitbang_uart_io_t;

// Sends len bytes at baud on all lines.  The lines must be idle high before the
// call and are left high (stop bit) after a full stop-bit time.
//
// Every edge has an absolute deadline counted from the first start bit
// (t0 + n * 1e9 / baud), reached by busy-waiting on now_ns(): a write that comes
// late shifts only that edge and the error does not add up over the packet.  Only
// level changes are written.
//
// Returns the largest lateness of an edge in ns (time between its deadline and
// the start of its first line write), for diagnostics.
uint64_t bitbang_uart_send(const bitbang_uart_io_t *io, uint32_t baud, const uint8_t *buf, size_t len);

#endif
