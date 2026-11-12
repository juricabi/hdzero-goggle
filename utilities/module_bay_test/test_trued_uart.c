// Host-side test for the True-D LINK transmitter (not part of the firmware build).
//
// Runs the firmware's own encoder (src/module/trued_proto.c) and bit-banged UART
// (src/module/bitbang_uart.c) against a simulated clock and three simulated bay
// pins, with jitter injected into the clock reads and the pin writes the way the
// goggle's sysfs writes and interrupts would add it.  Each pin's trace is then fed
// to two software UART receivers:
//   - "mid-bit": a pin-change receiver (Arduino SoftwareSerial style) that starts on
//     the falling edge, after a random interrupt latency, and samples every bit once
//     in its middle;
//   - "16x": a hardware UART model, 16x oversampling, majority of samples 7/8/9.
// Every pin and both receivers must return the command bytes exactly.
//
//   make -C utilities/module_bay_test test

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/module/bitbang_uart.h"
#include "../../src/module/trued_proto.h"

#define LINES      3
#define MAX_EDGES  4096
#define MAX_BYTES  128
#define REPEATS    5
#define GAP_NS     30000000ULL // 30 ms between repeats, as in trued.c

typedef struct {
    uint64_t t;
    int level;
} edge_t;

typedef struct {
    // jitter model (ns)
    uint64_t clock_cost_min, clock_cost_max; // one clock_gettime()
    uint64_t write_cost_min, write_cost_max; // one sysfs write()
    double spike_prob;                       // chance that a clock read or a write is hit by an interrupt
    uint64_t spike_min, spike_max;           // its length
    // state
    uint64_t now;
    uint32_t rng;
    edge_t edges[LINES][MAX_EDGES];
    int n_edges[LINES];
} sim_t;

static uint32_t rnd(sim_t *s) {
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    return s->rng;
}

static uint64_t rnd_range(sim_t *s, uint64_t lo, uint64_t hi) {
    return hi <= lo ? lo : lo + rnd(s) % (hi - lo + 1);
}

static uint64_t spike(sim_t *s) {
    if ((rnd(s) % 1000000) < s->spike_prob * 1000000)
        return rnd_range(s, s->spike_min, s->spike_max);
    return 0;
}

static uint64_t sim_now(void *ctx) {
    sim_t *s = ctx;
    s->now += rnd_range(s, s->clock_cost_min, s->clock_cost_max) + spike(s);
    return s->now;
}

static void sim_write(void *ctx, int line, int level) {
    sim_t *s = ctx;
    uint64_t cost = rnd_range(s, s->write_cost_min, s->write_cost_max) + spike(s);
    // the pin changes somewhere inside the syscall
    uint64_t t = s->now + rnd_range(s, cost / 4, (cost * 3) / 4);
    s->now += cost;
    if (s->n_edges[line] < MAX_EDGES) {
        s->edges[line][s->n_edges[line]].t = t;
        s->edges[line][s->n_edges[line]].level = level;
        s->n_edges[line]++;
    }
}

// edges [from, to) of a line: one packet
typedef struct {
    const sim_t *s;
    int line, from, to;
} trace_t;

static int level_at(const trace_t *tr, uint64_t t) {
    int level = 1; // idle high before the first edge
    for (int i = tr->from; i < tr->to && tr->s->edges[tr->line][i].t <= t; i++)
        level = tr->s->edges[tr->line][i].level;
    return level;
}

// first falling edge at or after t
static int next_start(const trace_t *tr, uint64_t t, uint64_t *start) {
    const sim_t *s = tr->s;
    const int line = tr->line;
    for (int i = tr->from; i < tr->to; i++) {
        if (s->edges[line][i].t >= t && s->edges[line][i].level == 0) {
            *start = s->edges[line][i].t;
            return 1;
        }
    }
    return 0;
}

// Pin-change receiver: start edge seen after an interrupt latency of up to
// rx_latency_ns, then one sample in the middle of every bit.  Returns bytes
// decoded, -1 on a framing error.
static int decode_midbit(const trace_t *tr, sim_t *rng, uint64_t rx_latency_ns, uint8_t *out, int max) {
    const double bit = 1e9 / TRUED_BAUD;
    uint64_t t = 0, start;
    int n = 0;
    while (n < max && next_start(tr, t, &start)) {
        double t0 = (double)start + (double)rnd_range(rng, 0, rx_latency_ns);
        if (level_at(tr, (uint64_t)(t0 + 0.5 * bit)) != 0)
            return -1; // glitch, not a start bit
        uint8_t byte = 0;
        for (int k = 0; k < 8; k++)
            byte |= level_at(tr, (uint64_t)(t0 + (1.5 + k) * bit)) << k;
        if (level_at(tr, (uint64_t)(t0 + 9.5 * bit)) != 1)
            return -1; // framing error
        out[n++] = byte;
        t = (uint64_t)(t0 + 9.5 * bit);
    }
    return n;
}

