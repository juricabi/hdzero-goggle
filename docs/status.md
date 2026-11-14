# Status of the custom build, and how to continue (2026-11-14)

What this branch adds to HDZero's goggle firmware 9.6.0-rc1 (upstream a85841d): the **audio mixer** (Audio page),
**live-stream audio**, **HDMI audio** (experimental, default off) and **module bay control** (analog receiver
modules in the expansion bay: rapidFIRE, SteadyView, SteadyView X, Switch).  **Nothing of it has run on a real
goggle yet.**  The last independent review is [review-2026-11-13.md](review-2026-11-13.md) (of 2989f32..1087d5c;
before it [review-final.md](review-final.md), of 2989f32); this page says what happened since and what is left.

## Branches (fork https://github.com/juricabi/hdzero-goggle)

| branch | state |
|---|---|
| `all` | the integration branch: everything merged, the images are built from it.  Work goes here. |
| `hdmi-hardening` | HDMI audio hardening: its 6 commits and fea1e13 (the second WIP finished) **merged into `all`** in 0663ddf (2026-11-13); do not continue on it |
| `module-control`, `audio`, `hdmi-audio`, `rtsp-audio` | the feature branches as they were merged into `all` (older states; do not continue on them) |
| `main` | upstream a85841d, untouched |

Local worktrees on the developer's machine (WSL): `/root/hdz/goggle-all` (`all`), `/root/hdz/goggle-hdmi`
(`hdmi-hardening`), `/root/hdz/goggle-module`, `/root/hdz/hdzero-goggle` (`main`).

## Where each feature lives

