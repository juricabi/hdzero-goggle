#!/bin/sh
# HDZero goggle (Goggle 1 / Goggle 2 / BoxPro) - HDMI audio + pin probe, READ-ONLY.
#
# Collects what the official OS image cannot tell (live sound cards, pin mux,
# I2S registers, IT66121 / IT66021 registers, dmesg) into one text file on the
# SD card. It changes no persistent state: no file outside the SD card is
# written, no register is written except the bank-select register of the two
# ITE chips, which is restored at once while the goggle app is paused for a
# second (SIGSTOP/SIGCONT). debugfs is mounted (RAM only) if it is not.
#
# Use without SSH: copy this file to the SD card root as  develop.sh , boot.
#   The goggle firmware runs /mnt/extsd/develop.sh INSTEAD of the goggle app
#   (mkapp/app/script/rc.sh), so this script starts the app itself after part 1
#   and the goggle works normally. 2 beeps = part 1 done, app starting.
#   Wait 2 minutes, then power off and send  hdmi_audio_probe_N.txt  (and
#   hdmi_audio_dmesg_N.txt). Delete/rename develop.sh afterwards.
# Use with SSH (app already running):  sh /mnt/extsd/hdmi_audio_probe.sh
#
# For a second run with HDMI in, connect an HDMI source, select HDMI in as the
# source within the first minute after the 2 beeps (part 2 starts 60 s after them).
#
# POSIX sh for busybox 1.27 ash (no head/sort/find/printf/date/timeout needed).
# PROBE_ROOT / PROBE_OUTDIR / PROBE_WAIT are for testing on a PC only.

VERSION=1
R="${PROBE_ROOT:-}"
OUTDIR="${PROBE_OUTDIR:-$R/mnt/extsd}"
WAIT_APP="${PROBE_WAIT:-60}"
APP="$R/mnt/app/app/HDZGOGGLE"
DUMP="$R/sys/class/sunxi_dump"
DBG="$R/sys/kernel/debug"
DT="$R/proc/device-tree"
PATH="$PATH:/usr/sbin:/usr/bin:/sbin:/bin:/mnt/app/app/record/audio"
export PATH

[ -d "$OUTDIR" ] && [ -w "$OUTDIR" ] || OUTDIR="$R/tmp"
n=1
while [ -e "$OUTDIR/hdmi_audio_probe_$n.txt" ]; do n=$((n + 1)); done
OUT="$OUTDIR/hdmi_audio_probe_$n.txt"
DMESG_OUT="$OUTDIR/hdmi_audio_dmesg_$n.txt"
: > "$OUT" || exit 1

log() { echo "$*" >> "$OUT"; }
sec() { log ""; log "==================== $* ===================="; }
have() { command -v "$1" > /dev/null 2>&1; }
run() {
    log "\$ $*"
    if have "$1"; then
        "$@" >> "$OUT" 2>&1
        log "[exit $?]"
    else
        log "(command $1 not available)"
    fi
}
catf() {
    for f in "$@"; do
        if [ -r "$f" ] && [ -f "$f" ]; then
            log "--- $f"
            cat "$f" >> "$OUT" 2>&1
        else
            log "--- $f (missing)"
        fi
    done
}
# grep lines matching an extended regex in a file, with a header
grepf() {
    log "--- $2 | grep -E '$1'"
    if [ -r "$2" ]; then grep -E "$1" "$2" >> "$OUT" 2>&1; else log "(missing)"; fi
}

hex() { awk -v v="$1" 'BEGIN { printf "0x%08x", v }'; }
hex2() { awk -v v="$1" 'BEGIN { printf "0x%02x", v }'; }

# Read SoC registers through the sunxi_dump sysfs interface (what /usr/sbin/awr does).
# $1 = start address (decimal or 0x..), $2 = number of 32-bit words.
regs() {
    if [ ! -w "$DUMP/dump" ]; then
        log "(no $DUMP/dump, cannot read registers)"
        return
    fi
    a=$(($1))
    i=0
    line=""
    while [ "$i" -lt "$2" ]; do
        h=$(hex "$a")
        echo "$h" > "$DUMP/dump" 2> /dev/null
        v=$(cat "$DUMP/dump" 2> /dev/null)
        line="$line $h=$v"
        a=$((a + 4))
        i=$((i + 1))
        if [ $((i % 4)) -eq 0 ]; then
            log "$line"
            line=""
        fi
    done
    [ -n "$line" ] && log "$line"
}

