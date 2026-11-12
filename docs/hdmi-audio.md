# HDMI audio on the HDZero goggles - findings, experimental code, probe

Status: **experimental, everything default off.** Nothing here has been run on a
goggle yet. With the default `setting.ini` the firmware behaves exactly as before
(the IT66121 register sequence is verified identical, see
`utilities/probe/test/it66121_seq_test.sh`).

Applies to the original **HDZero Goggle** (`-DHDZ_GOGGLE=ON`) and the **Goggle 2**
(`-DHDZ_GOGGLE2=ON`); BoxPro builds the same code.

## 1. Short answer

| | HDMI out audio (IT66121) | HDMI in audio (IT66021) |
|---|---|---|
| chip | supports 2-ch L-PCM via I2S (SCK, WS, I2S0; MCLK optional) or S/PDIF | outputs I2S (SCK, WS, I2S0, I2S1) / S/PDIF, MCLK |
| SoC controller reserved for it in the shipped device tree | **I2S0** `daudio@0x05090000`, SoC = clock master, pins PH1/PH2/PH3 only (TX direction) - ALSA card `snddaudio0` | **I2S2** `daudio@0x05092000`, SoC = clock slave, pins PG10/PG11/PG13 only (RX direction) - ALSA card `snddaudio2` |
| wiring on the board | **likely** (pin groups trimmed to exactly the three signals each chip has; not documented) | **likely** (same argument; vendor note records from `hw:snddaudio2`) |
| shipped firmware | audio never programmed (Reg04 audio resets stay asserted); **rc.sh re-muxes PH1 to uart4 and PH2/PH3 off at boot** | EDID advertises L-PCM 2 ch 32/44.1/48 kHz; nothing captures I2S2 |
| what this branch adds (off by default) | `[hdmi] audio_out=1`: IT66121 audio setup + the DVR audio input copied to I2S0 | `[hdmi] audio_in=1`: I2S2 copied to the codec DAC / line-out jack while HDMI in is shown; EDID narrowed to 48 kHz |
| not possible / not done | - | HDMI in -> HDMI out (the app switches HDMI out off while HDMI in is shown); HDMI in audio into the DVR file (outline in section 6) |
| only the device can tell | whether PH1-PH3 reach the IT66121 (G1 and G2 separately), whether the TV plays the tone | whether the IT66021 drives SCK/WS by default and at which word clock |

Both goggles ship **the same device tree and sys_config** (byte-identical), so the
expected answer is the same for G1 and G2; the board wiring and the Goggle-2-only
FPGA register 0x8A (written by `IT66121_init/close`) are the parts that can differ.

## 2. What the official OS images contain

Images: Goggle 1 `HDZEROGOGGLE_Rev20250319.zip` and Goggle 2 `HDZEROGOGGLE2_Rev20250715.zip`
from www.hd-zero.com, file `Recovery/HDZG_OS.bin` each.

