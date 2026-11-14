# Developer scripts (not part of the firmware)

The scripts used to build, run and test this branch on a Linux host (WSL Ubuntu 24.04, as root).  Paths default
to `/root/hdz/...` and can be changed by the environment variables named in each script.  How they fit together:
[docs/status.md](../../docs/status.md).

| script | what |
|---|---|
| `build_all.sh <worktree> [goggle\|goggle2\|boxpro ...]` | the flashable images (`HDZERO_GOGGLE-…-<commit>.bin` + `.sha256`) into `$OUT` (default `/root/hdz/out`), with the toolchain in `$TOOLCHAIN` (default `/root/hdz/toolchain`: Bootlin `armv7-eabihf--musl--stable-2018.02-2`); refuses a worktree on a Windows drive (jffs2 needs the file modes) |
| `emu_build.sh <goggle\|goggle2\|boxpro>` | the SDL emulator build of this worktree (host compiler) into `$EMU_OUT/build_<target>` |
| `emu_run.sh <build dir> <setting.ini\|-> <key script> <shot prefix>` | runs the emulator **sandboxed**: its own Xvfb, a private mount namespace with tmpfs `/mnt` and `/tmp`, uid 65534 without capabilities, every system command (`passwd`, `insmod`, `mount`, `i2cset`, `reboot`, …) replaced by a logging stub; keys from a script, screenshots and logs into `$EMU_OUT/shots` and `$EMU_OUT/logs`.  **Never run the emulator without it**: the app runs the goggle's shell commands on the host, among them the WiFi page's root password change. Needs `Xvfb`, `xdotool`, ImageMagick `import`. |
| `keys/*.txt` | key scripts for `emu_run.sh` (give their absolute path: it reads them after a `cd`): `k <key> [n]` tap (`s` down, `w` up, `d` click, `a` right button), `l <key>` hold 0.9 s, `w <s>` wait, `s <name>` screenshot, `c <name>` copy setting.ini; other lines are ignored (the first line says which goggle the script is for).  Step counts for the main menu of 2d62c16 (from the top: `k w 3` = Audio, `k w 2` = Module Bay, on both goggles) and the Audio page of 24d8ac8 (Goggle 1 16 rows, Goggle 2 17); all checked against screenshots on 2026-11-13.  `check_start.txt` is the start check of docs/custom-build.md; `check_start_boxpro.txt` the same for the BoxPro (`k w 2` = Audio, 17 rows with the HDMI ones since 2026-11-14, no Module Bay; checked against screenshots). |
| `run_live.sh all\|<t_script>` | the live-stream audio suite (`utilities/live_audio_test/run_all.sh`) in private namespaces |
| `run_probe.sh run\|mutants` | the probe kit's tests as an unprivileged uid in its own pid namespace (`utilities/probe/test/run_tests.sh` and `mutants.sh` refuse to run as root); `SHELLS`, `ONLY`, `QUICK`, `ROOTFS` are passed on |

The other suites run directly: `make -C utilities/module_bay_test test` (and `utilities/module_bay_test/mutants.sh`),
`python3 utilities/audio_mixer/test_mixer.py`, `python3 utilities/audio_mixer/mutants.py`,
`python3 utilities/audio_mixer/check_strings.py`, the C tests `sh utilities/probe/test/{it66121_seq,it66021_edid,
pcmbridge,hdmi_boot}_test.sh` and their mutants `utilities/probe/test/c_mutants.sh` (from the git worktree).
