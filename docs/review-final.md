# Final review of the custom build (commit 2989f32)

Independent review of 2026-11-12, done in the emulator, with host tests and by reading the code. Nothing ran
on a real goggle. The reviewer's experiments are under `/root/hdz/review-goggle/` on the machine the review
ran on (screenshots in `emu/shots/`, the audio model in `audio/`, tests in `tests/`).

**Status of the findings: open.**

## Verdicts

**(a) Flash the Goggle 1 image and leave everything at defaults: go.**

- The goggle's own updater accepts `HDZERO_GOGGLE-77-206-9.6.0-rc1-2989f32.bin`: the name check, the tar
  member names and the md5 of app.fex pass. The app image is 5.9 MB; the app partition is 12 MiB. The RX and
  VA FPGA images are byte-identical to the official 9.6.0-rc1 release and to an upstream a85841d build. File
  modes, boot scripts and library dependencies are unchanged. A clean rebuild reproduces the image (only
  `__TIME__` strings differ).
- With Module = None nothing touches PG0-PG2 (GPIO 192-194) or their pin mux: boot, source switches, bay
  power cycles, standby, WiFi on/off, ELRS channel commands and the dial were all traced. The only
  exception is the pin check, which runs only when Check Pins is clicked.
- With HDMI audio off, the IT66121 register sequence and the EDID served to an HDMI source are
  byte-identical to upstream; only a mutex was added.
- It is not exactly stock when untouched: see "Differences from stock" below. The three a pilot notices: a
  new analog RSSI bar on Goggle 1 that is always empty; a main menu that no longer fits on screen, with
  stock entries shifted down one place; probably up to ~2 s of A/V-in sound in the headphones after the SD
  check that runs at boot.

**(b) Use the new Audio page, HDMI audio off: OK.** The routing logic is correct against the codec driver:
an independent model built from `sun8iw16-codec.c` covered all 12,288 combinations of levels, Record
switches, Record Level and context per target, with 0 cases where the page and the ADC disagree and no
source recorded twice. Migration from every old `[record] audio_source` value records the same as stock. The
remaining defects are in the watcher's robustness and in the hint wording.

**(c) HDMI audio (experimental) and Stream Audio.** Stream Audio is fine to try. Keep HDMI audio switched
off until the probe kit has been run on the goggle:

- With HDMI Out Audio on, the record daemon runs its bridge whenever HDMI out is initialised (everything
  except HDMI in). It rewrites PH_CFG0 from user space (read-modify-write through `/sys/class/sunxi_dump`);
  that register also holds UART3 (the ESP32 / ELRS backpack), UART0 and two GPIO outputs. The IT66121 audio
  enable does read-modify-writes of Reg04, which also holds the video and HDCP reset bits: a bad I2C read
  there could put HDMI out video into reset (suspicion).
- With HDMI In Audio on, the EDID drops the "Basic Audio" bit; some sources may then send no audio. The
  picture is safe (checksum maths checked). The change takes effect only after a restart, and so does
  switching it off.
- Stream Audio off: rtspLive behaves as upstream (identical SDP and packets in the tests, compared against
  8364a43, which has the same code as the base). On: the tests pass, but only against stubs and qemu.

**(d) Select a module type after the hardware mod: not yet; fix the checklist first.**

- The software side is sound: the WiFi/SDIO interlock holds in every ordering traced; the pins stay inputs
  until 2.0 s after every bay power-on; the rapidFIRE frames are correct for True-D MOD 1.17 and a real
  rapidFIRE, with ≥ 100 µs per level and gap, guaranteed by absolute-deadline sleeps; a frame cut short
  cannot set a wrong channel.
- `module-bay-mod-safety.md` has unsafe steps (live probing on the SoC's SDIO connector; supply pins
  identified by number at 0.5 A; hot insertion) and a shifter path that does not work as written.
- The official sys_config puts `vcc-pg` on the same regulator as the `vcc18-*` loads. That points to a
  1.8 V PG bank, which would mean the resistor-only mod cannot drive a 5 V True-D and the LM339 shifter
  path is needed (suspicion). Settle it with Check Pins / the pin finder before soldering.

## Confirmed defects

