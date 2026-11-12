# HDMI audio on the HDZero goggles - findings, experimental code, probe

Status: **experimental, everything default off.** Nothing here has been run on a
goggle yet. With the default `setting.ini` the firmware behaves exactly as before
(host tests: `utilities/probe/test/it66121_seq_test.sh` - the IT66121 register sequence
is identical; `it66021_edid_test.sh` - the IT66021 I2C sequence and the EDID it serves
are identical).

Applies to the original **HDZero Goggle** (`-DHDZ_GOGGLE=ON`) and the **Goggle 2**
(`-DHDZ_GOGGLE2=ON`); BoxPro builds the same code.

Sources: `PG`/`RL` = the IT66121 Programming Guide v1.05 / Register List (`docs/`); the
IT66121FN and IT66021FN datasheets; **V536 datasheet** = "V536-H/V526 Datasheet Revision
1.6" (Allwinner), chapters 11.1 (I2S/PCM) and 12.7 (PIO); **ITE driver** = ITE's public
IT6602/IT66021 driver `it6602.c` / `it6602_reg.h` (reyno123456/wireless_soc,
`AR8020SW/Driver/Board/Common/IT66021`); pkendall64's wiki page "Analog Module Backpack
Control".

## 1. Short answer

| | HDMI out audio (IT66121) | HDMI in audio (IT66021) |
|---|---|---|
| chip | 2-ch L-PCM via I2S (SCK, WS, I2S0; MCLK optional) or S/PDIF (datasheet) | outputs I2S (SCK, WS, I2S0, I2S1) / S/PDIF, MCLK (datasheet). ITE driver: Reg52[3:0] tri-states the I2S pins, Reg52[4] S/PDIF; the firmware writes Reg52 = 0x30, i.e. I2S pins driven, S/PDIF tri-stated |
| SoC side (V536 datasheet, shipped device tree) | I2S0 `daudio@0x05090000`; DT pin group `daudio0@0` = PH1/PH2/PH3 = I2S0 BCLK/LRCK/DOUT (PH_CFG0 function 3); `daudio_master=4`: the SoC drives BCLK/LRCK. ALSA card `snddaudio0`. The DT names no consumer | I2S2 `daudio@0x05092000`; `daudio2@0` = PG10/PG11/PG13 = I2S2 BCLK/LRCK/DIN (PG_CFG1 function 3); `daudio_master=1`: the external chip drives BCLK/LRCK. ALSA card `snddaudio2` |
| wiring on the board | **likely** to the IT66121 (a group of exactly a transmitter's three signals, the IT66121 the only I2S receiver); which SoC pin reaches which chip pin: **unknown** | **likely** to the IT66021 (a receiver's three signals; vendor note records from `hw:snddaudio2`); pin order at the chip: **unknown** |
| shipped firmware | audio never programmed (Reg04 audio resets stay asserted); **rc.sh re-muxes PH1 to uart4 and PH2/PH3 off at boot** | EDID advertises Basic Audio + L-PCM 2 ch 32/44.1/48 kHz; nothing captures I2S2 |
| what this branch adds (off by default) | `[hdmi] audio_out=1`: IT66121 audio setup + the DVR audio input copied to I2S0 | `[hdmi] audio_in=1`: I2S2 played into the codec DAC while HDMI in is shown: heard on the line-out jack **and recorded by the DVR** (mixed with its source); EDID offers 48 kHz L-PCM only |
| not possible | - | HDMI in -> HDMI out (the app switches HDMI out off while HDMI in is shown) |
| only the device can tell | whether PH1-PH3 reach SCK/WS/I2S0 of the IT66121 in this order (G1 and G2 separately; on G2 also whether the FPGA is in the path), whether the TV plays the tone | whether the IT66021 drives SCK/WS by default, at which word clock and format |

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

