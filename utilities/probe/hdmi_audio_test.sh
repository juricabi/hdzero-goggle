#!/bin/sh
# OPTIONAL, ACTIVE TEST of the HDMI audio paths. It writes chip/SoC registers at
# run time only (IT66121 audio registers, PH1-PH3 pin function, I2S0 control),
# restores them at the end, and never writes flash: a power cycle undoes all.
#
# Part A, HDMI OUT: TV/monitor with speakers on the goggle HDMI out, source NOT
#   HDMI in (HDMI out is switched off while HDMI in is shown). Plays a 1 kHz
#   tone (-12 dBFS) for 15 s into ALSA hw:snddaudio0 (I2S0) after programming
#   the IT66121 audio input. Listen: tone on the TV = HDMI out audio works.
#   The IT66121 CTS counter (non-zero = I2S clock reaches the chip) is logged.
# Part B, HDMI IN: HDMI source playing sound on the goggle HDMI in, source
#   HDMI in selected. Records 10 s from hw:snddaudio2 (I2S2) into
#   hdmi_in_test_N.wav on the SD card and logs the I2S2 sample counter and
#   IT66021 registers. Play the WAV on a PC.
#
# Use with SSH (app running, interactive):  sh hdmi_audio_test.sh [out|in]
# Use without SSH: copy this file as develop.sh plus tone1k_48k_s16le_2ch.raw to
#   the SD card root, boot. The script starts the goggle app, then
#   45 s later 1 beep  -> part A (tone 15 s on HDMI out), 2 beeps = A done;
#   then you have 60 s to select HDMI in, 3 beeps -> part B (10 s), 4 beeps = done.
# Report: /mnt/extsd/hdmi_audio_test_N.txt. Delete develop.sh afterwards.
# POSIX sh for busybox 1.27 ash. PROBE_ROOT / PROBE_FAST are for PC tests only.

R="${PROBE_ROOT:-}"
OUTDIR="${PROBE_OUTDIR:-$R/mnt/extsd}"
APP="$R/mnt/app/app/HDZGOGGLE"
DUMP="$R/sys/class/sunxi_dump"
G="$R/sys/class/gpio"
HERE=$(dirname "$0")
TONE_SRC="$HERE/tone1k_48k_s16le_2ch.raw"
TMP="${TMPDIR:-$R/tmp}"
W1=45
W2=60
TONE_S=15
REC_S=10
[ -n "$PROBE_FAST" ] && W1=0 && W2=0 && TONE_S=1 && REC_S=1
PATH="$PATH:/usr/sbin:/usr/bin:/sbin:/bin:/mnt/app/app/record/audio"
export PATH
WHAT="${1:-both}"

[ -d "$OUTDIR" ] && [ -w "$OUTDIR" ] || OUTDIR="$R/tmp"
n=1
while [ -e "$OUTDIR/hdmi_audio_test_$n.txt" ]; do n=$((n + 1)); done
OUT="$OUTDIR/hdmi_audio_test_$n.txt"
: > "$OUT" || exit 1
log() { echo "$*" >> "$OUT"; [ -t 1 ] && echo "$*"; }
have() { command -v "$1" > /dev/null 2>&1; }
hex() { awk -v v="$1" 'BEGIN { printf "0x%08x", v }'; }
pause_us() { usleep "$1" 2> /dev/null || sleep 1; }
beep() {
    [ -d "$G/gpio131" ] || echo 131 > "$G/export" 2> /dev/null
    k=0
    while [ "$k" -lt "$1" ]; do
        echo 1 > "$G/gpio131/value" 2> /dev/null
        pause_us 150000
        echo 0 > "$G/gpio131/value" 2> /dev/null
        pause_us 350000
        k=$((k + 1))
    done
}
interactive() { [ -t 0 ] && [ -t 1 ]; }
step() { # $1 message, $2 seconds to wait when not interactive, $3 beeps
    log ""
    log ">>> $1"
    if interactive; then
        echo "    press Enter to continue"
        read -r _
    else
        sleep "$2"
        beep "${3:-1}"
    fi
}

reg_rd() {
    hex "$1" > "$DUMP/dump" 2> /dev/null
    cat "$DUMP/dump" 2> /dev/null
}
reg_wr() { echo "$(hex "$1") $(hex "$2")" > "$DUMP/write" 2> /dev/null; }

is_hex() { case "$1" in 0x[0-9a-fA-F]*) return 0 ;; esac; return 1; }
app_pid() { pidof HDZGOGGLE 2> /dev/null; }
# shellcheck disable=SC2086 # pidof may return several pids
app_stop() { p=$(app_pid); [ -n "$p" ] && kill -STOP $p 2> /dev/null; }
# shellcheck disable=SC2086
app_cont() { [ -n "$p" ] && kill -CONT $p 2> /dev/null; p=""; }

# IT66121 on bus 1 at 0x4c; reg 0x0F bit0 selects bank 1 (0x130-0x1BF)
B=1
A=0x4c
it_r() { i2cget -y $B $A "$1" 2> /dev/null; }
it_w() { i2cset -y $B $A "$1" "$2" 2> /dev/null; }

