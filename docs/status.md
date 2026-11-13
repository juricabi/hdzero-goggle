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
| `hdmi-hardening` | HDMI audio hardening (6 commits, two WIP), **not merged into `all`** |
| `module-control`, `audio`, `hdmi-audio`, `rtsp-audio` | the feature branches as they were merged into `all` (older states; do not continue on them) |
| `main` | upstream a85841d, untouched |

Local worktrees on the developer's machine (WSL): `/root/hdz/goggle-all` (`all`), `/root/hdz/goggle-hdmi`
(`hdmi-hardening`), `/root/hdz/goggle-module`, `/root/hdz/hdzero-goggle` (`main`).

## Where each feature lives

| feature | code | tests | docs |
|---|---|---|---|
| audio mixer | `src/core/audio_mixer.c` (state, watcher, `setting.ini`), `audio_mixer_routing.c` (levels and switches to codec controls), `audio_mixer_codec.c` (ALSA read-back), `src/ui/page_audio.c`; migration from `[record] audio_source` | `utilities/audio_mixer/`: `test_mixer.py` (the real C against `codec_model.py`), `mutants.py`, `check_strings.py` | [audio.md](audio.md) |
| live-stream audio | `src/record/live_audio.c`, `avshare.c`; `src/rtspLive/server/AACLiveAudio*`, `LiveAudioSource`, `LiveNalVideoSource`; WiFi page's Stream Audio | `utilities/live_audio_test/` (`run_all.sh`) | [live-audio.md](live-audio.md) |
| HDMI audio | `src/core/hdmi_audio.c`, `src/driver/it66121.c`, `it66021.c` (EDID), `src/record/pcmbridge*.c` | `utilities/probe/test/` (EDID, IT66121 sequence, pcmbridge) | [hdmi-audio.md](hdmi-audio.md), `utilities/probe/README.md` |
| module bay | `src/module/`: `module.c` (worker thread, the WiFi / SDIO interlock, Check Pins), `softspi.c` (PG0-PG2 through sysfs GPIO), `rapidfire.c`, `steadyview.c`, `steadyview_x.c` (+ `mkapp/app/ko/soft_uart.ko`), `switch.c`, `module_def.c`; `src/ui/page_modulebay.c`; hooks in `elrs.c`, `osd.c`, `settings.c`, `ui_main_menu.c`, `page_wifi.c`, `driver/hardware-*.c` (bay power) | `utilities/module_bay_test/` (`make test`, `mutants.sh`) | [module-bay.md](module-bay.md), [module-bay-mod-safety.md](module-bay-mod-safety.md) (the 3-wire mod) |
| settings, languages | `src/core/settings.c`, `mkapp/app/language/*.ini`, `src/lang/language.h` (table 512) | `check_strings.py` | [custom-build.md](custom-build.md) (every new setting) |

## Build, emulate, test

All on Linux (WSL Ubuntu 24.04, root), worktrees on the Linux file system.  Scripts: [utilities/dev/](../utilities/dev/README.md).

```sh
utilities/dev/build_all.sh /root/hdz/goggle-all goggle    # image + .sha256 -> /root/hdz/out (goggle2, boxpro too)
utilities/dev/emu_build.sh goggle                          # SDL emulator build
utilities/dev/emu_run.sh /root/hdz/emu/build_goggle - utilities/dev/keys/smoke.txt smoke   # always sandboxed
make -C utilities/module_bay_test test                     # ~2 min
python3 utilities/audio_mixer/test_mixer.py                # ~30 s; mutants.py ~10 min; check_strings.py
utilities/dev/run_live.sh all                              # live-stream audio, in private namespaces
utilities/dev/run_probe.sh run                             # probe kit tests, as an unprivileged uid
```

From Git Bash on Windows: write the commands into a script and run `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 --
bash /mnt/c/.../script.sh` (inline `bash -lc '...'` gets `$` and globs mangled).

## Done since the review of 2989f32

Its defects 1-10 (the Goggle 1 RSSI bar, the record daemon's audio leak, the main menu order with a scroll bar and
chevrons, Check Pins inputs first, the WiFi refusal, unreadable registers = busy, the watcher's robustness, the
hint texts, the line out in HDMI in, the SteadyView CS turnaround), its usability rewrites in every language, the
corrected mod checklist, [first-flash.md](first-flash.md), nothing written to `setting.ini` at start-up (8b187fb).
2026-11-13: the docs follow the True-D facts measured that day (below); defects 11 and 12 documented in
custom-build.md; developer scripts in `utilities/dev/`.

## Open, in this order

1. **Merge `hdmi-hardening` into `all`.**  Finish its two WIP commits first (04941cc: the bridge no longer touches
   PH_CFG0; 18b9c6f: the probe reads PH_CFG0 through the sunxi_dump latch and through `/dev/mem`).  `all` already
   has simple versions of `hdmi_audio_out_active()` / `hdmi_audio_in_active()` / `hdmi_audio_out_fault()`, which the
   branch replaces.
2. **Build and test the final `all`**: 8b187fb (the last commit) has not been compiled yet.  Every suite above;
   then the emulator check of custom-build.md ("Checked in the emulator", marked out of date) with `emu_run.sh`;
   the key scripts need new step counts for the menu order of 2d62c16.
3. **The probe harness hazard** (review-final.md): `run_probe.sh` runs it safely; `run_tests.sh` itself could also
   refuse to run as root.
4. **An independent review** of everything after 2989f32 (the 14 commits on `all` and the HDMI branch), then the
   fixes, as for every earlier stage.
5. **Rebuild the images** (`build_all.sh`), record their SHA-256 (first-flash.md uses `<commit>`).

Then, on the goggle (only by its owner, never flashed without asking):

6. **First flash** of the Goggle 1 image, [first-flash.md](first-flash.md) (the official 9.6.0-rc1 image and the
   recovery package kept on the PC), the untouched checks, then the Audio page.
7. Optional: the HDMI audio probe kit (read-only first).
8. **Module bay**: bay empty, Module None, WiFi off: **Check Pins** to learn the PG bank voltage (3.3 V: the
   4.7 kΩ resistor path; 1.8 V: the LM339 path) before any soldering; then the mod by
   [module-bay-mod-safety.md](module-bay-mod-safety.md); then Module = rapidFIRE.
9. After the hardware tests: a release of the images on the fork.

## Facts to keep in mind

- PG0-PG2 are also the WiFi chip's SDIO bus: the goggle never drives them while WiFi runs or the WiFi driver is
  loaded, keeps them inputs until 2.0 s after every bay power-on, and rapidFIRE keeps every level ≥ 100 µs.
- **Furious True-D** (measured on the owner's unit, 2026-11-13; furiousdfw `docs/bay-standards.md`): bay pin 1 =
  D11, pin 2 = D12, pin 3 = D13; no series resistors on the module; MOD 1.17 released (`BAY:RAPIDFIRE` for this
  goggle, never drives a pin; a band or channel counts when two of the three copies agree; `BAY CLK` stays D11);
  the stock bootloader's 0.1 s 5 V pulse is on **bay pin 3** (the owner's unit has the no-LED bootloader); for the
  Switch type with `BAY:1-8` use **Wiring A (standard)**.  Keep the 4.7 kΩ resistors in the mod anyway.
- The Orqa FPV.One is not this firmware's business, but the True-D's `BAY:ORQA` exists for it; never use `ORQA` in
  the HDZero.