Functions per the V536 datasheet (PH_CFG0, PG_CFG0/1; the kernel's pinctrl table agrees):

| pin | function 3 (datasheet) | other | used for |
|---|---|---|---|
| PH0 | I2S0_MCLK | 5: UART4_TX | GPIO 224 = `GPIO_HDZ_RX_RESET` |
| PH1, PH2, PH3 | **I2S0_BCLK, I2S0_LRCK, I2S0_DOUT** | 2: AIF2, 5: UART4 RX/CTS/RTS | DT `daudio0@0` (I2S0) |
| PH4 | I2S0_DIN | 2: AIF2_DIN | GPIO 228 = `GPIO_HDZ_TX_RESET` |
| PG0 - PG5 | - | 2: SDC1 CLK/CMD/D0-D3 | DT `sdc1` (SDIO Wi-Fi, `[wlan] wlan_busnum=1`); PG0-PG2 = GPIO 192-194, the module-bay control of the firmware (`src/core/defines.h`, see below) |
| PG8, PG9 | PG9: I2S2_MCLK | | Wi-Fi `wlan_regon`, `wlan_hostwake` |
| PG10, PG11, PG13 | **I2S2_BCLK, I2S2_LRCK, I2S2_DIN** | 2: AIF3 | DT `daudio2@0` (I2S2) |
| PG12 | I2S2_DOUT | 2: AIF3_DOUT | codec `gpio-spk` (speaker amplifier enable) |

So the SoC side is established: the I2S0 group is BCLK, LRCK and DOUT (a transmitter), the
I2S2 group BCLK, LRCK and DIN (a receiver); the pins left out are exactly the ones the board
uses otherwise (two reset GPIOs, the speaker GPIO). That fits the ITE chips: IT66121 audio
inputs `SCK/MCLK`, `WS`, `I2S0..3` (IT66121FN datasheet p.6; "MCLK input is optional",
p.11), IT66021 outputs `SCK_MCLK`, `WS_SPDIF`, `I2S0`, `I2S1_3DR`, `MCLK_MUTE` (IT66021FN
datasheet p.5, p.11). That these pins are wired to these chips, and in which order, is not
documented: *likely*, not established.

**GPIO 192-194 (PG0-PG2), module bay.** The firmware drives them as module-bay CLK/DAT/CS
(`softspi`). Per pkendall64's wiki (original HDZero Goggle) they are on the connector
between the module-bay board and the goggle, and reach the 9-pin analog module connector
only after a 3-wire mod. Goggle 2: not documented. `utilities/probe/bay_gpio_pattern.sh`
finds them and their voltage.

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
and puts them back when it stops (section 4).

**`/sys/class/sunxi_dump`** (what `awr`/`aww` use; the app runs `aww` from several places)
is one address latch for every process: `echo addr > dump; cat dump`. Two users at the
same time can read each other's register. Everything on this branch that reads through it
(the bridge, the probe kit) reads three times and checks the value, takes the lock file
`/tmp/.sunxi_dump.lock`, and the probe kit pauses the app (whose `aww` cannot take the lock).

### Other evidence

* `mkapp/app/app/record/audio/i2s/i2s-daudio-setup.txt`: enable daudio0 and daudio2;
  "must do: set i2s0 format to i2s (not pcm) manually: `aww 0x05090000 0x00060010`"
  = I2S0 CTL with BCLK_OUT (bit 18), LRCK_OUT (bit 17), MODE_SEL = 1 (I2S) (V536
  datasheet 11.1.5; reset value 0x00060000).
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
  `sunxi-daudio.c` has the matching `sunxi daudio audio hub mode` control. The audio hub
  shares a **playback** stream between I2S blocks; it gives no path from the capture to
  I2S0. Not used.
* Prebuilt `libmedia_mpp.so`: one AI device (`AI_DEV_MAX_NUM 1`), opened as ALSA
  `default` (codec); `audioHw_AO_Enable` opens `default`, or `hw:1,0` when
  `AIO_ATTR_S.mPcmCardId = PCM_CARD_TYPE_SNDHDMI`. `AI_devI2S = 3` accepted by
  `src/record/confparser.c` cannot work (one AI device).
  `AUDIO_FRAME_S.mLen` is the byte count of the whole interleaved frame (all channels),
  although `mm_comm_aio.h` calls it "data lenth per channel": `audioHw_AI_CapThread`
  (0x2c54c) sets it to the chunk bytes that `alsaSetPcmParams` computes as chunk frames x
  channels x physical width / 8 (0x2ad40-0x2ad88, the buffer one `snd_pcm_readi` fills),
  and `AIChannel_EmptyThisBuffer` copies `mLen` bytes. The bridge relies on that and logs
  the measured frame rate (`input delivers about N frames/s`; 24000 would mean otherwise).
* Tools on the goggle: busybox 1.27.2 (89 applets, no head/sort/find/printf/date/timeout;
  `flock` and `usleep` exist), i2c-tools (`i2cdetect/i2cdump/i2cget/i2cset`, `-r` ranges),
  alsa-utils (`amixer aplay arecord`), `awr`/`aww` (shell wrappers around
  `/sys/class/sunxi_dump/{dump,write}`), `gpior/gpiow`. Kernel has pinctrl/gpio debugfs.

## 3. IT66121: register sequence for 48 kHz / 16 bit / 2-ch L-PCM

Reg0F[0] selects the bank (RL p.3); `IT66121_init()` leaves Reg0F = 0x08 (CEC clock gated,
bank 0), so bank 1 (Reg130-1BF) is 0x09. Implemented in `src/driver/it66121.c`
(`it66121_audio_enable()`), applied at the end of every `IT66121_init()` when enabled.
Every multi-register access of the driver (init, close, `IT66121_set_phase()` from the
HDMI-in detect / ELRS / UI threads, audio) holds one lock, so `set_phase` never writes
Reg59/70/72 while bank 1 is selected.

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
instead - both are logged. A CTS of 0 means no audio clock was counted **in these
registers**: no clock at SCK/WS, a different pin order, the FPGA (G2), or the wrong
read-back register (section 8).

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
The ALSA capture device is exclusive, so a second reader (e.g. `arecord | aplay`) would
break DVR audio.

`src/record/pcmbridge.c` therefore runs **inside the record daemon** and opens an extra
channel on the same MPP AI device (the DVR, `ai2aenc`, and `ai2ao` already share it this
way), so HDMI out carries **exactly the signal that is recorded**. Frames go to ALSA
`hw:snddaudio0` (S16_LE, 2 ch, 48 kHz, mono sources duplicated). Before the first write it
applies the vendor fix (I2S0 CTL MODE_SEL = I2S, BCLK/LRCK output) and returns PH1-PH3 to
function 3: read three times (must agree), plausibility-checked (PH0/PH4 outputs; CTL
reserved bits 0), written and read back. On stop - and on a start that fails half-way - it
puts back exactly these fields, if they still hold its own values. The AI device is only
disabled when none of DVR, `ai2ao` and the bridge use it, also after a failed start.

Flow: `setting.ini [hdmi] audio_out=1` -> `hdmi_audio_load()` configures the IT66121
driver -> `IT66121_init()` programs audio -> `hdmi_audio_apply(hdmi_in_shown)` (after
start-up and every source switch; the route depends on the source being switched **to**)
writes the request `/tmp/hdmi_audio.req` (`seq`, `active`, `src=ai`,
`sink=hw:snddaudio0`, `i2s0_fix=1`) and sends `HDMIA_cmdSTART` on the record daemon's
queue (key 0x5680). The daemon answers in `/tmp/hdmi_audio.state` (`state`, `seq`, `pid`,
`err`), also when it starts and exits. `hdmi_audio_poll()` (peripheral thread, about
every 0.5 s; no file access while the feature is off) re-sends what was not confirmed:
queue not there yet, sink missing or busy, record daemon restarted by the Storage page
(`formatsd.sh` / `chkfixsd.sh`) - after 3, 6, 12, 24, 30 s, six times, and again after a
daemon restart. Switching to HDMI in replaces it with the HDMI-in route (HDMI out is closed
there).

## 5. HDMI in audio

* **EDID** (`IT66021_edid()`). Shipped: Basic Audio (CEA byte 3 bit 6, by CEA-861 2-ch
  L-PCM at 32, 44.1 and 48 kHz) and one Short Audio Descriptor (0x8A-0x8C: L-PCM, 2 ch,
  32/44.1/48 kHz, 16/20/24 bit). With `audio_in=1` the EDID says only what the path does:
  Basic Audio cleared, SAD 48 kHz only (0x8B = 0x04). The chip serves the block checksums
  from RegC4/RegC5 in the mode the driver leaves it in (RegC0[1:0] = 00, shadow on; ITE
  driver `EDIDRAMInitial()` / `UpdateEDIDReg()`), so the adjusted block-1 checksum is
  written to RegC5 (and RAM byte 0xFF). Checked on the host (`it66021_edid_test.sh`: both
  checksums, CEA structure, audio claims; with `audio_in=0` identical to before).
  Trade-off: HDMI/CEA-861 expect a sink that takes any audio to take Basic Audio, so a
  48 kHz-only EDID is not strictly compliant; a source may then send no audio at all. One
  that sends another rate anyway is muted by the bridge, not played at the wrong speed.
* **IT66021 audio output.** The repository has no IT66021 register map, but the ITE driver
  documents the output control: Reg52 (`REG_RX_OUPT_CTRL1`) [3:0] `B_TriI2SIO`, [4]
  `B_TriSPDIF`, [5] `B_DisVAutoMute`; `IT6602AudioOutputEnable()` clears [4:0] to drive the
  pins. `IT66021_init()` writes Reg52 = 0x30: I2S pins driven, S/PDIF tri-stated, video
  auto-mute off. It also writes Reg7D (`REG_RX_HWMuteCtrl`) = 0x30, i.e. `B_HWMuteClr` and
  `B_HWAudMuteClrMode` set; the ITE driver pulses these (set, then clear) - what leaving
  them set does is unknown. Whether the chip outputs I2S with the right framing by
  default: device test (the probe dumps all three banks while HDMI in is active).
* **SoC**: `daudio2` is a clock slave; it only samples while the IT66021 drives SCK/WS. The
  I2S2 sample counter (RXCNT, `0x0509202C`) and a 10 s `arecord -D hw:snddaudio2` in
  `utilities/probe/hdmi_audio_test.sh` decide whether it does.

### HDMI-in audio as a mixer source: jack and DVR (implemented behind `audio_in=1`)

While HDMI in is shown, the bridge plays the I2S2 capture (`hw:snddaudio2`) into the
internal codec's DAC (`plughw:audiocodec`, 48 kHz). From the DAC it enters the codec's
output mixers like any analog source:

```
I2S2 (IT66021) -> bridge -> AIF1 DA0 -> DAC -> L/R output mixer -+-> L+R sum -> lineout volume -> jack
                                                                 +-> "l/r_output mixer" -> ADC -> MPP AI -> DVR (AAC)
```

`hdmi_audio_line_out()` in `src/core/hdmi_audio.c` is the one function that switches it
(the integration point for the mixer module of the audio branch). Controls (card
`audiocodec`):

| purpose | controls |
|---|---|
| heard | `DACL Mixer AIF1DA0L Switch` 1, `DACR Mixer AIF1DA0R Switch` 1 (libmedia_mpp sets only the left one); `Left Output Mixer DACL Switch` 1, `Right Output Mixer DACR Switch` 1; `DAC volume` 152 (8 steps of 0.75 dB below libmedia_mpp's 160: the line out sums left and right); `LINEOUTL Mux` / `LINEOUTR Mux` 1 (OMIX_LR_SUM); `lineout volume` 31; `Lineout Switch` 1 (last) |
| not mixed in | `Left Output Mixer LINEINL Switch` 0, `Right Output Mixer LINEINR Switch` 0 (the analog line-in bypass the last analog source left on) |
| recorded | `LADC input Mixer l_output mixer Switch` 1, `RADC input Mixer r_output mixer Switch` 1 (left ADC <- left output mixer, right <- right), next to the DVR's own source (`audio_sel.sh in_*`: the direct ADC input switch and `AIF1 AD0L/R Mixer ADCL/R Switch`); `ADC input gain control` / `ADC volume` unchanged |
| off (leaving HDMI in) | `Lineout Switch` 0 first (the state HDMI in had), the two ADC output-mixer switches 0, the DAC switches 0, `DAC volume` 160, the line-in bypass switches 1 (what `dvr_enable_line_out(true)` sets) |

With the audio branch's mixer this becomes its DAC source (`AUDIO_MIXER_HDMI` level,
`context->live_dac`); there the recording takes the output mixers only in "As Heard"
mode, and "line out off for HDMI in" is lifted while HDMI-in audio plays. The record
daemon's libmedia_mpp init resets part of the mixer (Lineout, DACL, DAC volume 160, line
out 27); on a daemon restart `hdmi_audio_poll()` applies the controls again.

