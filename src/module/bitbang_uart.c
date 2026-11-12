#include "bitbang_uart.h"

#define NS_PER_S 1000000000ULL

// Idle (high) time between the call and the first start bit, so a receiver that
// only just saw the line come up has a clean idle period before the first edge.
#define BITBANG_UART_LEAD_NS 200000ULL

static uint64_t bit_deadline(uint64_t t0, uint64_t bit, uint32_t baud) {
    return t0 + bit * NS_PER_S / baud;
}

static void wait_until(const bitbang_uart_io_t *io, uint64_t deadline, uint64_t *late) {
    uint64_t now;
    while ((now = io->now_ns(io->ctx)) < deadline)
        ;
    if (late)
        *late = now - deadline;
}

uint64_t bitbang_uart_send(const bitbang_uart_io_t *io, uint32_t baud, const uint8_t *buf, size_t len) {
    uint64_t worst = 0;
    int level = 1; // idle
    const uint64_t t0 = io->now_ns(io->ctx) + BITBANG_UART_LEAD_NS;
    const uint64_t nbits = (uint64_t)len * 10;

    if (baud == 0 || len == 0)
        return 0;

    for (uint64_t n = 0; n < nbits; n++) {
        const uint8_t byte = buf[n / 10];
        const int pos = n % 10; // 0 start, 1..8 data (LSB first), 9 stop
        const int bit = pos == 0 ? 0 : pos == 9 ? 1 : (byte >> (pos - 1)) & 1;
        uint64_t late;

        if (bit == level)
            continue;

        wait_until(io, bit_deadline(t0, n, baud), &late);
        if (late > worst)
            worst = late;
        for (int line = 0; line < io->lines; line++)
            io->write_line(io->ctx, line, bit);
        level = bit;
    }

    // the last stop bit gets its full time before anything else may follow
    wait_until(io, bit_deadline(t0, nbits, baud), NULL);
    return worst;
}