beep() {
    g="$R/sys/class/gpio"
    [ -d "$g/gpio131" ] || echo 131 > "$g/export" 2> /dev/null
    echo out > "$g/gpio131/direction" 2> /dev/null
    k=0
    while [ "$k" -lt "$1" ]; do
        echo 1 > "$g/gpio131/value" 2> /dev/null
        usleep 150000 2> /dev/null || sleep 1
        echo 0 > "$g/gpio131/value" 2> /dev/null
        usleep 250000 2> /dev/null || sleep 1
        k=$((k + 1))
    done
}

app_pid() { pidof HDZGOGGLE 2> /dev/null; }

# Dump one ITE chip. $1 bus, $2 address, $3 bank-select mask on reg 0x0F, $4.. banks.
# Bank switching is done only while the goggle app is stopped, and reg 0x0F is restored.
ite_dump() {
    bus=$1
    addr=$2
    mask=$3
    shift 3
    if ! have i2cdump || ! have i2cget || ! have i2cset; then
        log "(i2c-tools missing)"
        return
    fi
    log "--- i2cget -y $bus $addr 0x00..0x03 (chip id)"
    for r in 0x00 0x01 0x02 0x03; do log "$r: $(i2cget -y "$bus" "$addr" "$r" 2>&1)"; done
    orig=$(i2cget -y "$bus" "$addr" 0x0f 2>&1)
    log "reg 0x0F (bank select) = $orig"
    case "$orig" in 0x*) ;; *)
        log "(chip not answering, no dump)"
        return
        ;;
    esac
    pid=$(app_pid)
    # shellcheck disable=SC2086 # pidof may return several pids
    [ -n "$pid" ] && kill -STOP $pid 2> /dev/null
    for b in "$@"; do
        nv=$(hex2 $(((orig & ~mask) | b)))
        i2cset -y "$bus" "$addr" 0x0f "$nv" 2> /dev/null
        log "--- bank $b (0x0F=$nv): i2cdump -y $bus $addr b"
        i2cdump -y "$bus" "$addr" b >> "$OUT" 2>&1
    done
    i2cset -y "$bus" "$addr" 0x0f "$orig" 2> /dev/null
    # shellcheck disable=SC2086
    [ -n "$pid" ] && kill -CONT $pid 2> /dev/null
    log "reg 0x0F restored to $(i2cget -y "$bus" "$addr" 0x0f 2>&1)"
}

dt_props() { # print every property of a DT node directory, recurse $2 levels
    [ -d "$1" ] || return
    log "--- node ${1#"$DT"}"
    for f in "$1"/*; do
        [ -e "$f" ] || continue
        if [ -d "$f" ]; then
            [ "$2" -gt 0 ] && dt_props "$f" $(($2 - 1))
            continue
        fi
        nm=${f##*/}
        case "$nm" in phandle | linux,phandle) continue ;; esac
        sz=$(wc -c < "$f" 2> /dev/null)
        s=$(tr '\000' '|' < "$f" 2> /dev/null)
        x=$(hexdump -v -n 64 -e '/1 "%02x"' "$f" 2> /dev/null)
        log "  $nm [$sz] = \"$s\"  hex:$x"
    done
}

pinctrl_dump() {
    for d in "$DBG"/pinctrl/*/; do
        [ -d "$d" ] || continue
        for f in pinmux-pins pins pinmux-functions pingroups pinconf-pins gpio-ranges; do
            catf "$d$f"
        done
    done
    sec "pin mux summary: PE16-21, PG0-13, PH0-8 (module bay PG0-2 = GPIO 192-194, I2S0 PH1-3, I2S2 PG10/11/13)"
    for d in "$DBG"/pinctrl/*/; do
        [ -r "${d}pinmux-pins" ] && grep -E '\((P(G([0-9]|1[0-3])|H[0-8]|E(1[6-9]|2[01])))\)' "${d}pinmux-pins" >> "$OUT" 2>&1
    done
}