1. **Goggle 1: empty analog RSSI bar in analog mode on an untouched unit (medium, visible).**
   `src/core/osd.c:764-766` and `:963` (from pkendall64's commit ef0ef6d). On Goggle 1 `rtc6715.rssi` is
   never updated, so the 128×16 box always reads 0 and looks like "no signal". Fix: on HDZGOGGLE do not
   create or show the analog RSSI bar; keep `showRXOSD` only for the channel label.
2. **Up to ~2 s of A/V-in audio in the headphones after the record daemon restarts (medium, audible;
   confirmed in code, hardware effect unverified).** When: the automatic SD check after boot, card
   insertion, SD repair/format, DVR playback start. Why: while the line out is off,
   `audio_mixer_routing.c:199-204,227-229` mutes only "Lineout Switch" and keeps LINEIN in the output mixer
   at +6 dB. The record daemon's `AW_MPI_SYS_Init` turns Lineout on at volume 27 (found by disassembling
   libmedia_mpp). The 2 s watcher then mutes it again, with PA clicks. Stock had nothing connected at that
   point, so it was silent. Fix: while the line out is off, also disconnect the output-mixer sources that
   the recording of the mix does not need; re-apply right after `chkfixsd.sh` / `formatsd.sh`; watch every
   250-500 ms.
3. **The main menu no longer fits and gives no hint that it scrolls (medium, usability, untouched unit).**
   Goggle 1 has 20 entries and Goggle 2 has 21. From the top, Goggle 1 hides "Go Sleep!" and Goggle 2 also
   hides Module Bay. Audio is inserted at position 8 (`ui_main_menu.c:324`), which moves the 11 stock
   entries below it down one place: a pilot who counts dial clicks lands on the wrong page. Fix: put Audio
   next to Module Bay at the end, and add a scroll indicator.
4. **Check Pins reads pins that may still be "IO disabled" (medium, module bay).** `softspi_open()`
   (`softspi.c:32-48`) only exports the pins and never sets their direction. After `rc.sh` (PG_CFG0 =
   0x22777777) the pins are in function 7, where the datasheet calls the data register undefined, so the
   first Check Pins after boot can show garbage levels. Afterwards `close()` leaves the pins as GPIO inputs
   (function 0), not "IO disabled" (`module-bay.md:55` says otherwise). Fix: call `softspi_float_all()`
   right after `softspi_open()` in `run_pin_check()` (`module.c:262`).
5. **WiFi starts anyway if the pins are not released within 3 s (low-medium).** `module.c:574-578` only
   logs and lets the WiFi page start WiFi. Every path traced releases within about 15-600 ms. Fix: return
   failure and do not start WiFi, or force the pins to inputs from the caller.
6. **The SDIO check treats a failed register read as "pins free" (low).** `bay_pins_busy()`
   (`module.c:223-234`): if the three sunxi_dump reads disagree (the app's own `awr` / `aww` calls do not
   take the lock) or the read fails, it returns false; the only remaining guard is
   `/sys/module/xradio_wlan`. The 2989f32 lock itself cannot deadlock (non-blocking, at most 1 s, only in
   the worker) and cannot stall the UI (the only UI wait is `module_wifi_changed()`, at most 3 s). Fix:
   treat a failed read as busy, retry after 5 s, log it.
7. **Audio watcher robustness (low).** `audio_mixer.c:243-246`: one failed control read stops watching for
   good, without a log entry. `:289-299`: after 3 failed re-applies it gives up until the user changes
   something. `:197-199`: the return value of `system()` is ignored, so a missing or failing amixer is
   recorded as applied.
8. **Wrong HDMI In Audio hint (low).** "Hear and record HDMI in audio" (`page_audio.c:86`): the HDMI-in
   audio is recorded only with the mix recorded.
9. **Line out in HDMI in (low).** The first headphone-level change turns the line out on even while HDMI
   in is the source (`audio_mixer.c:145-149`).
10. **Brief line contention in SteadyView register reads (low).** DATA is switched back to output while CS
    is still low (`steadyview.c:160-162`), at most 0.7 mA through the 4.7 kΩ. Fix: raise CS first.
11. **L band frequency mismatch with the True-D (low).** The True-D's L1-L8 are 5333-5613 MHz; the
    goggle's MSP table uses 5362-5621 (`elrs.c:58-65`). A VTX set by frequency on L band ends up 29 MHz
    off; by band/channel it is correct. Document it.
