# Audio mixer

All three targets (HDZero Goggle, Goggle 2, BoxPro) use the internal audio codec
of the Allwinner V536 (`sun8iw16`, ALSA card `audiocodec`). The firmware drives it
with `amixer`; the record daemon captures the ALSA `default` device (48 kHz stereo,
AAC). The control names and ranges below are the ones of the device's
`amixer contents` dump in `mkapp/app/app/record/audio/audiocodec/alsa-util/`,
their meaning comes from the Allwinner driver (`sound/soc/sunxi/sun8iw16-codec.c`).

## Signal paths

```
 codec input           preamp            output mixer gain     L/R OUTPUT MIXER          line out
 MIC1  built-in mic ──[MIC1 boost 0..7]─┬─[MIC1_G      -4.5..+6 dB]─sw─┐
 MIC2  "Line In" jack [MIC2 boost 0..7]─┼─[MIC2 BST    -4.5..+6 dB]─sw─┤
 LINEIN A/V (L/R) ──────────────────────┼─[LINEIN gain -4.5..+6 dB]─sw─┼──> L+R sum ─[lineout volume 0..31]─sw─> jack
 DAC   DVR playback ────────────────────┼──────────────────────────sw─┘      (LINEOUTL/R Mux = OMIX_LR_SUM)
                                        │                                │
                       direct (Custom)  │  sw: MIC1, MIC2, LINEIN        │ sw: "l_output/r_output mixer" (As Heard)
                                        v                                v
                          L/R ADC INPUT MIXER ─[ADC input gain -4.5..+6 dB]─ ADC ─[ADC volume, digital]─ AIF1 ─> record daemon
```

* Per source: only the preamps (MIC1/MIC2 boost) and the three output mixer gains.
  The preamps act on both paths; the output mixer gains only on the way into the
  output mixer. The direct paths into the ADC are plain on/off switches; LINEIN
  has no gain of its own in front of the ADC at all.
* Shared: `ADC input gain control` (analog, every ADC input), `ADC volume`
  (digital), `lineout volume` (analog, after the L+R sum, so it never reaches the
  ADC).
* The output mixer can be fed into the ADC (`LADC input Mixer l_output mixer Switch`,
  `RADC input Mixer r_output mixer Switch`). This is the only way to get per-source
  levels onto the recording, and then the recording is the headphone mix.
* The DAC joins through `Left/Right Output Mixer DACL/DACR Switch`. The line out is
  mono: both LINEOUT muxes take the sum of the left and right output mixers (as the
  old firmware and libmedia_mpp set them).

## What the firmware does

`src/core/audio_mixer_routing.c` turns the settings into one `amixer -q -s` batch
(36 `cset` lines, every routing control set explicitly); `src/core/audio_mixer.c`
writes it to `/tmp/audio_mixer.cmd` and runs `amixer -q -s < /tmp/audio_mixer.cmd`.

Applying is asynchronous: callers only mark the state as changed, and a worker
thread writes the latest state, so a burst of dial steps ends in one or two amixer
runs instead of one per step, and the input thread (which holds `lvgl_mutex`) never
waits for amixer or the driver's PA sleep. Two callers still wait for the codec,
as before: the recording start (`dvr_select_audio_source()`, so a recording never
starts with the previous routing) and DVR playback on/off (after `media_init()`,
and before `media_exit()` closes the file's stream, so the live sources are back
before the DAC stops: the line out never loses its last source).
`setting.ini` is written from the UI thread like every other setting: on a toggle
click, when a slider is released, when the page is left, and 1 s after the last
dial step of a slider that is still grabbed (so a power cut loses at most that
last second, never a released value).

The batch is ordered so that a change never disconnects everything in between:
a line out that goes off is switched off first; then the gains, every switch that
ends up on, the line out (switched on last when it is on), and only then every
switch that ends up off (and the output mixer gains of the sources that are off,
back to 0 dB). With no source connected, DAPM powers the line out down, which
switches the headphone PA off and on again (a click; the Goggle 2 driver also
sleeps `pa_msleep_time` = 160 ms in the PA-off event). Switching the only source,
starting or ending playback, or As Heard <-> Custom therefore overlaps the old and
the new path for the few milliseconds of the batch instead of leaving a gap.