Coexistence and loops:
* The record daemon runs one bridge route at a time. While HDMI in is shown the app closes
  HDMI out, so the DVR -> HDMI out route is not needed then; the two routes are exclusive
  by the app's state, and switching replaces one with the other.
* The DVR capture (MPP AI on the codec ADC) keeps running next to the HDMI-in bridge: codec
  playback and capture run at the same time, but the codec's AIF1 has **one** sample-rate
  field for both (`sun8iw16-codec.c` `codec_hw_params`: `SYS_SR_CTRL` AIF1_FS; the machine
  driver sets PLL_AUDIO to 24.576 MHz for 48 kHz). Both run at 48 kHz (record.conf `[ai]
  sample_rate=48000`); the bridge refuses the route if the DVR is set to another rate.
* No loop: nothing plays the ADC capture back into the DAC while this route runs. The
  HDMI-out bridge (AI -> I2S0) only runs while HDMI out is open. `ai2ao` (AI -> AO = codec
  DAC, `AIO_cmdSTART`, not used by the app) would need the codec playback stream the
  bridge holds: its open fails (and the other way round). DVR playback in the app (also
  the codec DAC) happens in the menu, where this route is off.

Limits:
* An analog round trip DAC -> output mixer -> ADC: some noise; the ADC input gain is shared
  by all sources, and two loud sources mixed into the ADC can clip (nothing limits).