######################################################################
part1() {
    sec "hdmi_audio_probe v$VERSION part 1 (static)"
    catf "$R/proc/version" "$R/proc/cmdline" "$R/proc/uptime"
    run uname -a
    catf "$R/mnt/app/version" "$R/mnt/app/platform" "$R/etc/openwrt_version"
    log "app running at start: pid '$(app_pid)'"
    run ps

    sec "kernel config"
    if [ -r "$R/proc/config.gz" ]; then
        cp "$R/proc/config.gz" "$OUTDIR/hdmi_audio_config_$n.gz" 2> /dev/null && log "copied /proc/config.gz to hdmi_audio_config_$n.gz"
    else
        log "/proc/config.gz absent (the shipped kernels have no IKCONFIG)"
    fi
    run lsmod
    catf "$R/proc/modules" "$R/proc/filesystems"
    run ls -l "$R/lib/modules" "$R/mnt/app/ko"

    sec "ALSA"
    run ls -l "$R/proc/asound" "$R/dev/snd"
    catf "$R/proc/asound/version" "$R/proc/asound/cards" "$R/proc/asound/devices" "$R/proc/asound/pcm" "$R/proc/asound/timers"
    for c in "$R"/proc/asound/card[0-9]*; do
        [ -d "$c" ] || continue
        catf "$c/id"
        for p in "$c"/pcm*; do
            [ -d "$p" ] || continue
            catf "$p/info" "$p/sub0/info" "$p/sub0/hw_params" "$p/sub0/sw_params" "$p/sub0/status"
        done
    done
    cards=$(awk '/^ *[0-9]+ \[/ { print $1 }' "$R/proc/asound/cards" 2> /dev/null)
    for c in $cards; do
        run amixer -c "$c" info
        run amixer -c "$c" contents
    done
    run aplay -l
    run arecord -l
    run aplay -L

    sec "ASoC debugfs / clocks"
    mounted_dbg=0
    if ! grep -q debugfs "$R/proc/mounts" 2> /dev/null; then
        if [ -z "$R" ] && mount -t debugfs none /sys/kernel/debug 2> /dev/null; then
            mounted_dbg=1
            log "(mounted debugfs on /sys/kernel/debug, RAM only)"
        else
            log "(debugfs not mounted and could not be mounted)"
        fi
    fi
    catf "$DBG/asoc/codecs" "$DBG/asoc/dais" "$DBG/asoc/platforms"
    grepf 'i2s|daudio|audio|codec|pll_audio|hdmi|dmic' "$DBG/clk/clk_summary"
    grepf 'daudio|i2s|codec|audio|dma|mmc|sdc' "$R/proc/interrupts"

    sec "device tree: sound / i2s / codec / hdmi / sdc1 / wlan / pin groups"
    soc=""
    for s in "$DT"/soc*; do [ -d "$s" ] && soc="$s"; done
    for nd in "$soc"/daudio* "$soc"/sound* "$soc"/codec* "$soc"/i2s* "$soc"/dmic* "$soc"/hdmi* "$soc"/sdmmc@04021000 "$soc"/uart@05001000; do
        [ -d "$nd" ] && dt_props "$nd" 0
    done
    for nd in "$DT"/snddaudo* "$DT"/wlan "$DT"/aliases; do
        [ -d "$nd" ] && dt_props "$nd" 0
    done
    pc="$soc/pinctrl@0300b000"
    for nd in "$pc"/daudio* "$pc"/aif* "$pc"/sdc1* "$pc"/uart4* "$pc"/uart3* "$pc"/dmic* "$pc"/hdmi*; do
        [ -d "$nd" ] && dt_props "$nd" 0
    done
    run ls "$R/sys/class"
    run ls -l "$R/sys/class/script"

    sec "pin control (debugfs) - after rc.sh"
    pinctrl_dump
    catf "$DBG/gpio"
    run ls "$R/sys/class/gpio"

    sec "SoC registers (sunxi_dump)"
    log "PIO port E CFG0..PULL1 (0x0300B090..B0B0), rc.sh writes PE_CFG2 0x0300B098=0x00775577"
    regs 0x0300B090 9
    log "PIO port G CFG0..PULL1 (0x0300B0D8..B0F8), rc.sh writes PG_CFG0 0x0300B0D8=0x22777777 (PG0-5 off, PG6/7 uart1)"
    regs 0x0300B0D8 9
    log "PIO port H CFG0..PULL1 (0x0300B0FC..B11C), rc.sh writes PH_CFG0 0x0300B0FC=0x35517751 (PH1 uart4, PH2/3 off)"
    regs 0x0300B0FC 9
    log "I2S0 daudio@0x05090000 (CTL FMT0 FMT1 ISTA RXFIFO FCTL FSTA INT TXFIFO CLKD TXCNT RXCNT CHCFG ...)"
    regs 0x05090000 29
    log "I2S1 / tdmhdmi daudio@0x05091000"
    regs 0x05091000 29
    log "I2S2 daudio@0x05092000"
    regs 0x05092000 29
    log "codec internal I2S 0x05096000.. (first 16 words) and mic bias 0x050967c0"
    regs 0x05096000 16
    regs 0x050967c0 1
    log "CCU: PLL_AUDIO area 0x03001070.. and audio/I2S clock + gating area 0x03001A00..0x03001A7C"
    regs 0x03001070 4
    regs 0x03001A00 32

    sec "I2C"
    run i2cdetect -l
    run ls -l "$R"/dev/i2c-*
}