| Audio page        | setting.ini `[mixer]` | controls                                                           |
|-------------------|-----------------------|--------------------------------------------------------------------|
| Mic               | `mic` 0..8            | 0 = output mixer switch off, 1..8 = `MIC1_G ...` 0..7 (-4.5..+6 dB)  |
| Line In           | `line_in` 0..8        | `MIC2 BST stage to L_R outp mixer gain`                             |
| A/V In            | `av` 0..8             | `LINEINL/R to L_R output mixer gain`                                |
| Mic Boost         | `mic_boost` 0..7      | `MIC1 boost AMP gain control`                                      |
| Line In Boost     | `line_in_boost` 0..7  | `MIC2 boost AMP gain control`                                      |
| Headphones        | `headphones` 0..31    | `lineout volume`, 0 = `Lineout Switch` off                         |
| Playback Volume   | `playback` 0..31      | `lineout volume` while a DVR file plays                            |
| DVR Audio         | `dvr_mode` 0/1        | 0 As Heard: ADC fed from the output mixers; 1 Custom: direct inputs |
| Record Mic/Line In/A/V In | `dvr_mic`, `dvr_line_in`, `dvr_av` | Custom: `L/RADC input Mixer MIC1/MIC2/LINEIN` switches |
| Record Level      | `dvr_level` 0..7      | `ADC input gain control` (-4.5..+6 dB); `ADC volume` stays 160 (0 dB) |
| Analog RX Audio (Goggle 2, BoxPro) | `rx_audio` 0/1 | not a codec control: the RTC6715's audio output |

"Record Audio" on the Record Option page still decides whether the DVR records
audio at all. The old "Audio Source" row is gone: the same thing is DVR Audio =
Custom with one source set to Yes.

The mixer is applied at boot, on every change on the Audio page, when a recording
starts and on every source switch (the old `dvr_select_audio_source()` /
`dvr_enable_line_out()` call sites), and when a DVR file starts and stops playing.
HDMI in keeps the old rule: line out off.

The built-in analog receiver (RTC6715 on Goggle 2 and BoxPro) has an audio output
of its own, switched by the receiver itself: the "Analog RX Audio" row
(`[mixer] rx_audio`), used by every `rtc6715.init(1, ...)` for the live picture
(source change and head-tracker wake-up). It is independent of the A/V In level:
the AV-in jack and the module bay share the codec's LINEIN with the receiver, and
their audio keeps reaching the headphones whatever the receiver does. The Analog
RSSI page keeps the receiver's audio off, as before. Migrated from the old
setting: on only if the old Audio Source was A/V In (the old
`rtc6715.init(1, audio_source == A/V In)`, upstream commit 2ee4309); a fresh
install has it off, like the old default (Mic).

### libmedia_mpp resets part of the mixer

`AW_MPI_SYS_Init()` calls `audioHw_Construct()` -> `alsaOpenMixer()`, which sets
`Lineout`, `DACL Mixer AIF1DA0L`, `Left Output Mixer DACL`, `AIF1 AD0L Mixer ADCL`,
`LADC input Mixer MIC1 boost` on, `lineout volume` 27, `DAC volume` 160,
`LINEOUTL/R Mux` OMIX_LR_SUM, `MIC1 boost AMP gain control` 4,
`ADC input gain control` 3 and `ADC volume` 160. It runs when the record daemon
starts (rc.sh, and again in chkfixsd.sh / formatsd.sh, i.e. the automatic SD check
after boot, the SD repair and format of the Storage page) and in the app itself for
every DVR file played. When the daemon gets there is not known to the app (fsck can
run for minutes, formatsd.sh copies resources first).

So the mixer's worker watches the codec instead of guessing: while it has nothing
to apply, it reads every control of the last batch back every 2 s, straight from
`/dev/snd/controlC0` with ALSA control ioctls (`audio_mixer_codec.c`, no process,
36 small ioctls), and applies the current state again as soon as one differs. A
reset is therefore undone within about 2 s whenever it happens, and logged
(`codec changed by someone else`). If the codec does not keep a value (three
re-applies in a row for the same state), the worker logs it and stops re-applying
until the state changes. This also means that `amixer cset` by hand on one of the
mixer's controls is undone within 2 s; to experiment, change the setting on the
Audio page or stop the app. The emulator has no codec, so it does not watch.

### DVR playback

Before: the player's `AW_MPI_SYS_Init` switched `Left Output Mixer DACL` on with
line out 27, so playback was audible, but only the left DAC channel (every source
switch had turned both DAC switches off), with the live A/V monitor still mixed in,
and the DAC switch and volume 27 stayed after playback.
Now, while a file plays: live sources out of the output mixer, DACL -> left and
DACR -> right output mixer, `DAC volume` 152 (-6 dB, because the line out adds left
and right: a mono recording keeps the old level instead of +6 dB), line out =
Playback Volume (also when Headphones is 0 or the source is HDMI in). Afterwards the
live mix is restored.

## Levels and defaults

* 0 dB of a source = gain register 3, the driver default seen in the dump. The
  sources can only be trimmed between -4.5 and +6 dB; below that there is only off.
* Mixing adds signals: two sources peaking together can reach +6 dB over either one.
  In As Heard mode that sum goes into the ADC, so keep mixed hot sources at 0 dB or
  below, or lower Record Level (down to -4.5 dB). Nothing in this path limits;
  whether it clips audibly has not been measured.
