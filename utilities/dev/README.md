# Developer scripts (not part of the firmware)

The scripts used to build, run and test this branch on a Linux host (WSL Ubuntu 24.04, as root).  Paths default
to `/root/hdz/...` and can be changed by the environment variables named in each script.  How they fit together:
[docs/status.md](../../docs/status.md).

| script | what |
|---|---|
| `build_all.sh <worktree> [goggle\|goggle2\|boxpro ...]` | the flashable images (`HDZERO_GOGGLE-…-<commit>.bin` + `.sha256`) into `$OUT` (default `/root/hdz/out`), with the toolchain in `$TOOLCHAIN` (default `/root/hdz/toolchain`: Bootlin `armv7-eabihf--musl--stable-2018.02-2`); refuses a worktree on a Windows drive (jffs2 needs the file modes) |
| `emu_build.sh <goggle\|goggle2\|boxpro>` | the SDL emulator build of this worktree (host compiler) into `$EMU_OUT/build_<target>` |
| `emu_run.sh <build dir> <setting.ini\|-> <key script> <shot prefix>` | runs the emulator **sandboxed**: its own Xvfb, a private mount namespace with tmpfs `/mnt` and `/tmp`, uid 65534 without capabilities, every system command (`passwd`, `insmod`, `mount`, `i2cset`, `reboot`, …) replaced by a logging stub; keys from a script, screenshots and logs into `$EMU_OUT/shots` and `$EMU_OUT/logs`.  **Never run the emulator without it**: the app runs the goggle's shell commands on the host, among them the WiFi page's root password change. Needs `Xvfb`, `xdotool`, ImageMagick `import`. |
| `keys/*.txt` | key scripts for `emu_run.sh`: `k <key> [n]` tap, `l <key>` hold 0.9 s, `w <s>` wait, `s <name>` screenshot, `c <name>` copy setting.ini.  Written for 2989f32's main menu; Audio and Module Bay moved to the end of the menu in 2d62c16, so adjust the step counts before reuse. |
| `run_live.sh all\|<t_script>` | the live-stream audio suite (`utilities/live_audio_test/run_all.sh`) in private namespaces |
| `run_probe.sh run\|mutants` | the probe kit's tests as an unprivileged uid (`utilities/probe/test/run_tests.sh` is not safe as root) |

The other suites run directly: `make -C utilities/module_bay_test test` (and `utilities/module_bay_test/mutants.sh`),
`python3 utilities/audio_mixer/test_mixer.py`, `python3 utilities/audio_mixer/mutants.py`,
`python3 utilities/audio_mixer/check_strings.py`.