part2() {
    sec "hdmi_audio_probe v$VERSION part 2 (app running: pid '$(app_pid)')"
    catf "$R/proc/uptime"
    run ps
    sec "ALSA streams now"
    for c in "$R"/proc/asound/card[0-9]*; do
        for p in "$c"/pcm*; do
            [ -d "$p" ] && catf "$p/sub0/hw_params" "$p/sub0/status"
        done
    done
    sec "pin mux after app start"
    for d in "$DBG"/pinctrl/*/; do
        [ -r "${d}pinmux-pins" ] && grep -E '\((P(G([0-9]|1[0-3])|H[0-8]|E(1[6-9]|2[01])))\)' "${d}pinmux-pins" >> "$OUT" 2>&1
    done
    catf "$DBG/gpio"
    regs 0x0300B0D8 9
    regs 0x0300B0FC 9
    log "I2S0 / I2S2 again (TXCNT 0x28 / RXCNT 0x2C count samples only while clocks run)"
    regs 0x05090000 29
    regs 0x05092000 29

    sec "IT66121 HDMI out (bus 1, 0x4c): bank 0 and bank 1"
    ite_dump 1 0x4c 0x03 0 1
    sec "IT66021 HDMI in (bus 3, 0x49): banks 0, 1, 2; EDID RAM 0x54; ring/MHL 0x70"
    ite_dump 3 0x49 0x03 0 1 2
    run i2cdump -y 3 0x54 b
    run i2cdump -y 3 0x70 b
    sec "dmesg (audio/pin related lines; full log in $(basename "$DMESG_OUT"))"
    if have dmesg; then
        dmesg > "$DMESG_OUT" 2>&1
        grep -i -E 'daudio|snd|sound|audio|codec|i2s|hdmi|pinctrl|pin [0-9]|sdc1|mmc1|uart4|xradio|wlan|asoc' "$DMESG_OUT" >> "$OUT" 2>&1
    fi
    sec "tools present"
    run busybox --list
    run ls /bin /sbin /usr/bin /usr/sbin /mnt/app/app/record/audio
    [ "${mounted_dbg:-0}" = 1 ] && umount /sys/kernel/debug 2> /dev/null && log "(debugfs unmounted)"
    sec "end"
}

######################################################################
part1
sync

if [ -n "$(app_pid)" ]; then
    part2
elif [ -x "$APP" ]; then
    # develop.sh mode: start the goggle app exactly like rc.sh would, then part 2
    beep 2
    log ""
    log "starting $APP, part 2 in $WAIT_APP s"
    sync
    if [ -e "$R/mnt/extsd/HDZGOGGLE.log" ]; then
        "$APP" > "$R/mnt/extsd/HDZGOGGLE.log" 2>&1 &
    else
        "$APP" &
    fi
    (
        sleep "$WAIT_APP"
        part2
        sync
    ) &
else
    log "(goggle app $APP not found, part 2 skipped)"
    part2
fi
sync
exit 0