// Hardware UART model: 16 samples per bit from the start edge (as detected on
// the 16x clock), a bit is the majority of samples 7, 8 and 9.
static int decode_16x(const trace_t *tr, uint8_t *out, int max) {
    const double tick = 1e9 / TRUED_BAUD / 16;
    uint64_t t = 0, start;
    int n = 0;
    while (n < max && next_start(tr, t, &start)) {
        // the receiver sees the edge on its next sample tick
        double t0 = ((uint64_t)(start / tick) + 1) * tick;
        int bits[10];
        for (int b = 0; b < 10; b++) {
            int ones = 0;
            for (int k = 7; k <= 9; k++)
                ones += level_at(tr, (uint64_t)(t0 + (b * 16 + k - 1) * tick));
            bits[b] = ones >= 2;
        }
        if (bits[0] != 0 || bits[9] != 1)
            return -1;
        uint8_t byte = 0;
        for (int k = 0; k < 8; k++)
            byte |= bits[1 + k] << k;
        out[n++] = byte;
        t = (uint64_t)(t0 + 9.5 * 16 * tick);
    }
    return n;
}

// Largest deviation of an edge from its ideal place, measured from the packet's
// first start edge on the same line, in % of a bit.
static double worst_edge_error(const sim_t *s, int line, const uint8_t *frame, int len) {
    const double bit = 1e9 / TRUED_BAUD;
    double worst = 0;
    int e = 0;
    for (int rep = 0; rep < REPEATS; rep++) {
        int level = 1;
        uint64_t t0 = 0;
        for (int n = 0; n < len * 10; n++) {
            int pos = n % 10;
            int b = pos == 0 ? 0 : pos == 9 ? 1 : (frame[n / 10] >> (pos - 1)) & 1;
            if (b == level)
                continue;
            if (e >= s->n_edges[line])
                return 1e9;
            if (n == 0)
                t0 = s->edges[line][e].t;
            double err = ((double)s->edges[line][e].t - (double)t0 - n * bit) / bit * 100.0;
            if (err < 0)
                err = -err;
            if (err > worst)
                worst = err;
            level = b;
            e++;
        }
    }
    return worst;
}

enum {
    EXPECT_ALL,     // every packet intact on every line, both receivers
    EXPECT_ONE,     // at least one of the REPEATS packets intact on every line (why it repeats)
    EXPECT_DAMAGE,  // the receivers must see damaged packets (the test can fail)
};

typedef struct {
    const char *name;
    uint64_t clock_min, clock_max, write_min, write_max;
    double spike_prob;
    uint64_t spike_min, spike_max;
    uint64_t rx_latency;
    int expect;
} scenario_t;

typedef struct {
    int packets, packets_ok; // over all lines and both receivers
    int lines_without_packet; // lines on which no packet of the burst was intact
    uint64_t late;
    double edge_error;
} result_t;

static int packet_ok(const uint8_t *got, int n, const uint8_t *frame) {
    return n == TRUED_CMD_LEN && memcmp(got, frame, TRUED_CMD_LEN) == 0;
}

static void run(const scenario_t *sc, uint8_t channel, uint32_t seed, int verbose, result_t *res) {
    static sim_t s;
    static sim_t rx_rng;
    int first[REPEATS + 1][LINES];
    memset(&s, 0, sizeof(s));
    s.clock_cost_min = sc->clock_min;
    s.clock_cost_max = sc->clock_max;
    s.write_cost_min = sc->write_min;
    s.write_cost_max = sc->write_max;
    s.spike_prob = sc->spike_prob;
    s.spike_min = sc->spike_min;
    s.spike_max = sc->spike_max;
    s.rng = seed ? seed : 1;
    s.now = 1000000;
    rx_rng.rng = seed * 31 + 7;

    uint8_t frame[TRUED_CMD_LEN];
    trued_build_channel_cmd(channel, frame);

    // the burst trued.c sends: REPEATS packets, GAP_NS apart
    bitbang_uart_io_t io = {.ctx = &s, .now_ns = sim_now, .write_line = sim_write, .lines = LINES};
    for (int rep = 0; rep < REPEATS; rep++) {
        if (rep)
            s.now += GAP_NS;
        for (int line = 0; line < LINES; line++)
            first[rep][line] = s.n_edges[line];
        uint64_t l = bitbang_uart_send(&io, TRUED_BAUD, frame, TRUED_CMD_LEN);
        if (l > res->late)
            res->late = l;
    }
    for (int line = 0; line < LINES; line++)
        first[REPEATS][line] = s.n_edges[line];

    for (int line = 0; line < LINES; line++) {
        int ok_mid = 0, ok_16x = 0;
        for (int rep = 0; rep < REPEATS; rep++) {
            trace_t tr = {&s, line, first[rep][line], first[rep + 1][line]};
            uint8_t got[MAX_BYTES];
            ok_mid += packet_ok(got, decode_midbit(&tr, &rx_rng, sc->rx_latency, got, MAX_BYTES), frame);
            ok_16x += packet_ok(got, decode_16x(&tr, got, MAX_BYTES), frame);
        }
        double err = worst_edge_error(&s, line, frame, TRUED_CMD_LEN);
        if (err > res->edge_error)
            res->edge_error = err;
        res->packets += 2 * REPEATS;
        res->packets_ok += ok_mid + ok_16x;
        if (!ok_mid || !ok_16x)
            res->lines_without_packet++;
        if (verbose)
            printf("  ch %2u line %d: intact packets mid-bit %d/%d, 16x %d/%d, worst edge error %5.1f%% of a bit\n",
                   channel, line, ok_mid, REPEATS, ok_16x, REPEATS, err);
    }
}