12. **ELRS page "Send VTX freq" sends nothing on an untouched Goggle 1 whose last source was analog
    (low).** `elrs.c:512-523` returns silently; upstream sent the HDZero channel. `module-bay.md` mentions
    it, `custom-build.md` does not.

## Suspicions (need the goggle)

- 1.8 V PG bank (see (d)).
- Whether WiFi still starts after module control or Check Pins has released PG0-PG2 (pinctrl must re-apply
  function 2; the docs list this as untested).
- With HDMI audio on: a failed I2C read during the Reg04 read-modify-write could leave HDMI out video in
  reset, and a wrong PH_CFG0 write would cut the ELRS UART.
- The device-tree values for the Line In boost were not checked against the boards.

## Test-harness hazard (not the goggle)

`utilities/probe/test/run_tests.sh`: Ubuntu 24.04's busybox runs its own applets ahead of PATH. In the
busybox_sh pass, run as root, it would call the host's real `i2cset`, `mount` and `kill -STOP` on any
process named HDZGOGGLE. Guard it, or run it only as an unprivileged user.

## Docs that the code contradicts

- `custom-build.md:49` says "Changed on purpose … nothing", yet the differences below exist.
- `custom-build.md:78-81` is stale: it still says the module-bay branch is unmerged ("Type Unknown / Pin
  Control Off").
- `language.h:6`: the comment says ja_jp has 345 entries; it has 378.
- `audio.md:327` ("silent either way") is wrong for the window in defect 2. The `phones_at_boot` trigger at
  `:172-176` is imprecise: HDZero chosen on the Source page also turns the line out on.
- `module-bay.md:55`: after Check Pins the pins stay GPIO inputs, not "IO disabled".
- `live-audio.md` compares against 8364a43, not the base a85841d (equivalent code, but name the base).
- **`module-bay-mod-safety.md`** (every current was re-derived from the ATmega328P and LM339 datasheets
  and checks out; the V536 values could not be fetched independently):
  - Never move a probe on the goggle-side 2×10 connector while the goggle is on. Steps 11, 13, 14 and 16
    and the pin-finder instructions ask for exactly that. Clip leads on with the power off, or measure in
    the socket hole.
  - Step 5 identifies the supply pins by number at a 0.5 A limit: verify GND to the shield first and start
    at 0.1 A.
  - Step 13 inserts the module with the goggle on. Insert it with the goggle off.
  - Step 8 needs the bay board out of the goggle, and must also check against socket pins 4-7 and the
    neighbouring connector pins.
  - Move step 7 (flash MOD 1.17) before steps 5-6.
  - Step 10 loops forever on the shifter path.
  - Step 8's expected values are wrong with the shifter fitted.
  - Steps 13 and 14 have undefined or unreadable stop thresholds.
  - Step 18: "within ~2 s" should be "~6 s" (the SDIO re-check runs every 5 s).
  - Section 5 "LM339 (any maker)": only TI's datasheet allows inputs above VCC with VCC = 0.
  - Add: never plug in the True-D's USB while it sits in a wired bay (1.0 mA, at the design limit).

## Differences from stock on an untouched Goggle 1

1. OSD: the empty analog RSSI bar in analog mode (defect 1).
2. Main menu: two new entries, Audio after Record Option and Module Bay before Go Sleep; the list scrolls.
3. Record Option: the Audio Source row is gone; a note points to the Audio page.
4. setting.ini, first boot: `[mixer]` is written once (16 separate rewrites of the file). Later boots write
   nothing new. `[record] audio_source` is never written again.
5. Codec at boot: one amixer batch sets line out off, ADC from MIC1 only, and LINEIN in the output mixer at
   +6 dB. Stock left the codec as the record daemon's reset had it until the first source switch. Silent
   either way, apart from defect 2.
6. The 2 s watcher undoes the record daemon's mixer resets; stock kept them until the next source switch.
7. DVR playback: both DAC channels, live sources muted, Playback Volume applies. Stock played the left
   channel only, mixed with the A/V monitor.
8. HDMI in: the line-out muxes are left at the L+R sum.
9. WiFi page: a new Stream Audio row on the System page. `[wifi] stream_audio=false` and `record.conf
   [live] audio=0` are written at the WiFi boot action on every boot.
10. Record daemon and rtspLive: at start the daemon writes `/tmp/hdmi_audio.state` and allocates an idle
    second audio encoder (malloc only); live video timestamps are now CLOCK_MONOTONIC (packets identical in
    the test); a killed rtspLive now stops the live encoder within 0.5 s; rtspLive clears its connect flags
    on SIGTERM.
11. MSP in analog mode: GET_BAND_CHAN / GET_FREQ answer 255 / 0 instead of the HDZero channel. Channel
    commands are ignored while the bay is not controlled. "Send VTX freq" and the long-press confirm send
    nothing on Goggle 1.
12. Goggle 1 batch-2 hardware: one extra bay power-on command to the DM5680 at app start (upstream's
    `static bool pwr_last = -1` skipped it; now `bay_power = -1` in `hardware-goggle.c`).
13. settings.c: an `analog_channel` of 0 or more than 48 is reset to R1 (upstream had a bug here that set
    `scan.channel` instead).
14. Small internals: the language table is 512 entries; MAX_PANELS is 17, so each page has 7 more hidden
    objects; a scroll-to-view call runs on every row selection; a module worker thread runs and does
    nothing.
15. Version string: "9.6.0-rc1-2989f32", shown as "9.6.0-rc1-2989f".
16. Goggle 2 only: with Expansion selected and Module None, no analog channel is shown, and the dial and
    ELRS no longer tune the unused built-in receiver.

## Usability rewrites (old → new)

The Audio-page hints fit in about 60 characters; the Module Bay text column holds about 45.

**Audio page**

- "Built-in mic in the headphones. 0 is off" → "Goggle mic, heard in your headphones"
- "Line In jack (external mic) in the headphones. 0 is off" → "Line In jack (ext. mic), heard in headphones"
- "AV in jack or analog module in the headphones. 0 is off" → "AV jack or analog module, heard in headphones"
- Goggle 2: "…module or receiver in the headphones. 0 is off" → "AV jack, module or receiver (needs
  Built-in RX Audio On)"