| feature | code | tests | docs |
|---|---|---|---|
| audio mixer | `src/core/audio_mixer.c` (state, watcher, `setting.ini`), `audio_mixer_routing.c` (levels and switches to codec controls), `audio_mixer_codec.c` (ALSA read-back), `src/ui/page_audio.c`; migration from `[record] audio_source` | `utilities/audio_mixer/`: `test_mixer.py` (the real C against `codec_model.py`), `mutants.py`, `check_strings.py` | [audio.md](audio.md) |
| live-stream audio | `src/record/live_audio.c`, `avshare.c`; `src/rtspLive/server/AACLiveAudio*`, `LiveAudioSource`, `LiveNalVideoSource`; WiFi page's Stream Audio | `utilities/live_audio_test/` (`run_all.sh`) | [live-audio.md](live-audio.md) |
| HDMI audio | `src/core/hdmi_audio.c`, `src/driver/it66121.c` (Reg04 guard), `it66021.c` (EDID), `src/record/pcmbridge*.c`, the boot step `src/boot/hdmi_audio_boot.c` + `src/record/hdmi_boot.c` (run by `rc.sh`), `src/record/soc_reg.c` (the shared `sunxi_dump` access; `system_exec_sunxi_dump()` for the app's `aww`/`awr`) | `utilities/probe/test/` (EDID, IT66121 sequence, pcmbridge, hdmi_boot; the probe kit) | [hdmi-audio.md](hdmi-audio.md), `utilities/probe/README.md` |
| module bay | `src/module/`: `module.c` (worker thread, the WiFi / SDIO interlock, Check Pins), `softspi.c` (PG0-PG2 through sysfs GPIO), `rapidfire.c`, `steadyview.c`, `steadyview_x.c` (+ `mkapp/app/ko/soft_uart.ko`), `switch.c`, `module_def.c`; `src/ui/page_modulebay.c`; hooks in `elrs.c`, `osd.c`, `settings.c`, `ui_main_menu.c`, `page_wifi.c`, `driver/hardware-*.c` (bay power) | `utilities/module_bay_test/` (`make test`, `mutants.sh`) | [module-bay.md](module-bay.md), [module-bay-mod-safety.md](module-bay-mod-safety.md) (the 3-wire mod) |
| settings, languages | `src/core/settings.c`, `mkapp/app/language/*.ini`, `src/lang/language.h` (table 512) | `check_strings.py` | [custom-build.md](custom-build.md) (every new setting) |

## Build, emulate, test

All on Linux (WSL Ubuntu 24.04, root), worktrees on the Linux file system.  Scripts: [utilities/dev/](../utilities/dev/README.md).

```sh
utilities/dev/build_all.sh /root/hdz/goggle-all goggle    # image + .sha256 -> /root/hdz/out (goggle2, boxpro too)
utilities/dev/emu_build.sh goggle                          # SDL emulator build
utilities/dev/emu_run.sh /root/hdz/emu/build_goggle - $PWD/utilities/dev/keys/check_start.txt g1_none   # always sandboxed; absolute key path
make -C utilities/module_bay_test test                     # ~2 min; utilities/module_bay_test/mutants.sh ~5 min
python3 utilities/audio_mixer/test_mixer.py                # ~30 s; mutants.py ~10 min; check_strings.py
utilities/dev/run_live.sh all                              # live-stream audio, in private namespaces
utilities/dev/run_probe.sh run                             # probe kit tests (~10 min), as an unprivileged uid; also: mutants (~45 min)
sh utilities/probe/test/it66121_seq_test.sh                # and it66021_edid_test.sh, pcmbridge_test.sh, hdmi_boot_test.sh
utilities/probe/test/c_mutants.sh                          # mutants of the C safety code against those and module_bay_test (~11 min)
```

`utilities/probe/test/run_tests.sh` and `mutants.sh` refuse to run as root; from a root shell always use
`run_probe.sh`.

From Git Bash on Windows: write the commands into a script and run `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 --
bash /mnt/c/.../script.sh` (inline `bash -lc '...'` gets `$` and globs mangled).

## Done since the review of 2989f32

Its defects 1-10 (the Goggle 1 RSSI bar, the record daemon's audio leak, the main menu order with a scroll bar and
chevrons, Check Pins inputs first, the WiFi refusal, unreadable registers = busy, the watcher's robustness, the
hint texts, the line out in HDMI in, the SteadyView CS turnaround), its usability rewrites in every language, the
corrected mod checklist, [first-flash.md](first-flash.md), nothing written to `setting.ini` at start-up (8b187fb).
2026-11-13: the docs follow the True-D facts measured that day (below); defects 11 and 12 documented in
custom-build.md; developer scripts in `utilities/dev/`.

Later on 2026-11-13 (all of it on the PC only: host tests against models, the emulator):

- **`hdmi-hardening` merged** (0663ddf).  Its WIP 04941cc was already completed by 70b6472; the WIP 18b9c6f (probe
  kit: PH_CFG0 through the latch and through `/dev/mem`) was finished on the branch in fea1e13 (tests, mutants,
  and the file modes its Windows commits had lost, `rc.sh` among them).  The review's HDMI concerns: nothing writes
  PH_CFG0 at run time any more (only `rc.sh` and the once-per-boot step `hdmi_audio_boot`, from the exact stock
  value to one constant, only with `[hdmi] audio_out=1`); the IT66121 Reg04 is written only as constants that keep
  the video/HDCP/RCLK resets as the stock init leaves them; the Audio page's HDMI switches apply at the next start;
  BoxPro had no HDMI audio (24d8ac8: no HDMI rows on its Audio page; changed on 2026-11-14, below).  Design:
  [hdmi-audio.md](hdmi-audio.md).
- **The probe harness hazard** (750de50): `run_tests.sh` and `mutants.sh` refuse root; in the busybox passes the
  stubs are shell functions ahead of busybox's applets, and a shell under which any stub would be bypassed is not
  used (FAIL).  The busybox pass, which failed 13 of 23 checks before (also under `run_probe.sh`), now passes.
- **Built and tested** (results below); the emulator check of custom-build.md redone (934e509), the key scripts in
  `utilities/dev/keys` fixed for the menu of 2d62c16 and checked against screenshots.
- **Test fixes** (4c285fb and the commit after it): the audio mixer's mutants had not run since 8b187fb; mutants
  that did not build or no longer applied are fixed, and three checks were added where mutants survived ("nothing
  written at start-up" now also catches a rewrite with the same text; the DAC with the line out off and no mix; a
  recheck during a backoff).  The probe kit's `mutants.sh` garbled one mutant (dash's `echo`); the module bay
  `mutants.sh` printed the compiler command as the reason.  No firmware code changed for these.