* Layout: Allwinner PhoenixSuit image `IMAGEWTY` v3 (unencrypted, 34 parts). The runtime
  device tree is item `dtb` of `boot_package_nor.fex` (sunxi-package: scp, optee, u-boot,
  soc-cfg, dtb; 90512 bytes, identical to `sunxi.fex`); `sys_config.fex` is the text
  source; `boot.fex` is a legacy uImage holding an ARM zImage with an LZO payload
  (Linux 4.9.118, G1 build #43 2025-03-19, G2 build #69 2025-07-15); `rootfs.fex` is
  squashfs, `app.fex` jffs2 (= `/mnt/app`).
* G1 vs G2: `sunxi.fex`, `boot_package` dtb, `sys_config.fex`, `config.fex`, boot0 are
  **identical**. Kernels differ only in build; the set of sound-driver strings is identical.
  Root file systems are the same apart from rebuilt libraries. App partitions differ in
  HDZGOGGLE/record/rtspLive and update scripts; `script/rc.sh` is the same.
* No `IKCONFIG` in either kernel (so `/proc/config.gz` is expected to be absent). All sound
  drivers are built in (`/lib/modules/4.9.118` has only sunxi-wlan, vin, hdzero modules).
  No `/etc/asound.conf`: ALSA `default` = card 0.

### Sound devices in the device tree (both goggles)

| DT node | compatible | parameters | ALSA card |
|---|---|---|---|
| `codec@0x05096000` + `i2s-controller@0x05096000`, `sound@0` | sunxi-internal-codec / -i2s, codec-machine | `aif2config=0`, `aif3config=0`, `gpio-spk` = PG12 | `audiocodec` (card 0, used by DVR and line out) |
| `daudio@0x05090000`, `sound@1` | sunxi-daudio, daudio0-machine | `daudio_master=4` (CBS_CFS: SoC drives BCLK/LRCK), `audio_format=1` (I2S), `signal_inversion=1`, `tdm_config=1` (I2S), `pcm_lrck_period=32`, `slot_width_select=32`, `word_select_size=16`, `mclk_div=0` (no MCLK), `tdm_num=0`, pins `daudio0@0` = **PH1 PH2 PH3** function `i2s0` (muxsel 3) | `snddaudio0` |
| `daudio@0x05091000`, `sound@2` | sunxi-tdmhdmi, sunxi-hdmi-machine | SoC-internal HDMI audio | none: the kernel has no `sunxi-hdmi-machine` driver |
| `daudio@0x05092000`, `sound@3` | sunxi-daudio, daudio2-machine | `daudio_master=1` (CBM_CFM: the external chip drives BCLK/LRCK), `audio_format=1`, `signal_inversion=0`, `tdm_config=1`, 32/32/16 as above, `tdm_num=2`, pins `daudio2@0` = **PG10 PG11 PG13** function `i2s2` | `snddaudio2` |
| `dmic-controller@0x05095000`, `sound@4` | dmic | `status = "disabled"` | - |

Card names come from the sunxi snddaudio machine driver (`"snddaudio"` + `tdm_num`).
The vendor notes in the repo use exactly `hw:audiocodec` and `hw:snddaudio2`.

### Pins

Pin functions recovered from the kernel's pinctrl table (function number: name):

| pin | functions | used for |
|---|---|---|
| PH0 | 3:i2s0 5:uart4 | GPIO 224 = `GPIO_HDZ_RX_RESET` |
| PH1, PH2, PH3 | 2:aif2 3:i2s0 5:uart4 | DT `daudio0@0` (I2S0) |
| PH4 | 2:aif2 3:i2s0 | GPIO 228 = `GPIO_HDZ_TX_RESET` |
| PG0 - PG5 | 2:sdc1 | DT `sdc1` (SDIO, `[wlan] wlan_busnum=1`), GPIO 192-194 = module bay (`src/core/defines.h`) |
| PG8, PG9 | - | Wi-Fi `wlan_regon`, `wlan_hostwake` |
| PG9 | 3:i2s2 | (only i2s2 function without aif3: MCLK by the usual order) |
| PG10, PG11, PG13 | 2:aif3 3:i2s2 | DT `daudio2@0` (I2S2) |
| PG12 | 2:aif3 3:i2s2 | codec `gpio-spk` (speaker amplifier enable) |

By the usual Allwinner order (MCLK, BCLK, LRCK, DOUT, DIN; PH0/PG9 have no `aif`
function, as MCLK pins) the trimmed groups are **I2S0 = BCLK, LRCK, DOUT** and
**I2S2 = BCLK, LRCK, DIN**. The pins left out of each group are exactly the ones the
board uses for something else (two reset GPIOs, the speaker GPIO). That, plus master/slave
direction, matches the ITE chips: IT66121 audio inputs are `SCK/MCLK`, `WS`, `I2S0..3`
(IT66121FN datasheet p.6; "MCLK input is optional", p.11) and the IT66021 outputs
`SCK_MCLK`, `WS_SPDIF`, `I2S0`, `I2S1_3DR`, `MCLK_MUTE` (IT66021FN datasheet p.5, p.11).
The V536 datasheet could not be fetched, so the signal order is *likely*, not established.

**rc.sh pin overrides** (`mkapp/app/script/rc.sh`, `setup_uart()`, unchanged since the
first commit, same in both shipped images) write three PIO configuration registers raw,
after the drivers have set the pin mux:

| register | value | effect |
|---|---|---|
| `0x0300B098` PE_CFG2 | `0x00775577` | PE16/17 off (twi1), PE18/19 uart2, PE20/21 off |
| `0x0300B0D8` PG_CFG0 | `0x22777777` | **PG0-PG5 off** (sdc1 pins), PG6/7 uart1 |
| `0x0300B0FC` PH_CFG0 | `0x35517751` | PH0 out, **PH1 uart4, PH2/PH3 off**, PH4 out, PH5/6 uart3, PH7 uart0 |

So on a running goggle I2S0 is not on its pins. The DT also has `uart4` (`uart4@0` = PH0,
PH1, `uart_used=0`) competing with I2S0 for PH1; which driver owns PH1 is visible in
`pinmux-pins` (probe). The experimental bridge therefore sets PH1-PH3 back to function 3
(read-modify-write, PH0/PH4 untouched).

### Other evidence

* `mkapp/app/app/record/audio/i2s/i2s-daudio-setup.txt`: enable daudio0 and daudio2;
  "must do: set i2s0 format to i2s (not pcm) manually: `aww 0x05090000 0x00060010`"
  = I2S0 CTL with BCLK_OUT (bit 18), LRCK_OUT (bit 17), MODE_SEL = 1 (I2S).
* `audiocodec/alsa-util/amixer-record.txt`: "I2S2: `arecord -Dhw:snddaudio2 ...`".
* Vendor experiment: commit `4392621` (2024-05-24, "update it66121 driver") added an
  `#else` variant of `IT66121_init()` that wrote IT66121 audio registers (bank 1
  `0x30-0x35` CTS/N, `0xC5`, `0x04=0x04`, `0xE0=0x01`, `0xE1=0x05`, `0xE2=0xE4`,
  channel status `0x191/0x193/0x198/0x199`, audio InfoFrame `0x168-0x16D` with checksum
  `0x71`); it was never enabled (`#if (1)`) and removed by `f6779a7` "delete dirty code".
  It did not enable the InfoFrame packet (RegCE), wrote RegC5 without the second
  password byte and selected "no data delay" (left-justified) framing.
* `I2S HUB FUNC` (mixer control of card `audiocodec`): in the Allwinner 4.9
  `sunxi-inter-i2s.c` it sets `HUB_EN` (bit 31) of the internal codec I2S FIFO control;
  `sunxi-daudio.c` has the matching `sunxi daudio audio hub mode` control (`HUB_EN` +
  `TXEN`). The audio hub shares a **playback** stream between I2S blocks (used for codec +
  HDMI on other Allwinner SoCs). The goggle never plays a stream to the codec (line out is
  the analog bypass mixer), so the hub gives no free path from the microphone/line-in
  capture to I2S0; it would still need a playback stream. Not used.
* Prebuilt `libmedia_mpp.so`: one AI device (`AI_DEV_MAX_NUM 1`), opened as ALSA
  `default` (codec); `audioHw_AO_Enable` opens `default`, or `hw:1,0` when
  `AIO_ATTR_S.mPcmCardId = PCM_CARD_TYPE_SNDHDMI` (offset 0xdc in the device struct).
  Without a `sndhdmi` card, card 1 is probably `snddaudio0`, but the bridge does not rely
  on that. `AI_devI2S = 3` accepted by `src/record/confparser.c` cannot work (one AI device).
* Tools on the goggle: busybox 1.27.2 (89 applets, no head/sort/find/printf/date/timeout),
  i2c-tools (`i2cdetect/i2cdump/i2cget/i2cset`), alsa-utils (`amixer aplay arecord`),
  `awr`/`aww` (shell wrappers around `/sys/class/sunxi_dump/{dump,write}`), `gpior/gpiow`.
  Kernel has pinctrl/gpio debugfs and `sunxi_dump`.

## 3. IT66121: register sequence for 48 kHz / 16 bit / 2-ch L-PCM

`PG` = `docs/IT66121_Programming_Guide.v1.05.pdf`, `RL` = `docs/IT66121_Register_List_Release_V1.0.pdf`.
Reg0F[1:0] selects the bank (RL p.3); `IT66121_init()` leaves Reg0F = 0x08 (CEC clock gated,
bank 0), so bank 1 (Reg130-1BF) is 0x09. Implemented in `src/driver/it66121.c`
(`it66121_audio_enable()`), applied at the end of every `IT66121_init()` when enabled.

| step | write | meaning / reference |
|---|---|---|
| 1 | `0F=08`, `E0=00` | bank 0; all audio sources off, 16-bit word length, I2S (S/PDIF: `E0=10`). PG p.31 step 1, RL p.22 |
| 2a | `04 &= ~0x10` | release RegSoftARst (audio clock base); the FIFO reset (bit 2) stays on while programming. PG p.31 step 2, RL p.3. `IT66121_init()` ends with `04=14` (both audio resets on) |
| clk | `58=11` | no external MCLK, auto over-sampling clock, auto IPCLK (reset default). RL p.9 |
| 3 | `0F=09`, `133=00 134=18 135=00`, `0F=08` | N = 6144 (48 kHz). PG p.31-33: valid for every TMDS clock when the CTS is counted by hardware |
| 3b | `F8=C3 F8=A5`, `C5 &= ~0x02`, `F8=FF` | RegC5[1]=0: hardware CTS (default); RegC5 is password protected. PG p.31, p.33; RL p.20 |
| 4 | `E0=00 E1=01 E2=E4 E3=00 E4=00 E5=00` | format: one source, not full-packet; 32-bit-slot I2S with 1-bit delay, WS=0 left, MSB first (the SoC sends 16-bit samples in 32-bit slots, 64 fs); FIFO n <- source n; no swap; channel status from registers; no flat bits; no HBR. PG p.33-35, RL p.22-23 |
| 5 | `0F=09`, `191=00 192=00 193=01 194=02 198=02 199=D2` | IEC 60958 channel status: consumer L-PCM, category general, Fs 48 kHz, clock accuracy level II, original Fs 48 kHz, 16 bit (same values as the mainline Linux it66121 driver). PG p.36-37, RL p.29-30 |
| IF | `168=01 169=00 16A=00 16B=00 16C=00 16D=70`, `0F=08`, `CE=03` | Audio InfoFrame: CC = 2 channels, SF/SS = refer to stream (required for L-PCM), CA = FL/FR; checksum = 0 - (168+169+16B+16C+0x84+0x01+0x0A) = 0x70 (PG p.46); RegCE = send + repeat every field (RL p.21) |
| 2b | `04 &= ~0x14` | release the audio FIFO reset |
| 7 | `E0=01` | enable source 0 (I2S0 pin). For S/PDIF (`E0=11`) only after Reg5F[5] (OSFreqLock) = 1, polled up to 200 ms (PG p.31 step 8, p.35) |

Off (`IT66121_audio(false)`): `E0=C0` (sources off, reset default), `CE=00`, `04 |= 0x14` -
the state `IT66121_init()` leaves. Mute without dropping the link: `E4=10` (flat bit of
source 0, RL p.23); AVMute (RegC1[0]) would blank video too. CTS read-back for diagnosis:
`IT66121_audio_cts()` reads Reg135[7:4]/136/137 (RL p.25); PG p.31 names Reg1A0-1A2
instead - both are logged until a device shows which is right. 0 = no SCK/WS at the chip.

Relation to the video code: HDMI mode is already on (`C0=01`), AVMute cleared (`C1=00`),
GCP sent (`C6=03`) by `IT66121_init()`; audio needs TMDS > 27 MHz (PG p.31), the goggle
outputs 1080p/720p. `IT66121_set_phase()` only writes Reg59 (keeps RegNoAud2Mute = 0) and
Reg70/72 - no effect on audio. `IT66121_close()` holds the chip in reset (and on Goggle 2
writes FPGA reg 0x8A = 0), so audio is reprogrammed by the next `IT66121_init()`.
With audio off `IT66121_init()` issues the same 98 (Goggle, BoxPro) / 100 (Goggle 2) writes
as before; with audio on it adds 41 writes and a 20 ms wait.

## 4. SoC side of HDMI out: what is implemented

The only digital audio the goggle has is the codec ADC capture (mic 1/2 or line-in, chosen
by `audio_sel.sh`), which the record daemon reads through MPP AI device 0 for the DVR.
Line out is an analog bypass and cannot be tapped digitally. The ALSA capture device is
exclusive, so a second reader (e.g. `arecord | aplay`) would break DVR audio.

`src/record/pcmbridge.c` therefore runs **inside the record daemon** and opens an extra
channel on the same MPP AI device (the DVR, `ai2aenc`, and `ai2ao` already share it this
way), so HDMI out carries **exactly the signal that is recorded**. Frames go to ALSA
`hw:snddaudio0` (S16_LE, 2 ch, 48 kHz, mono sources duplicated). Before the first write it
applies the vendor fix (I2S0 CTL MODE_SEL = I2S, BCLK/LRCK output) and returns PH1-PH3 to
function 3. The AI device is only disabled when none of DVR, `ai2ao` and the bridge use it.

Flow: `setting.ini [hdmi] audio_out=1` -> `hdmi_audio_load()` configures the IT66121
driver -> `IT66121_init()` programs audio -> `hdmi_audio_apply()` (called after start-up
and every source switch) writes `[hdmi_audio] src=ai sink=hw:snddaudio0 i2s0_fix=1` into
`record.conf` and sends `HDMIA_cmdSTART` on the record daemon's message queue (key 0x5680).
Switching to HDMI in stops it (HDMI out is closed there).

## 5. HDMI in audio

* EDID (`IT66021_edid()`): CEA block declares basic audio and one Short Audio Descriptor
  (0x8A-0x8C: L-PCM, 2 ch, 32/44.1/48 kHz, 16/20/24 bit), so sources do send audio.
* The repository has no IT66021 register map (the preliminary datasheet has none), so the
  audio-output registers are **unknown**. `IT66021_init()` writes Reg52/53 and others whose
  meaning is not documented here. The probe dumps all three banks while HDMI in is active.
* SoC: `daudio2` is a slave; it only samples when the IT66021 drives SCK/WS. The I2S2
  sample counter (RXCNT, `0x0509202C`) and a 10 s `arecord -D hw:snddaudio2` in
  `utilities/probe/hdmi_audio_test.sh` decide whether it does.
* Implemented behind `[hdmi] audio_in=1`: while the source is HDMI in, the bridge copies
  `hw:snddaudio2` to `plughw:audiocodec` (codec DAC) and `audio_sel.sh out_on` +
  `out_dac_on` route the DAC to the line-out jack (undone when leaving HDMI in). The EDID
  descriptor is narrowed to 48 kHz (0x8B = 0x04, checksum adjusted) because the bridge
  runs at 48 kHz.
* HDMI in -> HDMI out: not possible with the current app, `app_switch_to_hdmi_in()` calls
  `IT66121_close()`.
* HDMI in -> DVR: not implemented. MPP AI can only open ALSA `default`. Two options for
  later: point the record daemon at an `ALSA_CONFIG_PATH` file whose `pcm.!default`
  capture slave is rewritten to `hw:snddaudio2` before an HDMI-in recording (alsa-lib
  re-reads changed top-level config files on the next open), or feed `AW_MPI_AENC_SendFrame`
  from an ALSA capture with monotonic timestamps instead of binding AI to AENC.

## 6. Switching it on (experimental)

In `/mnt/app/setting.ini` (needs this firmware):

```ini
[hdmi]
audio_out=1   ; HDMI out carries the DVR audio input (mic / line-in)
audio_in=0    ; 1: HDMI in audio to the line-out jack while HDMI in is shown
```

Reboot. Logs: HDZGOGGLE log line `IT66121 audio on (I2S): 04=.. 5F=.. E0=01 .. CTS=..`;
record daemon (`/mnt/extsd/RECORD.log` if that file exists) lines tagged `pcmbridge`
(`i2s0 fix: ...`, `first AI frame: len=...`, `bridge: N frames in, M dropped`).
`record.conf [hdmi_audio]` keys: `src` (`ai` or an ALSA capture name), `sink`, `sink2`,
`i2s0_fix`, `latency_ms`. The future mixer UI can call `hdmi_audio_set(out, in)`.

## 7. Probe procedure

See `utilities/probe/README.md`. In short: copy `hdmi_audio_probe.sh` to the SD card as
`develop.sh`, boot, wait for 2 beeps and 2 more minutes, send `hdmi_audio_probe_N.txt` and
`hdmi_audio_dmesg_N.txt`. Optional: `hdmi_audio_test.sh` (tone to HDMI out, 10 s HDMI in
recording) and `bay_gpio_pattern.sh` (module-bay pin finder, bay empty). Please do it on
both a Goggle 1 and a Goggle 2.

## 8. Decision table

| probe / test result | HDMI out audio | HDMI in audio |
|---|---|---|
| `/proc/asound/cards` lacks `snddaudio0` | daudio0 failed to probe (dmesg `daudio`/pinctrl lines): needs a kernel/DT change | - |
| `/proc/asound/cards` lacks `snddaudio2` | - | same for daudio2 |
| `pinmux-pins`: PH1 owned by the uart (5001000.uart) | the bridge's raw re-mux is required (as implemented); a DT fix would be cleaner | - |
| test A: I2S0 TXCNT does not grow while `aplay` runs | I2S0 not clocking: MODE/master fix failed, look at CTL value | - |
| test A: TXCNT grows, IT66121 CTS stays 0 | PH1-PH3 not wired to IT66121 SCK/WS on this goggle: HDMI audio impossible without a hardware change (unless an S/PDIF source exists) | - |
| test A: CTS non-zero (~148500 at 1080p), no sound on TV | wired; check E0/CE in the log, try another TV; channel status / InfoFrame issue | - |
| test A: tone heard | **works**: set `audio_out=1` and test the DVR-mix path | - |
| test B: I2S2 RXCNT stays 0 / WAV only 44 bytes | - | IT66021 does not output I2S by default, or not wired: need the IT66021 audio register map (ITE programming guide) |
| test B: RXCNT grows, WAV silent/noise | - | clocks arrive; data pin (PG13) or format mismatch: compare IT66021 banks with an audio-off dump |
| test B: WAV plays the source audio | - | **works**: `audio_in=1` for monitoring; DVR path is the next step (section 5) |
| Goggle 1 and Goggle 2 differ | wiring differs; keep per-target defaults (`#ifdef HDZGOGGLE2`) | same |

## 9. Established / likely / unknown

| statement | G1 | G2 |
|---|---|---|
| Kernel registers I2S0 (`snddaudio0`) and I2S2 (`snddaudio2`) cards; DT pins PH1-3 / PG10,11,13 | established (DT, kernel strings) | established (identical DT) |
| IT66121 needs only SCK/WS/I2S0, MCLK optional; register sequence above | established (datasheet, PG, RL) | same |
| shipped app never enables IT66121 audio; rc.sh moves PH1-3 off I2S0 | established | established |
| I2S0 PH1-3 = BCLK/LRCK/DOUT, wired to IT66121 | likely | likely (G2 additionally gates HDMI out through FPGA reg 0x8A; whether audio passes the FPGA is unknown) |
| I2S2 PG10/11/13 = BCLK/LRCK/DIN, wired to IT66021 | likely | likely |
| IT66021 outputs I2S without extra register setup | unknown | unknown |
| `I2S HUB FUNC` can feed HDMI from the capture path | no (playback-only mechanism) | no |
| PG0-PG5: DT sdc1/SDIO Wi-Fi pins, set to "disabled" by rc.sh at boot; GPIO 192-194 module bay | established (DT, rc.sh); who owns them at run time: probe | same |