- New hint on the greyed HDMI In row (make it selectable) → "Off: switch on HDMI In Audio at the bottom"
- "Headphone volume. It does not change the recording" → "Headphone volume. Does not affect recording"
- "Record What I Hear" → "Record Headphone Mix"
- "Yes: records the headphone mix at the levels above" → "Yes: record exactly what you hear above"
- "Recorded at full level, heard or not" → "Recorded even if its headphone level is Off"
- "Not recorded. Yes: at full level, heard or not" → "Not recorded. Yes: record it directly"
- "In What I Hear: recorded at its headphone level" → "Recorded via the mix, at its headphone level".
  While a source goes through the mix, grey out its Yes/No: today a row can show "▶No" and "Mix" while the
  source is recorded.
- "Mic preamp, for the headphones and the recording" → "Mic gain for headphones and recording"
- "Analog RX Audio" → "Built-in RX Audio"
- "HDMI out carries the recorded audio" → "Send the recorded audio to HDMI out"
- "Hear and record HDMI in audio. Restart once after switching on" → "HDMI in audio. Restart the goggle
  after changing", plus an orange "Restart" marker until the next boot.
- Russian: "Микро" → "Микрофон"; "Усиление микро" → "Усиление микрофона".

**WiFi page**

- Russian "Звук трансляции" overlaps the selection arrow → "Звук потока", or widen the label column.
- Add a note: "Stream Audio: sound set on the Audio page".

**Module Bay page**

- "Check Pins" → "Test Wiring"; "Click: read PG0-PG2 as inputs, nothing driven" → "Click to test the 3
  bay wires (read only)"; "PG0 1 PG1 ~ PG2 0" → "Wires 1 1 0 (3.3 V)", with "~" shown as "toggling", plus
  an OK/Fail verdict.
- Switch modes "PG0=1 PG1=2 PG2=4" … → "Wiring A (standard)" … "Wiring F". The sixth list entry is cut off
  over the Status line.
- "Active CH1" (green, shown with nothing connected) → "Sending CH1"; on Goggle 2 with Built-in as the
  source → "Sending CH1 (not video source)".
