# Status of the custom build, and how to continue (2026-11-13)

What this branch adds to HDZero's goggle firmware 9.6.0-rc1 (upstream a85841d): the **audio mixer** (Audio page),
**live-stream audio**, **HDMI audio** (experimental, default off) and **module bay control** (analog receiver
modules in the expansion bay: rapidFIRE, SteadyView, SteadyView X, Switch).  **Nothing of it has run on a real
goggle yet.**  The last independent review is [review-final.md](review-final.md) (of 2989f32); this page says what
happened since and what is left.

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
  BoxPro has no HDMI audio (24d8ac8: no HDMI rows on its Audio page).  Design: [hdmi-audio.md](hdmi-audio.md).
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

Results on `all` (the firmware code of 24d8ac8; later commits change only tests, scripts and docs), all on the PC:

| what | result |
|---|---|
| images, `build_all.sh` (goggle, goggle2, boxpro) | built, no compiler warnings besides the known `liblog.so` one; the SHA-256 are in the `.sha256` files next to them |
| emulator builds (`emu_build.sh goggle`, `goggle2`) | no warnings |
| emulator check (custom-build.md), 10 starts + every key script | as described there; nothing written to `setting.ini` at start-up or by opening the Audio / Module Bay pages |
| `make -C utilities/module_bay_test test` | 160 checks pass (15 + 17 + 81 + 3 + 44) |
| `utilities/module_bay_test/mutants.sh` | 11 of 11 killed |
| `test_mixer.py` / `check_strings.py` | 358 checks pass / 152 strings × 6 languages fit |
| `utilities/audio_mixer/mutants.py` | 50 mutants: the full run killed 49; the survivor (recheck ignored) is killed by a check added afterwards (with the other 7 watcher mutants, run again).  Before: it had not run since 8b187fb and stopped at its 16th mutant (one survivor, one that did not build); three mutants that did not build, one that no longer applied and three checks the survivors showed missing were fixed in 4c285fb and the commit after it |
| `run_live.sh all` | ring 7254 checks (x86 and ARM/qemu), `t_aidev` 8/8; the native-live555 tests and `t_off_identical` skipped (no LIVE555 tree on this PC) |
| `run_live.sh t_*.sh` with `RTSPLIVE=arm` (the cross-built rtspLive under qemu) | describe 12, kill 15, share 6, e2e 38, avsync 12, audio_fail 6, `t_arm` (e2e, describe, kill) all pass |
| `run_probe.sh run` | 65 pass, 0 fail (static 9, dash 28, busybox ash 28); before: 13 busybox-pass failures |
| `run_probe.sh mutants` | 16 of 16 killed |
| `it66121_seq_test.sh`, `it66021_edid_test.sh`, `pcmbridge_test.sh`, `hdmi_boot_test.sh` | 10, 5, 26, 117 pass |

Everything at a hardware boundary (codec, I2S, IT66121/IT66021, sunxi_dump, sysfs GPIO, MPP) is a model written by
the same engineers; nothing here replaces the goggle.

## Open, in this order

1. **An independent review** of everything after 2989f32: the 14 commits on `all` up to 49b6388, the merged
   `hdmi-hardening` (3cfc2ff..fea1e13) and the commits of this session (0663ddf..HEAD), then the fixes, as for
   every earlier stage.  Worth a close look: `src/record/hdmi_boot.c` (the boot step and its record),
   `src/driver/it66121.c` (the Reg04 guard), `src/util/system.c` (every `aww`/`awr` of the app now under a lock),
   `utilities/probe/test/run_tests.sh` (the busybox shims).
2. **Rebuild the images** after the review's fixes (`build_all.sh`) and record their SHA-256 (first-flash.md uses
   `<commit>`).  Until then the newest images are those of the commit that added these results (built after it,
   in `/root/hdz/out` on the developer's machine, not released).

Then, on the goggle (only by its owner, never flashed without asking):

3. **First flash** of the Goggle 1 image, [first-flash.md](first-flash.md) (the official 9.6.0-rc1 image and the
   recovery package kept on the PC), the untouched checks, then the Audio page.
4. Optional: the HDMI audio probe kit (read-only first; it now also reads PH_CFG0 through `/dev/mem` and shows the
   boot record `/tmp/hdmi_audio.boot`).  HDMI Out / In Audio stay Off until the probe shows the wiring; switched
   on, they apply after a restart.  Only the goggle can tell: the PH_CFG0 value `rc.sh` really leaves (the boot
   step refuses anything but 0x35517751), the `sunxi_dump` answer format, whether Reg04 reads 0x14 after the stock
   init, and whether PH1-PH3 reach the IT66121.
5. **Module bay**: bay empty, Module None, WiFi off: **Check Pins** to learn the PG bank voltage (3.3 V: the
   4.7 kΩ resistor path; 1.8 V: the LM339 path) before any soldering; then the mod by
   [module-bay-mod-safety.md](module-bay-mod-safety.md); then Module = rapidFIRE.
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
