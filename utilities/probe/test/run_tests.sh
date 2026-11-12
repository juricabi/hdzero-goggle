#!/bin/sh
# PC-side tests for the probe kit: shellcheck, then every script under dash and
# busybox ash against a stub /proc, /sys and /mnt tree with stub i2c/ALSA tools.
# Optional: ROOTFS=/path/to/extracted/goggle/rootfs checks that every external
# command the scripts use exists in the real goggle root filesystem.
# Usage: sh utilities/probe/test/run_tests.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
KIT=$(cd "$HERE/.." && pwd)
SCRIPTS="hdmi_audio_probe.sh bay_gpio_pattern.sh hdmi_audio_test.sh"
fail=0
ok() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

if command -v shellcheck > /dev/null 2>&1; then
    for s in $SCRIPTS; do
        if shellcheck -s sh "$KIT/$s"; then ok "shellcheck $s"; else bad "shellcheck $s"; fi
    done
else
    echo "SKIP shellcheck (not installed)"
fi

if [ -n "${ROOTFS:-}" ]; then
    for c in awk cat cp dd grep hexdump kill ls lsmod mkdir mount mv pidof ps rm sed sleep sync tail tr \
        umount uname usleep wc dmesg i2cget i2cset i2cdump i2cdetect amixer aplay arecord busybox dirname basename; do
        f=""
        for d in bin sbin usr/bin usr/sbin; do [ -e "$ROOTFS/$d/$c" ] && f="$d/$c"; done
        if [ -n "$f" ]; then ok "rootfs has $c ($f)"; else bad "rootfs lacks $c"; fi
    done
fi

mkstub() { # $1 = root, $2 = bay scenario (claimed|free)
    r=$1
    mkdir -p "$r/proc/asound/card0/pcm0p/sub0" "$r/proc/asound/card1/pcm0p/sub0" "$r/proc/asound/card2/pcm0c/sub0" \
        "$r/proc/device-tree/soc@03000000/daudio@0x05090000" "$r/proc/device-tree/soc@03000000/sound@1" \
        "$r/proc/device-tree/soc@03000000/pinctrl@0300b000/daudio0@0" "$r/proc/device-tree/snddaudo0" \
        "$r/sys/class/sunxi_dump" "$r/sys/class/gpio/gpio131" "$r/sys/class/gpio/gpio192" "$r/sys/class/gpio/gpio193" \
        "$r/sys/class/gpio/gpio194" "$r/sys/kernel/debug/pinctrl/300b000.pinctrl" "$r/sys/kernel/debug/asoc" \
        "$r/sys/bus/mmc/devices" "$r/mnt/extsd" "$r/mnt/app/app" "$r/tmp" "$r/lib/modules" "$r/dev/snd" "$r/bin"
    echo "Linux version 4.9.118 (stub)" > "$r/proc/version"
    echo "console=ttyS0" > "$r/proc/cmdline"
    echo "12.34 5.67" > "$r/proc/uptime"
    echo "hdzero 16384 0 - Live 0x00000000" > "$r/proc/modules"
    echo "debugfs /sys/kernel/debug debugfs rw 0 0" > "$r/proc/mounts"
    printf ' 0 [audiocodec     ]: audiocodec - audiocodec\n 1 [snddaudio0     ]: snddaudio0 - snddaudio0\n 2 [snddaudio2     ]: snddaudio2 - snddaudio2\n' > "$r/proc/asound/cards"
    echo "  2: [ 0- 0]: digital audio playback" > "$r/proc/asound/devices"
    echo "00-00: audiocodec : audiocodec : playback 1 : capture 1" > "$r/proc/asound/pcm"
    echo "Advanced Linux Sound Architecture Driver Version k4.9.118." > "$r/proc/asound/version"
    for c in 0 1 2; do echo "card$c" > "$r/proc/asound/card$c/id"; done
    echo closed > "$r/proc/asound/card0/pcm0p/sub0/status"
    echo closed > "$r/proc/asound/card1/pcm0p/sub0/hw_params"
    printf 'allwinner,sunxi-daudio\000' > "$r/proc/device-tree/soc@03000000/daudio@0x05090000/compatible"
    printf '\000\000\000\113' > "$r/proc/device-tree/soc@03000000/daudio@0x05090000/pinctrl-0"
    printf 'allwinner,sunxi-daudio0-machine\000' > "$r/proc/device-tree/soc@03000000/sound@1/compatible"
    printf 'PH1\000PH2\000PH3\000' > "$r/proc/device-tree/soc@03000000/pinctrl@0300b000/daudio0@0/allwinner,pins"
    printf '\000\000\000\001' > "$r/proc/device-tree/snddaudo0/snddaudio0_used"
    : > "$r/sys/class/sunxi_dump/dump"
    : > "$r/sys/class/sunxi_dump/write"
    : > "$r/sys/class/gpio/export"
    : > "$r/sys/class/gpio/unexport"
    for g in 131 192 193 194; do : > "$r/sys/class/gpio/gpio$g/value"; : > "$r/sys/class/gpio/gpio$g/direction"; done
    pm="$r/sys/kernel/debug/pinctrl/300b000.pinctrl/pinmux-pins"
    if [ "$2" = claimed ]; then
        for p in 192 193 194; do echo "pin $p (PG$((p - 192))): 4021000.sdmmc (GPIO UNCLAIMED) function sdc1 group PG$((p - 192))" >> "$pm"; done
    else
        for p in 192 193 194; do echo "pin $p (PG$((p - 192))): (MUX UNCLAIMED) (GPIO UNCLAIMED)" >> "$pm"; done
    fi
    echo "pin 225 (PH1): 5001000.uart (GPIO UNCLAIMED) function uart4 group PH1" >> "$pm"
    echo "pin 233 (PH9): twi2 (GPIO UNCLAIMED)" >> "$pm"
    echo "gpiochip0: GPIOs 0-287, parent: platform/300b000.pinctrl, 300b000.pinctrl:" > "$r/sys/kernel/debug/gpio"
    echo " gpio-131 (                    |sysfs               ) out lo" >> "$r/sys/kernel/debug/gpio"
    echo "snd-soc-dummy" > "$r/sys/kernel/debug/asoc/codecs"
    echo "goggle 2 stub" > "$r/mnt/app/version"
    printf '#!/bin/sh\necho started > "%s/tmp/app_started"\n' "$r" > "$r/mnt/app/app/HDZGOGGLE"
    chmod +x "$r/mnt/app/app/HDZGOGGLE"
}

