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
* The output mixers can be fed into the ADC (`LADC input Mixer l_output mixer Switch`,
  `RADC input Mixer r_output mixer Switch`): the left ADC takes the left output
  mixer, the right ADC the right one. This is the only way to get per-source levels
  onto the recording (As Heard). It is not the headphone signal: the recording is
  stereo and taken before the L+R sum and before `lineout volume`, while the
  headphones get left + right of both mixers (mono). A source that is in both output
  mixers (the mics, which feed both, or a mono A/V signal) reaches the line out from
  both, so it is about 6 dB hotter in the headphones than in each recorded channel
  (if the hardware sums; the driver calls it OMIX_LR_SUM, not measured). The
  headphone volume does not change the recording.
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
| Record Level      | `dvr_level` 0..7      | `ADC input gain control` (-4.5..+6 dB); `ADC volume` stays 160 (see below) |
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
DACR -> right output mixer, `DAC volume` 152 (8 steps of 0.75 dB = 6 dB below the
160 of before, because the line out adds left and right: a mono recording keeps the
old level instead of +6 dB), line out =
Playback Volume (also when Headphones is 0 or the source is HDMI in). Afterwards the
live mix is restored.

## Levels and defaults

* 0 dB of a source = gain register 3, the driver default seen in the dump. The
  sources can only be trimmed between -4.5 and +6 dB; below that there is only off.
* Mixing adds signals: two sources peaking together can reach +6 dB over either one.
  In As Heard mode that sum goes into the ADC, so keep mixed hot sources at 0 dB or
  below, or lower Record Level (down to -4.5 dB). Nothing in this path limits;
  whether it clips audibly has not been measured.
* What the page shows in dB, and why: only the values whose dB scale the BSP driver
  and the mainline driver of the same analog block (A64, `sun50i-codec-analog.c`,
  from the A64 manual) agree on: the three output mixer gains and the ADC input gain
  (Record Level), register 0..7 = -4.5..+6 dB in 1.5 dB steps. Everything else is
  shown in raw steps: the BSP gives `lineout volume` the -4.5 dB/1.5 dB scale of the
  mixer gains (amixer would show up to +42 dB) and the preamps 0..14 dB in 2 dB
  steps, while the A64 layout is line out 0..1 = mute, 2..31 = -43.5..0 dB and preamp
  0 = 0 dB, 1..7 = 24..42 dB. A step on a real goggle should settle it.
* `ADC volume` and `DAC volume` (digital, 0.75 dB steps) stay at 160, the value
  libmedia_mpp sets and the old firmware recorded and played with. By the BSP's own
  TLV (-119.25 dB at 0) that is +0.75 dB; by the A64 layout (0 = mute, 1 = -119.25 dB)
  it is 0 dB. Neither is shown on the page; the docs and code only rely on "same as
  before" and on relative steps (playback: 8 steps = 6 dB less).
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

## Differences from the old firmware

For a unit that is updated and never opens the Audio page, `utilities/audio_mixer/
test_mixer.py` checks that recording, headphones (sources and gains), line out power
and the analog receiver's audio switch are what the old firmware produced, for every
old Audio Source, after a source switch, with HDMI in and at recording start. What
differs:

* Booting straight into HDZero (no source switch since power-on). The old firmware
  left the record daemon's mixer in place until the first source switch: line out
  27 with only the (idle) left DAC connected, so the headphones were silent, and A/V
  (+6 dB, line out 31) only came after the first switch to any source. Now the A/V
  level applies from boot, i.e. the state the old firmware reached after any switch.
  Kept on purpose: the old behaviour depended on whether a source had been switched
  since power-on (and on when the record daemon initialised, see below), and the
  Audio page needs one state to show. A unit with nothing on the AV-in jack / module
  bay hears the same as before after any switch: LINEIN's noise floor at +6 dB.
  To get the old silence, set A/V In to Off (or Headphones to Off).
* The record daemon's mixer reset (boot, SD check, SD format, DVR playback): the old
  firmware kept it until the next source switch or recording start (line out 27, the
  line out on even with HDMI in, the mic into the left ADC); now it is undone within
  about 2 s.
* DVR playback: both DAC channels, live sources muted, Playback Volume; before it was
  the left DAC only with the live mix and line out 27, which stayed after playback.
* HDMI in: line out off as before; the old `out_off` also set the LINEOUT muxes to
  their first item, now they stay at OMIX_LR_SUM (no effect while the line out is
  off).
* Changes are click-free where the batch can make them so (see the batch order
  above); the old scripts switched one control per amixer process.

## Audio page layout

The page has its own columns (`conf/ui.h` `UI_AUDIO_*`, per target): label, two
button columns (arrow + button, the sliders span both), value, without column gaps.
They are sized for the longest translation at the target's font (Montserrat 24 on
the goggles, 18 on the BoxPro; label column 316 / 236 px, visible button text
160 / 126 px, value 100 / 72 px) and checked with emulator screenshots in English,
German, Spanish, Russian, Chinese and Japanese on all three targets. Two Spanish
and one Japanese strings were shortened for it ("Ganancia Línea", "Volumen
Reproducción", "Selección"; "モニター" for As Heard). A `_Static_assert` keeps the
columns inside the page; new translations must stay within these widths.

## Testing on a goggle

The host test covers the routing logic; these need the hardware. Before updating,
note the old Audio Source and, if possible, keep a short DVR clip and listen to
the headphones in each source, to compare with afterwards.

1. Update, boot, do not open the Audio page. `/mnt/app/setting.ini` has a `[mixer]`
   section with `version=1`; `/tmp/audio_mixer.cmd` exists. Log: `audio mixer:
   migrated from record/audio_source=N`.
2. Record a clip in HDZero and, on Goggle 2 / BoxPro, with the built-in analog
   receiver: the same source as before is recorded at the same level (Mic, Line In
   with an external mic: Line In Boost 4 as before, A/V In).
3. Analog receiver (Goggle 2, BoxPro), old Audio Source Mic or Line In: switch to
   the built-in receiver; the headphones must not get receiver audio or hiss
   ("Analog RX Audio" = Off). Old source A/V In: receiver audio as before. Toggle
   Analog RX Audio, leave the menu: it follows. The Analog RSSI page stays silent.
4. Headphones after a source switch: A/V (AV-in jack or module bay) as loud as before
   (A/V In +6.0 dB, Headphones 31). Booting straight into HDZero now also has it
   (documented difference).
5. HDMI in: headphones silent; record a clip: the recording has the selected source.
6. Dial a slider (e.g. Headphones) quickly: the value follows without lag; listen for
   clicks or drop-outs while turning A/V In from Off to +6 dB and back, while switching
   DVR Audio As Heard <-> Custom during a recording, and when a DVR file starts and
   stops playing. Pull the power within 1 s of a dial step: the last second may be
   lost, a released slider never.
7. Line out volume and preamp scales: step Headphones 31 -> 1 and the boosts 0 -> 7
   and note the level; this decides whether the raw steps can be shown in dB.
8. As Heard with Mic at 0 dB: the recording is stereo (the mic in both channels),
   the headphones about 6 dB louder than one recorded channel; Headphones does not
   change the recording level.
9. Record daemon reset: start a recording after an SD repair or format on the
   Storage page, or play a DVR file and return: within about 2 s the log shows
   `codec changed by someone else` and `amixer contents` matches
   `/tmp/audio_mixer.cmd` again (line out 31, not 27; Lineout off with HDMI in).
10. Playback: both channels, Playback Volume applies, the live sources come back
    after leaving the player.

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