- **The independent review of 2989f32..1087d5c** ([review-2026-11-13.md](review-2026-11-13.md)): Goggle 1 image at
  defaults GO; Audio page with HDMI off GO; HDMI audio on NO-GO until the read-only probe has run on the goggle;
  a module type after the mod CONDITIONAL (checklist and Check Pins first); Goggle 2 and BoxPro images at defaults
  GO (added on 2026-11-14 after comparing them with upstream builds and their start in the emulator; Goggle 2
  module-bay control and HDMI audio not reviewed).  Its findings are fixed: the pin
  finder drives only the pin under test (420a8a8); `utilities/probe/test/c_mutants.sh`, the checks that kill its
  survivors, time limits, the IT66121/IT66021 tests against a85841d also outside git (f68fc76); "Paused, pin state
  unknown" on the Module Bay page and the module bay test's own lock file (766f035); the doc errors and notes
  (9563dad, 420a8a8).  `run_probe.sh` takes a sandbox directory of its own for each run: two runs at once (the
  review's in another worktree and this one) had shared `/tmp/probe-test-mutants` and garbled each other's
  mutants run.

2026-11-14:

- **HDMI audio on the BoxPro** (51bf28a), with the goggles' status: experimental, off by default, in effect from
  the next start, **NO-GO until the read-only probe has run on the BoxPro**.  Its official package
  `HDZEROBOXPRO_Rev20250715.zip` (SHA-256 351397af...892419e) has the Goggle 1's `sys_config.fex` and device tree
  byte for byte and the same `rc.sh` register writes (hdmi-audio.md, section 2); its board wiring is as unknown
  as the goggles'.  `hdmi_boot_supported()` is true on all three targets (false only for a build for no known
  target), the Audio page has the HDMI rows on every target again; every HDMI audio C test runs for each target.
  At defaults the BoxPro's boot step still reads and writes no register.  Not reviewed.

Results on `all` with the BoxPro's HDMI audio (the firmware code of 51bf28a; later commits change only docs and a
key script), all on the PC:

| what | result |
|---|---|
| images, `build_all.sh` (goggle, goggle2, boxpro) | built from the commit that records these results, no compiler warnings besides the known `liblog.so` one; the SHA-256 are in the `.sha256` files next to them |
| emulator builds and the start checks of custom-build.md (5 starts on each of the three targets) | all three build without warnings; in all 15 starts `setting.ini` unchanged at start-up, by the Audio page and (goggles) by Module Bay, no `[mixer]`/`[hdmi]`/`[module]` written, the old Audio Source taken over (mic, line, AV); Module Bay shows "Off, bay pins untouched"; the BoxPro's Audio page has the HDMI In row greyed and HDMI Out/In Audio Off, its `setting.ini` after a start without one identical to upstream's BoxPro build; no crash |
| `make -C utilities/module_bay_test test` | 166 pass (15, 17, 87, 3, 44) |
| `utilities/module_bay_test/mutants.sh` | 11 of 11 killed |
| `test_mixer.py` / `check_strings.py` | 358 pass (every scenario, HDMI-in audio included, for each target) / 153 strings in 6 languages fit, the BoxPro layout with the HDMI rows |
| `utilities/audio_mixer/mutants.py` | 50 of 50 killed |
| `run_live.sh all` | all pass (ring buffer 7254 checks native and under qemu-arm, `t_aidev.sh` 8); the ARM runs (`RTSPLIVE=arm`) not repeated: nothing they run changed since 1087d5c |
| `run_probe.sh run` | 65 pass |
| `run_probe.sh mutants` | 18 of 18 killed |
| `it66121_seq_test.sh`, `it66021_edid_test.sh`, `pcmbridge_test.sh`, `hdmi_boot_test.sh` | 12, 5, 78, 352 pass: the IT66121 sequence with audio off identical to a85841d and audio on checked for each target; pcmbridge 26 and hdmi_boot 117 checks for each of the three targets (every refuse path: PH_CFG0 not the stock value -> refused, nothing written), plus a build for no known target that touches nothing; on a copy outside git the first two say SKIP (exit 77), with `BASE_SRC` 12 and 5 pass |
| `utilities/probe/test/c_mutants.sh` | 24 mutants: 23 killed, 1 equivalent (the Reg04 read-modify-write); new: the BoxPro left out of `hdmi_boot_supported()` again, any target supported |

