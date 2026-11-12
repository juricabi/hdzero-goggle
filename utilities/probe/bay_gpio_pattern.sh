#!/bin/sh
# OPTIONAL, ACTIVE TEST - drives pins. Run it ONLY with the module bay EMPTY
# (or with a module you are willing to risk). It is NOT part of the read-only probe.
#
# Purpose: find with a multimeter which module-bay contact each of GPIO 192,
# 193, 194 (PG0, PG1, PG2 = GPIO_MODULE_CLK / _DAT / _CS in src/core/defines.h)
# reaches. Pattern, repeated 3 times:
#   all three low ............................................. 10 s
#   1 beep  -> GPIO 192 (PG0) high, others low ................ 10 s, then low 3 s
#   2 beeps -> GPIO 193 (PG1) high, others low ................ 10 s, then low 3 s
#   3 beeps -> GPIO 194 (PG2) high, others low ................ 10 s, then low 3 s
#   finally 4 long beeps, pins back to inputs, goggle app started.
# Measure each bay contact against ground: the one at ~3.3 V right after N beeps
# is GPIO 191+N.
#
# Refuses to run if (checked first, results written to the SD card):
#   - the goggle app is running (it may drive the same pins: use develop.sh mode),
#   - pinctrl debugfs shows PG0..PG2 owned by a device (e.g. the SDIO/sdc1 host
#     used for Wi-Fi on PG0-PG5) or the GPIOs already requested,
#   - an SDIO card (mmc1) is present or the xradio Wi-Fi driver is loaded.
#
# Use: copy to the SD card root as develop.sh, empty the bay, boot, listen.
#      The report is /mnt/extsd/bay_gpio_N.txt. Delete develop.sh afterwards.
# POSIX sh for busybox 1.27 ash. PROBE_ROOT / PROBE_FAST are for PC tests only.

R="${PROBE_ROOT:-}"
OUTDIR="${PROBE_OUTDIR:-$R/mnt/extsd}"
G="$R/sys/class/gpio"
DBG="$R/sys/kernel/debug"
APP="$R/mnt/app/app/HDZGOGGLE"
HOLD=10
GAP=3
[ -n "$PROBE_FAST" ] && HOLD=0 && GAP=0
PINS="192 193 194"
PATH="$PATH:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH

[ -d "$OUTDIR" ] && [ -w "$OUTDIR" ] || OUTDIR="$R/tmp"
n=1
while [ -e "$OUTDIR/bay_gpio_$n.txt" ]; do n=$((n + 1)); done
OUT="$OUTDIR/bay_gpio_$n.txt"
: > "$OUT" || exit 1
log() { echo "$*" >> "$OUT"; }

pause() { usleep "$1" 2> /dev/null || sleep 1; }
beep() { # $1 count, $2 on-time in us
    [ -d "$G/gpio131" ] || echo 131 > "$G/export" 2> /dev/null
    echo out > "$G/gpio131/direction" 2> /dev/null
    k=0
    while [ "$k" -lt "$1" ]; do
        echo 1 > "$G/gpio131/value" 2> /dev/null
        pause "${2:-150000}"
        echo 0 > "$G/gpio131/value" 2> /dev/null
        pause 350000
        k=$((k + 1))
    done
}

start_app() {
    [ -n "$PROBE_ROOT" ] && return
    [ -x "$APP" ] || return
    if [ -e /mnt/extsd/HDZGOGGLE.log ]; then
        "$APP" > /mnt/extsd/HDZGOGGLE.log 2>&1 &
    else
        "$APP" &
    fi
}

refuse() {
    log "REFUSED: $*"
    log "Nothing was driven. Goggle app started normally."
    sync
    beep 1 1500000
    start_app
    exit 1
}

log "bay_gpio_pattern: checks"

# 1. goggle app must not run (its module-bay code may drive the same pins)
if pidof HDZGOGGLE > /dev/null 2>&1; then
    log "goggle app pid: $(pidof HDZGOGGLE)"
    [ -n "$PROBE_ROOT" ] || refuse "the goggle app is running; run this file as /mnt/extsd/develop.sh instead"
fi

# 2. pin mux ownership (debugfs; mounted in RAM only if needed)
if ! grep -q debugfs "$R/proc/mounts" 2> /dev/null; then
    [ -z "$PROBE_ROOT" ] && mount -t debugfs none /sys/kernel/debug 2> /dev/null
fi
found=0
for d in "$DBG"/pinctrl/*/; do
    f="${d}pinmux-pins"
    [ -r "$f" ] || continue
    for p in 192 193 194; do
        line=$(grep -E "^pin $p \(" "$f")
        [ -n "$line" ] || continue
        found=1
        log "pinmux: $line"
        case "$line" in
            *"(MUX UNCLAIMED)"*) ;;
            *) refuse "pin $p is owned by a device: $line" ;;
        esac
        case "$line" in
            *"(GPIO UNCLAIMED)"*) ;;
            *) refuse "pin $p is already requested as GPIO: $line" ;;
        esac
    done
done
[ "$found" = 1 ] || refuse "cannot read pinctrl debugfs pinmux-pins, ownership of PG0-PG2 unknown"

if [ -r "$DBG/gpio" ]; then
    for p in 192 193 194; do
        l=$(grep -E "gpio-$p " "$DBG/gpio")
        [ -n "$l" ] && refuse "GPIO $p already requested: $l"
    done
fi

# 3. SDIO Wi-Fi on sdc1 (PG0-PG5)
for c in "$R"/sys/bus/mmc/devices/mmc1:*; do
    [ -e "$c" ] && refuse "an SDIO/MMC card is present on mmc1 ($c), PG0-PG5 are in use"
done
if lsmod 2> /dev/null | grep -q -E '^xradio|^sunxi_wlan'; then
    refuse "Wi-Fi driver loaded (xradio/sunxi_wlan), PG0-PG5 may be the SDIO bus"
fi

# raw function of PG0-PG2 (rc.sh writes PG_CFG0 = 0x22777777: PG0-5 disabled)
if [ -w "$R/sys/class/sunxi_dump/dump" ]; then
    echo 0x0300B0D8 > "$R/sys/class/sunxi_dump/dump"
    log "PG_CFG0 (0x0300B0D8) before: $(cat "$R/sys/class/sunxi_dump/dump")"
fi
log "checks passed, driving pattern (hold ${HOLD}s)"
sync

for p in $PINS; do
    echo "$p" > "$G/export" 2> /dev/null
    echo out > "$G/gpio$p/direction" 2> /dev/null || refuse "cannot make GPIO $p an output"
    echo 0 > "$G/gpio$p/value"
done

cycle=1
while [ "$cycle" -le 3 ]; do
    log "cycle $cycle: all low"
    sleep "$HOLD"
    i=1
    for p in $PINS; do
        beep "$i"
        sleep 1
        log "cycle $cycle: GPIO $p high (announced by $i beep(s))"
        echo 1 > "$G/gpio$p/value"
        sleep "$HOLD"
        echo 0 > "$G/gpio$p/value"
        sleep "$GAP"
        i=$((i + 1))
    done
    cycle=$((cycle + 1))
done

for p in $PINS; do
    echo in > "$G/gpio$p/direction" 2> /dev/null
    echo "$p" > "$G/unexport" 2> /dev/null
done
log "done: pins back to inputs and unexported"
sync
beep 4 600000
start_app
exit 0