mkbin() { # stub tools; $1 = bin dir, $2 = log file
    b=$1
    l=$2
    mkdir -p "$b"
    for a in hexdump usleep; do
        command -v "$a" > /dev/null 2>&1 || ln -sf "$(command -v busybox)" "$b/$a"
    done
    cat > "$b/i2cget" << STUB
#!/bin/sh
st="$l.\$2.\$3.0f"
case "\$4" in
0x00) echo 0x54 ;; 0x01) echo 0x49 ;; 0x02) echo 0x12 ;; 0x03) echo 0x06 ;;
0x0f) [ -f "\$st" ] && cat "\$st" || echo 0x08 ;;
*) echo 0x00 ;;
esac
STUB
    cat > "$b/i2cset" << STUB
#!/bin/sh
echo "i2cset \$*" >> "$l"
[ "\$4" = 0x0f ] && echo "\$5" > "$l.\$2.\$3.0f"
exit 0
STUB
    printf '#!/bin/sh\necho "i2cdump $*"\necho "     0  1  2  3"\necho "00: 54 49 12 06"\n' > "$b/i2cdump"
    printf '#!/bin/sh\necho "i2c-1 i2c sunxi i2c adapter"\n' > "$b/i2cdetect"
    printf '#!/bin/sh\necho "stub $(basename "$0") $*"\nexit 0\n' > "$b/amixer"
    cp "$b/amixer" "$b/aplay"
    cp "$b/amixer" "$b/arecord"
    printf '#!/bin/sh\necho "Module Size Used"\n[ -n "${STUB_XRADIO:-}" ] && echo "xradio_wlan 1 0"\nexit 0\n' > "$b/lsmod"
    printf '#!/bin/sh\necho "[1.0] sunxi-daudio 5090000.daudio: stub"\necho "[1.1] pinctrl: pin PH1 already requested"\n' > "$b/dmesg"
    printf '#!/bin/sh\n[ -n "${STUB_APP_PID:-}" ] && echo "$STUB_APP_PID"\n[ -n "${STUB_APP_PID:-}" ]\n' > "$b/pidof"
    printf '#!/bin/sh\necho "mount $*" >> "%s"\n' "$l" > "$b/mount"
    chmod +x "$b"/*
}

for sh in dash "busybox sh"; do
    command -v ${sh%% *} > /dev/null 2>&1 || { echo "SKIP $sh"; continue; }
    tag=$(echo "$sh" | tr ' ' _)
    T=$(mktemp -d)
    mkstub "$T/root" free
    mkbin "$T/bin" "$T/i2c.log"
    export PATH="$T/bin:$PATH"

    # 1. probe, develop.sh mode (no app running): part 1, app start, part 2 in background
    PROBE_ROOT="$T/root" PROBE_WAIT=0 $sh "$KIT/hdmi_audio_probe.sh"
    sleep 2
    o="$T/root/mnt/extsd/hdmi_audio_probe_1.txt"
    if [ -s "$o" ] && grep -q "part 2" "$o" && grep -q "PIO port H" "$o" && grep -q "snddaudio0" "$o" \
        && grep -q 'allwinner,pins \[12\] = "PH1|PH2|PH3|"' "$o" && grep -q "(PH1)" "$o" && [ -f "$T/root/tmp/app_started" ]; then
        ok "$sh: probe develop mode (app started, both parts written)"
    else
        bad "$sh: probe develop mode"
        tail -20 "$o"
    fi
    if grep -q "i2cset -y 1 0x4c 0x0f 0x09" "$T/i2c.log" && [ "$(cat "$T/i2c.log.1.0x4c.0f")" = 0x08 ] \
        && [ "$(cat "$T/i2c.log.3.0x49.0f")" = 0x08 ]; then
        ok "$sh: probe restores the ITE bank registers"
    else
        bad "$sh: bank register not restored"
        cat "$T/i2c.log"
    fi
    if grep -q mount "$T/i2c.log"; then bad "$sh: probe called mount under PROBE_ROOT"; else ok "$sh: no mount under test"; fi

    # 2. probe, SSH mode (app running): one pass, pauses the 'app' around bank switching
    sleep 30 &
    fake=$!
    STUB_APP_PID=$fake PROBE_ROOT="$T/root" $sh "$KIT/hdmi_audio_probe.sh"
    o="$T/root/mnt/extsd/hdmi_audio_probe_2.txt"
    if grep -q "app running: pid '$fake'" "$o" && kill -0 $fake 2> /dev/null; then ok "$sh: probe ssh mode"; else bad "$sh: probe ssh mode"; fi
    kill -CONT $fake 2> /dev/null
    kill $fake 2> /dev/null

    # 3. bay pattern: refuses when PG0-2 belong to sdc1
    mkstub "$T/root2" claimed
    PROBE_ROOT="$T/root2" PROBE_FAST=1 $sh "$KIT/bay_gpio_pattern.sh"
    o="$T/root2/mnt/extsd/bay_gpio_1.txt"
    if grep -q "REFUSED: pin 192 is owned by a device" "$o" && [ ! -s "$T/root2/sys/class/gpio/gpio192/direction" ]; then
        ok "$sh: bay pattern refuses claimed pins"
    else
        bad "$sh: bay pattern claimed"
        cat "$o"
    fi
    # refuses with Wi-Fi loaded
    STUB_XRADIO=1 PROBE_ROOT="$T/root" PROBE_FAST=1 $sh "$KIT/bay_gpio_pattern.sh"
    if grep -q "REFUSED: Wi-Fi driver loaded" "$T/root/mnt/extsd/bay_gpio_1.txt"; then ok "$sh: bay pattern refuses with Wi-Fi"; else bad "$sh: bay wifi"; fi
    # runs when free
    PROBE_ROOT="$T/root" PROBE_FAST=1 $sh "$KIT/bay_gpio_pattern.sh"
    o="$T/root/mnt/extsd/bay_gpio_2.txt"
    if grep -q "cycle 3: GPIO 194 high" "$o" && grep -q "done: pins back" "$o" \
        && [ "$(cat "$T/root/sys/class/gpio/gpio193/direction")" = in ] && [ "$(cat "$T/root/sys/class/gpio/gpio194/value")" = 0 ]; then
        ok "$sh: bay pattern runs on free pins and releases them"
    else
        bad "$sh: bay pattern free"
        cat "$o"
    fi

    # 4. active HDMI audio test (stub aplay/arecord), both parts
    sleep 30 &
    fake=$!
    : > "$T/i2c.log"
    printf '0x35517751' > "$T/root/sys/class/sunxi_dump/dump"
    STUB_APP_PID=$fake PROBE_ROOT="$T/root" PROBE_FAST=1 TMPDIR="$T/root/tmp" $sh "$KIT/hdmi_audio_test.sh" < /dev/null > /dev/null
    o="$T/root/mnt/extsd/hdmi_audio_test_1.txt"
    if grep -q "playing tone on hw:snddaudio0" "$o" && grep -q "i2cset -y 1 0x4c 0xce 0x03" "$T/i2c.log" \
        && grep -q "i2cset -y 1 0x4c 0x6d 0x70" "$T/i2c.log" && grep -q "recording 1s from hw:snddaudio2" "$o" \
        && [ "$(cat "$T/i2c.log.1.0x4c.0f")" = 0x08 ] && [ ! -e "$T/root/tmp/hdmi_tone.raw" ]; then
        ok "$sh: hdmi_audio_test both parts (IT66121 sequence sent, bank restored, temp file removed)"
    else
        bad "$sh: hdmi_audio_test"
        cat "$o"
    fi
    kill -CONT $fake 2> /dev/null
    kill $fake 2> /dev/null
    rm -rf "$T"
done

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