* Clock: I2S2 is clocked by the HDMI source, the codec by PLL_AUDIO. The bridge mutes until
  the incoming rate is seen to be 48 kHz (within 3 %) and mutes again if it is not; small
  drift is absorbed by dropping or repeating one frame per block (at most ~1400 ppm), an
  underrun is refilled with silence. Clicks at a correction are possible.
* Recorded only through the output-mixer -> ADC switches (on the audio branch: As Heard only).
  The WiFi stream (RTSP branch) and HDMI out carry what the DVR records only when they take
  the DVR audio; HDMI out is off while HDMI in is shown anyway.

Device test (with `audio_in=1`, and `RECORD.log` / `HDZGOGGLE.log` on the SD card):
1. HDMI source playing a known tone on HDMI in, headphones on the jack, HDMI in selected:
   the tone is heard; `RECORD.log` shows `HDMI in audio at 48000 Hz: unmuted` and every
   10 s `bridge: N frames in (~48000/s) ...`. A 44.1 kHz source: `muted`, silence.
2. Start a DVR recording while it plays, speak into the mic: the file has the tone and the
   mic (the DVR's selected source). Leave HDMI in: line out goes off as before; switch to
   analog: A/V on the jack as before, no tone.
3. Storage page: repair or format the SD card while HDMI in plays: within ~10 s the
   `HDZGOGGLE.log` shows `record daemon restarted` and the tone is back.
4. Long run (10 min): count clicks; `drift-adjusted` in the bridge log should grow slowly.

* HDMI in -> HDMI out: not possible with the current app, `app_switch_to_hdmi_in()` calls
  `IT66121_close()`.

## 6. Switching it on (experimental)

In `/mnt/app/setting.ini` (needs this firmware):

```ini
[hdmi]
audio_out=1   ; HDMI out carries the DVR audio input (mic / line-in)
audio_in=0    ; 1: HDMI in audio on the line-out jack and in the DVR while HDMI in is shown
```

Reboot. Logs: HDZGOGGLE log `IT66121 audio on (I2S): 04=.. 5F=.. E0=01 .. CTS=..` and
`hdmi audio: route a -> b`; record daemon (`/mnt/extsd/RECORD.log` if that file exists)
lines tagged `pcmbridge` (`i2s0 fix: ...`, `first AI frame: len=...`,
`input delivers about N frames/s`, `bridge: N frames in ...`, `i2s0 undo: ...`).
`/tmp/hdmi_audio.req` / `/tmp/hdmi_audio.state` show the last request and the daemon's
answer. `record.conf [hdmi_audio] latency_ms` (default 60) stays tunable. The future
mixer UI can call `hdmi_audio_set(out, in)`.

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
| test A: I2S0 TXCNT does not grow while `aplay` runs | I2S0 not clocking: MODE/master fix not applied (look at the CTL lines; "read not trusted" = nothing was written) | - |
| test A: TXCNT grows, Reg135-137 CTS stays 0 | no audio clock counted at the IT66121 **in these registers**. Equally possible: PH1-PH3 not wired to SCK/WS/I2S0; wired in another order (e.g. LRCK on SCK); on G2 the FPGA in the path; the CTS is read back in Reg1A0-1A2 (compare the logged 1A0-1A2). Next: the IT66121 bank dumps (probe part 2, test A) during the tone; a scope on PH1-PH3 and the IT66121 SCK/WS pins if possible; a test build with the I2S0 slot/format varied | - |
| test A: CTS (either register set) non-zero, ~148500 at 1080p, no sound on TV | clock arrives; check E0/CE in the log, try another TV; format (E1), channel status or InfoFrame | - |
| test A: tone heard | **works**: set `audio_out=1` and test the DVR path | - |
| test B: I2S2 RXCNT stays 0 / WAV only 44 bytes | - | no clock on PG10/PG11: the IT66021 does not drive SCK/WS (Reg52 = 0x30 drives the pins; the source may send no audio for this EDID; check the IT66021 bank dumps) or not wired |
| test B: RXCNT grows, WAV silent/noise | - | clocks arrive; data pin (PG13) or format mismatch: compare IT66021 banks with an audio-off dump |
| test B: WAV plays the source audio | - | **works**: `audio_in=1`; then the device test of section 5 |
| Goggle 1 and Goggle 2 differ | wiring differs; keep per-target defaults (`#ifdef HDZGOGGLE2`) | same |

## 9. Established / likely / unknown

| statement | G1 | G2 |
|---|---|---|
| Kernel registers I2S0 (`snddaudio0`) and I2S2 (`snddaudio2`) cards; DT pins PH1-3 / PG10,11,13 | established (DT, kernel strings) | established (identical DT) |
| SoC pin functions: PH1/2/3 = I2S0 BCLK/LRCK/DOUT, PG10/11/13 = I2S2 BCLK/LRCK/DIN | established (V536 datasheet) | same |
| I2S0 is the SoC clock master, I2S2 a clock slave (DT `daudio_master`) | established (DT) | same |
| IT66121 needs only SCK/WS/I2S0, MCLK optional; register sequence above | established (datasheet, PG, RL) | same |
| shipped app never enables IT66121 audio; rc.sh moves PH1-3 off I2S0 | established | established |
| PH1-PH3 wired to the IT66121 SCK/WS/I2S0, in this order | likely (wiring), order unknown | likely, order unknown; whether the audio passes the FPGA (reg 0x8A) is unknown |
| PG10/11/13 wired to the IT66021 SCK/WS/I2S0 | likely | likely |
| IT66021 I2S pins driven (Reg52 = 0x30 per the ITE driver's bit names) | likely (ITE driver for the same family) | same |
| IT66021 outputs I2S audio with the right framing without extra setup | unknown | unknown |
| `I2S HUB FUNC` can feed HDMI from the capture path | no (playback-only mechanism) | no |
| PG0-PG5: DT sdc1/SDIO Wi-Fi pins, "IO disable" after rc.sh | established (DT, rc.sh) | same |
| GPIO 192-194 = the firmware's module-bay CLK/DAT/CS | established (`src/core/defines.h`) | same |
| ... on the goggle-side connector of the bay board; on the 9-pin module connector only after a 3-wire mod; I/O voltage | likely (pkendall64's wiki); voltage unknown | unknown (wiki covers the original goggle) |
| `AUDIO_FRAME_S.mLen` = whole interleaved frame in bytes | established (libmedia_mpp disassembly); logged on the device | same |