* Not trustworthy dB scales: the BSP gives `lineout volume` the -4.5 dB/1.5 dB scale
  of the mixer gains (amixer would show up to +42 dB) and the preamps 0..14 dB in
  2 dB steps. The V536 analog registers have the layout of the A64's, whose manual
  (mainline `sun50i-codec-analog.c`) gives line out 0..1 = mute, 2..31 = -43.5..0 dB,
  and preamp 0 = 0 dB, 1..7 = 24..42 dB. The page therefore shows raw steps for
  those three sliders; a step on a real goggle should settle it.
* Defaults when `[mixer]` is missing (taken over from `[record] audio_source`, so
  the DVR records exactly what it did before):
  * DVR Audio Custom with only the old source (Mic / Line In / A/V In) = the old
    `audio_sel.sh in_mic1 / in_mic2 / in_linein` routing;
  * headphones as the old `out_on`: A/V +6 dB (register 7), line out 31, mics off;
  * Mic/Line In Boost 4 and Record Level 3 (0 dB): what libmedia_mpp and the driver
    have always set; Playback Volume 27: libmedia_mpp's line out level that playback
    used until now. The boosts come from the device tree (`[codec] maingain` /
    `headsetmicgain` in sys_config.fex, written by the driver at probe; libmedia_mpp
    sets MIC1 to 4 again, nothing sets MIC2). They are 4 / 4 in the official images
    of all three targets (Goggle Rev20250319, Goggle 2 Rev20250715, BoxPro
    Rev20250517; `lineoutvol` 31 and `pa_msleep_time` 160 ms there as well), so
    Line In recordings keep their level on every target.

## Targets

* Goggle 2: A/V = LINEIN = AV-in jack, module bay and the internal receiver.
* Goggle (1): no internal receiver, A/V = AV-in jack / module bay; the RTC6715
  calls are no-ops.
* BoxPro: same codec and the same three inputs as before (the old page offered
  Mic / Line In / A/V In there too), internal receiver like Goggle 2. Same mixer,
  smaller rows.

## Checking without hardware

`utilities/audio_mixer/test_mixer.py` (python3 and a host C compiler, ~20 s) builds
the real `src/core/audio_mixer.c` and `audio_mixer_routing.c` for the host three
times (`-DHDZGOGGLE`, `-DHDZGOGGLE2`, `-DHDZBOXPRO`) with a harness
(`harness/harness.c`: real minIni and setting.ini, the worker thread, a simulated
codec that takes the batches the code writes for `amixer -q -s`) and checks them
against `codec_model.py`, a model of the codec's signal graph made of the driver's
DAPM routes (`ac_dapm_routes[]`: switches, muxes, the gains on each widget/edge,
the line out pin). For a codec state the model gives, per sink (recording left /
right, line out left / right), every source that reaches it and the gain settings
on the way. The test asserts:

* routing: for a matrix of mixer states (As Heard / Custom, every source, headphones
  0, HDMI in, playback, out-of-range values) the sources and gains of the recording
  and the headphones are what this document specifies, and every batch line is a
  control of the device's `amixer contents` dump with a value in range, set once;
* loading: migration from each old Audio Source (and -1, 3, 258, read as the old
  firmware did), what is written to `[mixer]`, clamping, missing fields;
* migration equivalence: per target and old Audio Source, the old firmware (driver
  defaults + the device tree's preamp values, libmedia_mpp's reset, then the real
  `mkapp/app/script/audio_sel.sh` sequences of a source switch, HDMI in and the
  recording start) against the new one: recording, headphones (sources and gains),
  line out power and the analog receiver's audio switch must be equal. Booting
  straight into HDZero is the one documented difference (headphones; the recording
  is equal). Also with the record daemon resetting the codec after the app has
  routed it: the watcher must restore the same state;
* transitions: replaying the batch of a change line by line on the model, the line
  out never loses power and the recording never loses its sources in between, and a
  line out that goes off goes off first;
* worker: `audio_mixer_apply()` returns at once even when amixer takes 150 ms, 20
  dial steps coalesce into a few batches ending at the last value, setting.ini is
  only written by `audio_mixer_save()`;
* watcher: a libmedia_mpp reset is undone; a control the codec does not keep stops
  after three re-applies.

`--driver <BSP>/sound/soc/sunxi/sun8iw16-codec.c` also checks that every modelled
route is one of the driver's. `utilities/audio_mixer/mutants.py` (~5 min) applies 18
small mutations to a copy of the sources (among them the review's: ADC MIC1 and
LINEIN switches swapped, AIF1 AD0L/R off, As Heard inverted; and the batch order, the
receiver migration, the preamp defaults, clamping, the worker, the watcher) and fails
unless the test catches every one.

`utilities/audio_mixer/stub/amixer` logs what it is given; put it first in `PATH`
(with `AMIXER_LOG`) to see the batches the emulator UI sends. On a goggle,
`/tmp/audio_mixer.cmd` holds the last batch and `amixer contents` shows the result.