- "Bay pins not used" → "Off, bay pins untouched"
- "module control active" → "set Module to None first"
- SteadyView on batch-1 hardware, where Mode is greyed with no reason → "Mode needs the bay power switch".
- The Module Bay page content starts about 95 px lower than the stock pages.

## Host test suites (what ran)

- `module_bay_test`: 136/136, twice. It runs the real `module.c`, the module types and `settings.c`. The
  pins are an ideal model and `softspi.c` is never compiled. The True-D receiver models lack MOD 1.17's
  storm guard. 4 extra mutants were all killed.
- `audio_mixer`: `test_mixer.py` 254 checks pass; `mutants.py` 32/32 killed; 4 of 10 extra mutants
  survived (ADC volume never written, HDMI level not setting `phones_at_boot`, unused gains not reset,
  `audio_mixer_set()` without clamping); `check_strings.py`: all fit, but it ignores the measuring
  binary's exit status. The tests run the real `audio_mixer.c` and routing; the codec is `codec_model.py`,
  checked against the driver one way only; the ALSA read-back (`audio_mixer_codec.c`) and amixer are never
  run.
- `live_audio_test`: `run_all.sh` passes (ring 7254 checks; ARM qemu runs of e2e 38, describe 12, kill 15).
  Outside the runner: share 6, avsync 12 and audio_fail 6 pass. The native-live555 tests, including
  `t_off_identical`, were skipped (no LIVE555 tree on the host). Everything runs against MPP stubs. `t_kill`
  is borderline flaky (0.1 s margin under load).
- `probe/test`: the EDID (5), IT66121 sequence (3) and pcmbridge (19) tests pass; `mutants.sh` 13/13
  killed; `run_tests.sh` exits 1: dash 23/23 and static 9/9 pass, busybox_sh fails 13 of 23 (the stub
  bypass in the hazard above). The goggle's own ash was not run.
- Overall: good coverage of the real logic, but every hardware boundary (codec, sysfs GPIO, MPP, I2C) is a
  model written by the same engineers.

## What only the goggle can settle, and the safest order

1. **Before flashing.** Keep the official `HDZERO_GOGGLE-77-206-9.6.0-rc1.bin` and the full recovery
   package `HDZEROGOGGLE_Rev20250319.zip` (from hd-zero.com/document) on the PC. Check the image's SHA-256.
   Put **only** that one `.bin` on the SD root: the menu's substring match also lists `.sha256` files and
   Goggle 2 images, and choosing a non-tar file erases RX/VA first.
2. **Flash from the menu, wait for "all done", reboot.** Then, untouched: HDZero video and OSD; a 1-minute
   DVR recording with the default mic, then play it back; with headphones on, listen during the first 10 s
   after boot (defect 2); switch to analog with the bay module; HDMI out picture; WiFi on, stream, WiFi
   off; ELRS backpack; standby and wake.
3. **The Audio page:** change levels and record each source.
4. **Optional:** `hdmi_audio_probe.sh` (read-only) as `develop.sh`, then the test script. Switch HDMI audio
   on in the menu only if the probe shows the wiring.
5. **Module bay.** Bay empty, Module None, WiFi off: run Check Pins twice (the first read may be garbage,
   defect 4), or run `bay_gpio_pattern.sh`, to learn the PG bank voltage; after Check Pins, switch WiFi on
   and confirm it still connects; then choose resistors (3.3 V) or the shifter (1.8 V), with the corrected
   checklist; then set Module = rapidFIRE and keep WiFi off while flying analog.
6. **Getting back to official firmware.** Flash the official 9.6.0-rc1 `.bin` from the Firmware menu
   (`update_goggle.sh` is unchanged). If the app no longer starts: put `HDZG_OS.bin`, `HDZGOGGLE_RX.bin`
   and `HDZGOGGLE_VA.bin` from the recovery zip on the SD root, power on and wait 5 minutes: u-boot flashes
   them, independent of the app partition (https://docs.hd-zero.com/goggles-firmware-update). PhoenixCard
   is the last resort. After going back, `setting.ini` keeps the harmless `[mixer]` and `[module]` keys;
   stock uses the old `[record] audio_source`, which this firmware never changes, so a source changed on
   the Audio page is lost.