int main(void) {
    static const struct {
        uint8_t ch;
        uint8_t bytes[TRUED_CMD_LEN];
    } vectors[] = {
        {0, {0xFD, 0x02, 0x02, 0x05, 0x41, 0xEE, 0x00, 0x00, 0x70, 0x05}},
        {32, {0xFD, 0x02, 0x02, 0x05, 0x41, 0xEE, 0x00, 0x20, 0x54, 0x67}},
        {47, {0xFD, 0x02, 0x02, 0x05, 0x41, 0xEE, 0x00, 0x2F, 0xA5, 0x88}},
    };
    // Costs in ns.  "ideal": only the clock read takes time (it has to advance for
    // the busy-wait to end).  "goggle": a guess of the V536 sysfs path, 2-8 us per
    // pin write, interrupts of 5-20 us on 0.2 % of the calls.  "slow": 6-25 us per
    // write and 10-30 us interrupts on 1 % of the calls; single packets get damaged
    // there, the burst of REPEATS must still deliver one.  "broken": 60-200 us
    // stalls on 2 % of the calls, more than a UART can take; the receivers must
    // report damage, or passing the other scenarios would prove nothing.
    static const scenario_t scenarios[] = {
        {"ideal", 100, 100, 0, 0, 0.0, 0, 0, 0, EXPECT_ALL},
        {"goggle", 40, 120, 2000, 8000, 0.002, 5000, 20000, 5000, EXPECT_ALL},
        {"slow", 40, 200, 6000, 25000, 0.01, 10000, 30000, 10000, EXPECT_ONE},
        {"broken", 40, 120, 2000, 8000, 0.02, 60000, 200000, 5000, EXPECT_DAMAGE},
    };
    static const char *expect_text[] = {"every packet intact", "one intact packet per burst and line", "damage detected"};
    int failures = 0;

    for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
        uint8_t frame[TRUED_CMD_LEN];
        trued_build_channel_cmd(vectors[v].ch, frame);
        int same = memcmp(frame, vectors[v].bytes, TRUED_CMD_LEN) == 0;
        printf("vector ch %2u:", vectors[v].ch);
        for (int i = 0; i < TRUED_CMD_LEN; i++)
            printf(" %02X", frame[i]);
        printf("  %s\n", same ? "ok" : "MISMATCH");
        failures += !same;
    }

    for (size_t c = 0; c < sizeof(scenarios) / sizeof(scenarios[0]); c++) {
        const scenario_t *sc = &scenarios[c];
        result_t res = {0};
        int bursts = 0;
        printf("scenario %s (3 lines, 2 receivers, bursts of %d packets):\n", sc->name, REPEATS);
        for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
            // one verbose burst per vector, then many seeds
            for (uint32_t seed = 0; seed <= 200; seed++, bursts++)
                run(sc, vectors[v].ch, seed ? seed * 7919 + v : 12345 + v, seed == 0, &res);
        }
        int good;
        switch (sc->expect) {
        case EXPECT_ALL:
            good = res.packets_ok == res.packets;
            break;
        case EXPECT_ONE:
            good = res.lines_without_packet == 0;
            break;
        default:
            good = res.packets_ok < res.packets;
            break;
        }
        printf("  %d bursts: %d/%d packets intact (%.2f%%), %d line-bursts without an intact packet,\n"
               "  worst edge error %.1f%% of a bit, worst edge lateness %llu ns; expected %s -> %s\n",
               bursts, res.packets_ok, res.packets, 100.0 * res.packets_ok / res.packets, res.lines_without_packet,
               res.edge_error, (unsigned long long)res.late, expect_text[sc->expect], good ? "PASS" : "FAIL");
        failures += !good;
    }

    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