it_audio_on() { # same sequence as IT66121_audio(true) in src/driver/it66121.c
    it_w 0x0f 0x08
    it_w 0xe0 0x00
    v=$(it_r 0x04)
    it_w 0x04 $((v & ~0x10))
    it_w 0x58 0x11
    it_w 0x0f 0x09
    it_w 0x33 0x00
    it_w 0x34 0x18
    it_w 0x35 0x00
    it_w 0x0f 0x08
    it_w 0xf8 0xc3
    it_w 0xf8 0xa5
    v=$(it_r 0xc5)
    it_w 0xc5 $((v & ~0x02))
    it_w 0xf8 0xff
    it_w 0xe0 0x00
    it_w 0xe1 0x01
    it_w 0xe2 0xe4
    it_w 0xe3 0x00
    it_w 0xe4 0x00
    it_w 0xe5 0x00
    it_w 0x0f 0x09
    it_w 0x91 0x00
    it_w 0x92 0x00
    it_w 0x93 0x01
    it_w 0x94 0x02
    it_w 0x98 0x02
    it_w 0x99 0xd2
    it_w 0x68 0x01
    it_w 0x69 0x00
    it_w 0x6a 0x00
    it_w 0x6b 0x00
    it_w 0x6c 0x00
    it_w 0x6d 0x70
    it_w 0x0f 0x08
    it_w 0xce 0x03
    v=$(it_r 0x04)
    it_w 0x04 $((v & ~0x14))
    it_w 0xe0 0x01
}

it_status() {
    it_w 0x0f 0x08
    s="04=$(it_r 0x04) 5F=$(it_r 0x5f) E0=$(it_r 0xe0) E1=$(it_r 0xe1) C0=$(it_r 0xc0) C1=$(it_r 0xc1) C5=$(it_r 0xc5) CE=$(it_r 0xce)"
    it_w 0x0f 0x09
    s="$s | 130-137: $(it_r 0x30) $(it_r 0x31) $(it_r 0x32) $(it_r 0x33) $(it_r 0x34) $(it_r 0x35) $(it_r 0x36) $(it_r 0x37) | 1A0-1A2: $(it_r 0xa0) $(it_r 0xa1) $(it_r 0xa2)"
    it_w 0x0f 0x08
    log "IT66121: $s"
}

SAVE0="0x04 0x58 0xc5 0xce 0xe0 0xe1 0xe2 0xe3 0xe4 0xe5"
SAVE1="0x33 0x34 0x35 0x91 0x92 0x93 0x94 0x98 0x99 0x68 0x69 0x6a 0x6b 0x6c 0x6d"
it_save() {
    it_w 0x0f 0x08
    S0=""
    for r in $SAVE0; do S0="$S0 $r=$(it_r "$r")"; done
    it_w 0x0f 0x09
    S1=""
    for r in $SAVE1; do S1="$S1 $r=$(it_r "$r")"; done
    it_w 0x0f 0x08
    log "IT66121 saved bank0:$S0"
    log "IT66121 saved bank1:$S1"
}
it_restore() {
    it_w 0x0f 0x09
    for kv in $S1; do it_w "${kv%=*}" "${kv#*=}"; done
    it_w 0x0f 0x08
    it_w 0xf8 0xc3
    it_w 0xf8 0xa5
    for kv in $S0; do it_w "${kv%=*}" "${kv#*=}"; done
    it_w 0xf8 0xff
    it_w 0x0f 0x08
}

# run "$@" in the background, kill it after $T seconds
run_for() {
    "$@" >> "$OUT" 2>&1 &
    bg=$!
    sec=0
    while [ "$sec" -lt "$T" ] && kill -0 "$bg" 2> /dev/null; do
        sleep 1
        sec=$((sec + 1))
    done
    kill "$bg" 2> /dev/null && log "(stopped after ${T}s)"
    wait "$bg" 2> /dev/null
}