Before 51bf28a (at 2420218, BoxPro without HDMI audio) the same suites gave: module_bay_test 166, its mutants
11/11, test_mixer 358, check_strings 153 strings, mixer mutants 50/50, `run_live.sh all` pass, `run_probe.sh run`
65, its mutants 18/18, the C tests 10, 5, 26, 118 (`pcmbridge_test.sh` and `hdmi_boot_test.sh` for the Goggle 1
build only, plus one "not supported" check for the BoxPro), `c_mutants.sh` 22 (21 killed, 1 equivalent).

Before the review (1087d5c) the same suites gave: module_bay_test 160, its mutants 11/11, test_mixer 358,
check_strings 152 strings, mixer mutants 49 of 50 in the full run (the survivor killed by a check added
afterwards), `run_live.sh all` and the ARM runs (`RTSPLIVE=arm`: describe 12, kill 15, share 6, e2e 38, avsync 12,
audio_fail 6) pass, `run_probe.sh run` 65, its mutants 16/16, the C tests 10, 5, 26, 117.

Everything at a hardware boundary (codec, I2S, IT66121/IT66021, sunxi_dump, sysfs GPIO, MPP) is a model written by
the same engineers; nothing here replaces the goggle.

## Open, in this order

1. Optional: a look by a reviewer at the fixes of the review of 2026-11-13 and the BoxPro's HDMI audio
   (766f035..HEAD).
2. The images of the commit that records these results are in `/root/hdz/out` on the developer's machine (not
   released); first-flash.md uses `<commit>`.  Rebuild with `build_all.sh` after any further change.

Then, on the goggle (only by its owner, never flashed without asking):

3. **First flash** of the Goggle 1 image, [first-flash.md](first-flash.md) (the official 9.6.0-rc1 image and the
   recovery package kept on the PC), the untouched checks, then the Audio page.
4. Optional: the HDMI audio probe kit on the Goggle 1, Goggle 2 or BoxPro (read-only first; it now also reads
   PH_CFG0 through `/dev/mem` and shows the boot record `/tmp/hdmi_audio.boot`).  HDMI Out / In Audio stay Off
   until the probe has shown the wiring of that device (the review of 2026-11-13: NO-GO before that; the same
   for the BoxPro); switched on, they apply after a restart.  Only the device can tell:
   the PH_CFG0 value `rc.sh` really leaves (the boot step refuses anything but 0x35517751), the `sunxi_dump` answer
   format, whether Reg04 reads 0x14 after the stock init, and whether PH1-PH3 reach the IT66121.
5. **Module bay**: bay empty, Module None, WiFi off: **Check Pins** to learn the PG bank voltage (3.3 V: the
   4.7 kΩ resistor path; 1.8 V: the LM339 path) before any soldering ("? V": the pin finder, only as in checklist
   step 4, one clip per boot attached with the goggle off); then the mod by
   [module-bay-mod-safety.md](module-bay-mod-safety.md); then Module = rapidFIRE (the review of 2026-11-13:
   CONDITIONAL on the checklist and Check Pins).
6. After the hardware tests: a release of the images on the fork.

## Facts to keep in mind

- PG0-PG2 are also the WiFi chip's SDIO bus: the goggle never drives them while WiFi runs or the WiFi driver is
  loaded, keeps them inputs until 2.0 s after every bay power-on, and rapidFIRE keeps every level ≥ 100 µs.
- PH_CFG0 also holds UART3 (the ESP32 / ELRS backpack), UART0 and two reset GPIO outputs: nothing may change it at
  run time.  HDMI Out Audio switches PH1-PH3 only in the boot step, only from the exact stock value; with
  `[hdmi] audio_out` / `audio_in` not exactly `1` (the default) nothing reads or writes PH_CFG0 or any pin function.
- **Furious True-D** (measured on the owner's unit, 2026-11-13; furiousdfw `docs/bay-standards.md`): bay pin 1 =
  D11, pin 2 = D12, pin 3 = D13; no series resistors on the module; MOD 1.17 released (`BAY:RAPIDFIRE` for this
  goggle, never drives a pin; a band or channel counts when two of the three copies agree; `BAY CLK` stays D11);
  the stock bootloader's 0.1 s 5 V pulse is on **bay pin 3** (the owner's unit has the no-LED bootloader); for the
  Switch type with `BAY:1-8` use **Wiring A (standard)**.  Keep the 4.7 kΩ resistors in the mod anyway.
- The Orqa FPV.One is not this firmware's business, but the True-D's `BAY:ORQA` exists for it; never use `ORQA` in
  the HDZero.
