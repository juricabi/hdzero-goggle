#!/bin/sh
# Host check that the HDMI-audio changes leave IT66121_init()/set_phase()/close()
# byte-for-byte unchanged while [hdmi] audio_out is off, and print the extra
# register traffic when it is on. Compiles src/driver/it66121.c with stub I2C.
# Usage: sh utilities/probe/test/it66121_seq_test.sh [base-commit]   (default 8364a43)
set -eu
BASE=${1:-8364a43}
TOP=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/inc/log" "$T/inc/driver"
for d in orig new; do
    mkdir -p "$T/$d/driver" "$T/$d/core"
    echo '#include "stub_i2c.h"' > "$T/$d/driver/i2c.h"
    printf '#pragma once\nvoid DM5680_ResetHDMI_TX(int);\n' > "$T/$d/driver/dm5680.h"
    echo '#pragma once' > "$T/$d/core/common.hh"
done
echo '#pragma once' > "$T/inc/driver/hardware.h"
printf '#pragma once\n#include <stdio.h>\n#define LOGI(...) do { fprintf(stderr, __VA_ARGS__); fputc(10, stderr); } while (0)\n#define LOGE LOGI\n' > "$T/inc/log/log.h"
cat > "$T/inc/stub_i2c.h" << 'H'
#pragma once
#include <stdint.h>
uint8_t stub_read(int port, uint8_t s, uint8_t a);
void stub_write(int port, uint8_t s, uint8_t a, uint8_t d);
#define ADDR_IT66121 0x4C
#define ADDR_FPGA 0x64
#define I2C_Write(s, a, d) stub_write(2, s, a, d)
#define I2C_Read(s, a) stub_read(2, s, a)
#define I2C_R_Write(s, a, d) stub_write(1, s, a, d)
#define I2C_R_Read(s, a) stub_read(1, s, a)
H
cat > "$T/harness.c" << 'C'
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "stub_i2c.h"
static uint8_t regs[2][256];
static int bank(void) { return regs[0][0x0f] & 1; }
uint8_t stub_read(int port, uint8_t s, uint8_t a) {
    uint8_t v = (port == 1 && s == ADDR_IT66121) ? ((a < 0x30) ? regs[0][a] : regs[bank()][a]) : 0;
    printf("R %d %02x %02x -> %02x\n", port, s, a, v);
    return v;
}
void stub_write(int port, uint8_t s, uint8_t a, uint8_t d) {
    printf("W %d %02x %02x %02x\n", port, s, a, d);
    if (port == 1 && s == ADDR_IT66121) {
        if (a < 0x30) regs[0][a] = regs[1][a] = d; else regs[bank()][a] = d;
    }
}
void DM5680_ResetHDMI_TX(int v) { printf("RESET %d\n", v); }
int usleep(unsigned u) { printf("SLEEP %u\n", u); return 0; }
void IT66121_init(); void IT66121_close(); void IT66121_set_phase(uint8_t, uint8_t);
#ifdef NEWAPI
#include "it66121.h"
#endif
int main(int argc, char **argv) {
    regs[0][0x02] = 0x12; regs[0][0xc5] = 0x04; regs[0][0x5f] = 0xe0;
#ifdef NEWAPI
    if (argc > 1) IT66121_audio_config(true, IT66121_AUDIO_I2S);
#endif
    IT66121_init();
    IT66121_set_phase(1, 1);
    IT66121_close();
    return 0;
}
C
git -C "$TOP" show "$BASE:src/driver/it66121.c" > "$T/orig/driver/it66121.c"
git -C "$TOP" show "$BASE:src/driver/it66121.h" > "$T/orig/driver/it66121.h"
cp "$TOP/src/driver/it66121.c" "$TOP/src/driver/it66121.h" "$T/new/driver/"
rc=0
for v in HDZGOGGLE HDZGOGGLE2 HDZBOXPRO; do
    gcc -w -D$v -I"$T/inc" -I"$T/orig/driver" -o "$T/o" "$T/harness.c" "$T/orig/driver/it66121.c"
    gcc -w -D$v -DNEWAPI -I"$T/inc" -I"$T/new/driver" -o "$T/n" "$T/harness.c" "$T/new/driver/it66121.c"
    "$T/o" > "$T/o.txt"
    "$T/n" > "$T/n.txt" 2> /dev/null
    if cmp -s "$T/o.txt" "$T/n.txt"; then
        echo "PASS $v: audio_out=0 register sequence identical to $BASE ($(grep -c '^W' "$T/o.txt") writes)"
    else
        echo "FAIL $v: sequence differs"; diff "$T/o.txt" "$T/n.txt" | head; rc=1
    fi
done
"$T/n" on > "$T/a.txt" 2> /dev/null
echo "audio_out=1 adds $(($(grep -c '^W' "$T/a.txt") - $(grep -c '^W' "$T/n.txt"))) writes after IT66121_init():"
diff "$T/n.txt" "$T/a.txt" | sed -n 's/^> W 1 4c /  /p' | tr '\n' ' '
echo
exit $rc