part_out() {
    log "==== part A: HDMI OUT audio ===="
    if ! have aplay || ! have i2cget; then log "aplay / i2c-tools missing"; return; fi
    app_stop
    id="$(it_r 0x00) $(it_r 0x01) $(it_r 0x02) $(it_r 0x03)"
    app_cont
    log "IT66121 id regs: $id (expect 0x54 0x49 0x12 0x?6)"
    case "$id" in 0x54*) ;; *)
        log "IT66121 not answering: HDMI out is off (source HDMI in?) - part A skipped"
        return
        ;;
    esac

    ph=$(reg_rd 0x0300B0FC)
    ctl=$(reg_rd 0x05090000)
    log "PH_CFG0=$ph I2S0_CTL=$ctl (before)"
    if ! is_hex "$ph" || ! is_hex "$ctl"; then
        log "cannot read SoC registers through $DUMP - part A skipped"
        return
    fi

    app_stop
    it_save
    it_audio_on
    it_status
    app_cont

    # PH1-PH3 -> function 3 (I2S0 BCLK/LRCK/DOUT); rc.sh leaves PH1=uart4, PH2/PH3=off
    reg_wr 0x0300B0FC $(((ph & ~0xfff0) | 0x3330))
    log "PH_CFG0 now $(reg_rd 0x0300B0FC)"

    # 15 s of tone: 192-byte period doubled 14 times = 16.4 s
    t="$TMP/hdmi_tone.raw"
    if [ -r "$TONE_SRC" ]; then
        cp "$TONE_SRC" "$t"
        k=0
        while [ "$k" -lt 14 ]; do
            cat "$t" "$t" > "$t.2" && mv "$t.2" "$t"
            k=$((k + 1))
        done
    else
        log "no $TONE_SRC: using noise from /dev/urandom (LOUD - turn the TV down)"
        dd if=/dev/urandom of="$t" bs=192000 count=16 2> /dev/null
    fi

    log "playing tone on hw:snddaudio0 for ${TONE_S}s - listen to the TV"
    (
        sleep 1
        # I2S mode + clock master, after aplay's hw_params (vendor note i2s-daudio-setup.txt)
        c=$(reg_rd 0x05090000)
        is_hex "$c" && reg_wr 0x05090000 $(((c & ~0x30) | 0x60010))
        sleep 2
        log "during playback: I2S0 CTL=$(reg_rd 0x05090000) TXCNT=$(reg_rd 0x05090028) FSTA=$(reg_rd 0x05090018)"
        app_stop
        it_status
        app_cont
        sleep 3
        log "I2S0 TXCNT=$(reg_rd 0x05090028) (should grow 48000/s while playing)"
        app_stop
        it_status
        app_cont
    ) &
    mon=$!
    T=$TONE_S run_for aplay -D hw:snddaudio0 -t raw -f S16_LE -c 2 -r 48000 "$t"
    wait "$mon" 2> /dev/null
    rm -f "$t"

    app_stop
    it_restore
    app_cont
    reg_wr 0x0300B0FC "$ph"
    reg_wr 0x05090000 "$ctl"
    log "restored: PH_CFG0=$(reg_rd 0x0300B0FC) I2S0_CTL=$(reg_rd 0x05090000)"
}

part_in() {
    log "==== part B: HDMI IN audio ===="
    if ! have arecord || ! have i2cdump; then log "arecord / i2c-tools missing"; return; fi
    app_stop
    o=$(i2cget -y 3 0x49 0x0f 2> /dev/null)
    sig="(IT66021 not answering)"
    if is_hex "$o"; then
        i2cset -y 3 0x49 0x0f $((o & ~3)) 2> /dev/null
        sig=$(i2cget -y 3 0x49 0x99 2> /dev/null)
        i2cset -y 3 0x49 0x0f "$o" 2> /dev/null
    fi
    app_cont
    log "IT66021 reg 0x99=$sig (bit3 = video signal detected, used by IT66021_Sig_det)"
    log "PG_CFG1 (PG8-15)=$(reg_rd 0x0300B0DC) I2S2_CTL=$(reg_rd 0x05092000) RXCNT=$(reg_rd 0x0509202c)"
    w="$OUTDIR/hdmi_in_test_$n.wav"
    log "recording ${REC_S}s from hw:snddaudio2 into $(basename "$w")"
    (
        sleep 3
        log "during capture: I2S2 CTL=$(reg_rd 0x05092000) RXCNT=$(reg_rd 0x0509202c) FSTA=$(reg_rd 0x05092018)"
        sleep 3
        log "I2S2 RXCNT=$(reg_rd 0x0509202c) (grows only if the IT66021 drives SCK/WS)"
    ) &
    mon=$!
    T=$((REC_S + 5)) run_for arecord -D hw:snddaudio2 -t wav -f S16_LE -c 2 -r 48000 -d "$REC_S" "$w"
    wait "$mon" 2> /dev/null
    if [ -f "$w" ]; then
        log "wav size: $(wc -c < "$w") bytes (44 = header only, no samples)"
    else
        log "no wav written"
    fi
    app_stop
    o=$(i2cget -y 3 0x49 0x0f 2> /dev/null)
    if is_hex "$o"; then
        for b in 0 1 2; do
            i2cset -y 3 0x49 0x0f $(((o & ~3) | b)) 2> /dev/null
            log "--- IT66021 bank $b"
            i2cdump -y 3 0x49 b >> "$OUT" 2>&1
        done
        i2cset -y 3 0x49 0x0f "$o" 2> /dev/null
    fi
    app_cont
}

log "hdmi_audio_test ($WHAT) - app pid '$(app_pid)'"
if [ -z "$(app_pid)" ] && [ -x "$APP" ] && [ -z "$PROBE_ROOT" ]; then
    log "starting the goggle app"
    if [ -e /mnt/extsd/HDZGOGGLE.log ]; then
        "$APP" > /mnt/extsd/HDZGOGGLE.log 2>&1 &
    else
        "$APP" &
    fi
fi

if [ "$WHAT" != in ]; then
    step "part A (HDMI out): HDMI display connected, source is NOT HDMI in" "$W1" 1
    part_out
    sync
    beep 2
fi
if [ "$WHAT" != out ]; then
    step "part B (HDMI in): select HDMI in now, with a source that plays sound" "$W2" 3
    part_in
    sync
fi
beep 4
log "done"
sync
exit 0
