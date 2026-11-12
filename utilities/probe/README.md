# HDMI audio / pin probe kit (Goggle 1, Goggle 2, BoxPro)

Background and the meaning of the results: [docs/hdmi-audio.md](../../docs/hdmi-audio.md).

The goggle firmware runs `/mnt/extsd/develop.sh` **instead of** the goggle app when
that file exists on the SD card (`mkapp/app/script/rc.sh`). Every script here
starts the goggle app itself, so the goggle works normally while it runs. Delete
or rename `develop.sh` afterwards. With SSH (Wi-Fi) you can instead run the
scripts directly with `sh`, the app keeps running.

| file | what it does | changes |
|---|---|---|
| `hdmi_audio_probe.sh` | **run this first.** Sound cards, mixer controls, device tree, pin mux, GPIOs, I2S/PIO/CCU registers, IT66121 + IT66021 register dumps, dmesg, tool list | nothing persistent (mounts debugfs in RAM if needed; ITE bank register is restored while the app is paused for ~1 s) |
| `hdmi_audio_test.sh` + `tone1k_48k_s16le_2ch.raw` | optional active test: 1 kHz tone to HDMI out, 10 s recording from HDMI in | chip/SoC registers at run time only, restored at the end; a power cycle undoes everything |
| `bay_gpio_pattern.sh` | optional: drives GPIO 192/193/194 (PG0-PG2) one after another so a multimeter can find them on the module-bay contacts | drives pins - **bay empty**; refuses if the pins belong to another function |

## 1. Read-only probe (please always send this)

1. Copy `hdmi_audio_probe.sh` to the SD card root and rename it `develop.sh`.
2. Optional: connect an HDMI display to HDMI out.
3. Boot. The screen stays dark until part 1 is done (up to about a minute), then you hear
   **2 beeps** and the goggle app starts.
4. Leave the goggle on for **2 minutes** (part 2 runs 60 s after the beeps).
   For a second run that also covers HDMI in: connect a source and select HDMI in within
   that minute.
5. Power off, send `hdmi_audio_probe_N.txt`, `hdmi_audio_dmesg_N.txt` (and
   `hdmi_audio_config_N.gz` if it exists) from the SD card. State the goggle model.

SSH alternative: `sh /mnt/extsd/hdmi_audio_probe.sh` (one pass, ~30 s).

## 2. Optional: HDMI audio test

Needs a TV/monitor **with speakers** on HDMI out, and for part B an HDMI source that plays sound.

1. Copy `hdmi_audio_test.sh` as `develop.sh` **and** `tone1k_48k_s16le_2ch.raw` to the SD root.
2. Boot, stay on the HDZero/menu screen (not HDMI in). 45 s after boot: **1 beep**,
   a 1 kHz tone should play on the TV for 15 s. **Note whether you hear it.** 2 beeps = done.
3. You now have 60 s: select **HDMI in** with a source playing sound. **3 beeps**: 10 s recording.
   **4 beeps** = finished.
4. Send `hdmi_audio_test_N.txt` and `hdmi_in_test_N.wav`, plus "tone heard: yes/no".

SSH: `sh hdmi_audio_test.sh out` or `sh hdmi_audio_test.sh in` (asks for Enter between steps).

## 3. Optional: module-bay pin finder (bay EMPTY)

1. Remove any module from the bay. Copy `bay_gpio_pattern.sh` as `develop.sh`. Boot.
2. Measure each bay contact against ground. Pattern (3 rounds): 10 s all low; then
   1 beep -> GPIO 192 (PG0) at 3.3 V for 10 s; 2 beeps -> GPIO 193 (PG1); 3 beeps -> GPIO 194 (PG2).
3. 4 long beeps: finished, pins released, app started. One long beep right after boot
   means **refused** - nothing was driven, the reason is in `bay_gpio_N.txt`.

## Tests on a PC

`sh utilities/probe/test/run_tests.sh` (shellcheck, dash and busybox ash against a stub
/proc + /sys tree; `ROOTFS=<extracted goggle rootfs>` also checks every command exists
on the goggle) and `sh utilities/probe/test/it66121_seq_test.sh` (IT66121 register
sequence unchanged with HDMI audio off).
