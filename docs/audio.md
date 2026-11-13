# Audio: the Audio page and the codec

All three targets (HDZero Goggle, Goggle 2, BoxPro) use the internal audio codec
of the Allwinner V536 (`sun8iw16`, ALSA card `audiocodec`). The firmware drives it
with `amixer`; the record daemon captures the ALSA `default` device (48 kHz stereo,
AAC). The control names and ranges below are the ones of the device's
`amixer contents` dump in `mkapp/app/app/record/audio/audiocodec/alsa-util/`,
their meaning comes from the Allwinner driver (`sound/soc/sunxi/sun8iw16-codec.c` of the
V536 BSP, Linux 4.9; checked against the copy in the Lindenis V536 SDK).

## In short

* **Headphones**: every source has a level in one analog mixer that feeds the headphone
  jack. 0 is off.
* **Recording**: one ADC records, and the same capture feeds the DVR, the WiFi stream audio
  (Stream Audio) and HDMI out audio (HDMI Out Audio). It takes
  * the headphone mix at its levels ("Record What I Hear"), and/or
  * each analog source straight from its input at full level ("Record Mic", "Record Line
    In", "Record A/V In"), whether it is heard or not.

  A source that is already in the recorded headphone mix is not taken a second time
  through its direct path (it would be twice as loud and slightly smeared); its Record
  switch then shows "Mix" next to it, and the hint says how it is recorded.
* HDMI-in audio can only be recorded through the headphone mix: there is no direct path
  from the DAC into the ADC.
* After a firmware update the page is set to what the stock firmware did: the old Audio
  Source recorded directly, A/V In heard at +6 dB, Record What I Hear = No.

## The Audio page

One page in four sections. The rows scroll: the selected row is always on screen, the
first row of a section brings its heading along, and a scroll bar on the right shows where
the window is. Below the list one line of help for the selected row.

| Section | Row | Values | setting.ini `[mixer]` | Default |
|---|---|---|---|---|
| Headphones | Volume | Off, 1..31 | `headphones` | 31 |
| | Mic | Off, -4.5..+6.0 dB | `mic` 0..8 | Off |
| | Line In | Off, -4.5..+6.0 dB | `line_in` 0..8 | Off |
| | A/V In | Off, -4.5..+6.0 dB | `av` 0..8 | +6.0 dB |
| | HDMI In (greyed while HDMI In Audio is not in effect; not on the BoxPro) | Off, -4.5..+6.0 dB | `hdmi` 0..8 | 0.0 dB |
| | Playback Volume | Off, 1..31 | `playback` | 27 |
| Recording (DVR, WiFi Stream, HDMI Out) | Record What I Hear | Yes / No | `rec_mix` | No |
| | Record Mic | Yes / No, "Mix" | `rec_mic` | the old Audio Source |
| | Record Line In | Yes / No, "Mix" | `rec_line_in` | the old Audio Source |
| | Record A/V In | Yes / No, "Mix" | `rec_av` | the old Audio Source |
| | Record Level | -4.5..+6.0 dB | `dvr_level` 0..7 | 0.0 dB |
| Inputs | Mic Boost | 0..7 | `mic_boost` | 4 |
| | Line In Boost | 0..7 | `line_in_boost` | 4 |
| | Analog RX Audio (Goggle 2, BoxPro) | On / Off | `rx_audio` | On only if the old Audio Source was A/V In |
| HDMI Audio (experimental; Goggle 1, Goggle 2; from the next start) | HDMI Out Audio | On / Off | `[hdmi] audio_out` | Off |
| | HDMI In Audio | On / Off | `[hdmi] audio_in` | Off |
| | < Back | | | |

Sliders: click to grab, turn, click to release; the value applies while it moves.
Switches apply on click. setting.ini is written on a switch click, a slider release,
leaving the page, and 1 s after the last dial step of a slider still grabbed (a power
cut loses at most that last second, never a released value).

What a pilot reads from it:

* "Mic at Off and still recorded?" Yes, when Record Mic = Yes: the Headphones section is
  what you hear, the Recording section what is recorded. The two are independent except
  through Record What I Hear.
* "Record Mic = No, but the mic is recorded?" Only through What I Hear: then the row
  shows "Mix" and the hint says "In What I Hear: recorded at its headphone level".
* Volume never changes the recording; Record Level changes the recording, the stream and
  HDMI out alike, never the headphones.
* Hints: Record source rows say "Recorded at full level, heard or not" (Yes), "Not
  recorded. Yes: at full level, heard or not" (No) or "In What I Hear: recorded at its
  headphone level" (in the mix). The two HDMI switches only store the setting and apply
  at the next start ("Restart to apply"); until then the row shows an orange "Restart",
  and "Error" with the reason in the hint if HDMI Out Audio was on at this start but
  stayed off (hdmi-audio.md, section 6).

## Signal paths

```
 codec input           preamp            output mixer gain     L/R OUTPUT MIXER          line out
 MIC1  built-in mic ──[MIC1 boost 0..7]─┬─[MIC1_G      -4.5..+6 dB]─sw─┐
 MIC2  "Line In" jack [MIC2 boost 0..7]─┼─[MIC2 BST    -4.5..+6 dB]─sw─┤
 LINEIN A/V (L/R) ──────────────────────┼─[LINEIN gain -4.5..+6 dB]─sw─┼──> L+R sum ─[lineout volume 0..31]─sw─> jack
 DAC   playback / HDMI-in audio ────────┼──────────────────────────sw─┘      (LINEOUTL/R Mux = OMIX_LR_SUM)
                                        │                                │
           Record Mic / Line In / A/V In│  sw: MIC1, MIC2, LINEIN        │ sw: "l_output/r_output mixer"
                                        v                                v     (Record What I Hear)
                          L/R ADC INPUT MIXER ─[ADC input gain -4.5..+6 dB]─ ADC ─[ADC volume, digital]─ AIF1
                                                                                  ─> record daemon: DVR, stream, HDMI out
```

* The ADC input mixers are plain DAPM mixers: one bit per input in `L_ADCMIX_SRC` /
  `R_ADCMIX_SRC` (MIC1 bit 6, MIC2 bit 5, LINEIN bit 2, output mixers bits 1/0), summed,
  no mux, no event that clears another input (driver: `ac_ladcmix_controls[]`,
  `SND_SOC_DAPM_MIXER_E("LADC input Mixer", ...)`, event `late_enable_adc` only clocks the
  ADC). So the output mixer and the direct inputs can be on together. The output mixers
  are plain mixers too. Nothing in the driver changes these switches behind user space
  (`codec_init` at probe and resume sets only the line-out volume and the preamps from the
  device tree, and resume writes the saved registers back).
* Per source: only the preamps (MIC1/MIC2 boost) and the three output mixer gains.
  The preamps act on both paths; the output mixer gains only on the way into the
  output mixer. The direct paths into the ADC are plain on/off switches; LINEIN
  has no gain of its own in front of the ADC at all. That is why a direct source is
  recorded "at full level" and why per-source record levels exist only through the mix.
* Shared: `ADC input gain control` (Record Level; analog, every ADC input), `ADC volume`
  (digital), `lineout volume` (Volume; analog, after the L+R sum, so it never reaches the
  ADC).
* Record What I Hear takes the output mixers into the ADC (`LADC input Mixer l_output
  mixer Switch`, `RADC input Mixer r_output mixer Switch`): the left ADC takes the left
  output mixer, the right ADC the right one. The recording is stereo and taken before the
  L+R sum and before `lineout volume`, while the headphones get left + right of both
  mixers (mono). A source in both output mixers (the mics, a mono A/V signal) reaches the
  line out from both, so it is about 6 dB hotter in the headphones than in each recorded
  channel (if the hardware sums; the driver calls it OMIX_LR_SUM, not measured).
* The DAC joins through `Left/Right Output Mixer DACL/DACR Switch`. The line out is
  mono: both LINEOUT muxes take the sum of the left and right output mixers.

### When a source is in the mix and recorded directly

A source counts as "in the recorded mix" when Record What I Hear is on and its level in
effect is above 0. Then its direct ADC switch stays off. The level in effect: while a DVR
file plays, the live sources leave the output mixer (only the file is heard), so a source
with Record = Yes is recorded directly during playback. When a level goes from 0 to above
0 (or back), the batch switches the mix path on before the direct path off (or the other
way round): the source is never missing from the recording in between, at most doubled for
the few milliseconds of the batch (`test_mixer.py` checks this per transition).

## What the firmware does

`src/core/audio_mixer_routing.c` turns the settings into one `amixer -q -s` batch
(every routing control set explicitly); `src/core/audio_mixer.c` writes it to
`/tmp/audio_mixer.cmd` and runs `amixer -q -s < /tmp/audio_mixer.cmd`.

Applying is asynchronous: callers only mark the state as changed, and a worker
thread writes the latest state, so a burst of dial steps ends in one or two amixer
runs, and the input thread (which holds `lvgl_mutex`) never waits for amixer or the
driver's PA sleep. Three callers wait for the codec: the recording start
(`dvr_select_audio_source()`, so a recording never starts with the previous routing),
DVR playback on/off (after `media_init()`, and before `media_exit()` closes the file's
stream, so the live sources are back before the DAC stops), and HDMI-in audio off (before
the bridge stops playing into the DAC).

The batch is ordered so that a change never disconnects everything in between:
a line out that goes off is switched off first; then the gains, every switch that
ends up on, the line out (switched on last when it is on), and only then every
switch that ends up off (and the output mixer gains of the sources that are off,
back to 0 dB). With no source connected, DAPM powers the line out down, which
switches the headphone PA off and on again (a click; the Goggle 2 driver also
sleeps `pa_msleep_time` = 160 ms in the PA-off event).

| Control | from |
|---|---|
| `MIC1_G boost stage output mixer control`, `MIC2 BST stage to L_R outp mixer gain`, `LINEINL/R to L_R output mixer gain` | Mic, Line In, A/V In: 0 = output mixer switch off, 1..8 = register 0..7 |
| `Left/Right Output Mixer MIC1Booststage / MIC2Booststage / LINEINL(R) Switch` | level above 0 (and no DVR playback) |
| `Left/Right Output Mixer DACL / DACR Switch`, `DAC volume` | DVR playback (152), or HDMI-in audio at the HDMI In level (152 + 2 x (level - 4)) |
| `LADC/RADC input Mixer l_output / r_output mixer Switch` | Record What I Hear |
| `LADC/RADC input Mixer MIC1 boost / MIC2 boost / LINEINL(R) Switch` | Record Mic / Line In / A/V In, unless that source is in the recorded mix |
| `MIC1 / MIC2 boost AMP gain control` | Mic Boost, Line In Boost |
| `ADC input gain control` | Record Level; `ADC volume` stays 160 |
| `lineout volume`, `Lineout Switch` | Volume (Playback Volume while a file plays); off with HDMI in (unless HDMI-in audio plays) and at boot until the first analog source (below) |

"Record Audio" on the Record Option page still decides whether the DVR records
audio at all. The old "Audio Source" row is gone: the same thing is Record Mic / Line In /
A/V In with one source set to Yes.

The mixer is applied at boot, on every change on the Audio page, when a recording
starts and on every source switch (the old `dvr_select_audio_source()` /
`dvr_enable_line_out()` call sites), when a DVR file starts and stops playing, and when
HDMI-in audio starts and stops.

### Headphones at boot (as stock)

The stock firmware set the headphone routing only when it switched to an analog source
(`dvr_enable_line_out(true)` in `app_switch_to_analog()`); booting straight into HDZero
(or HDMI in) left the record daemon's mixer in place, whose line out has only the idle DAC:
the headphones were silent until the first switch to an analog source. The mixer does the
same while `[mixer] phones_at_boot` = 0 (every migrated or fresh setting.ini): the line out
is off from boot until the first switch to an analog source; after it, the headphone mix
of the page is heard, also back in HDZero (as stock). The first change of a headphone level
on the Audio page (Volume, Mic, Line In, A/V In, HDMI In) sets `phones_at_boot` = 1 and
turns the line out on at once: from then on the page's headphone mix is heard from boot,
in every source (except HDMI in without HDMI-in audio). The recording does not depend on
it.

### Analog receiver audio

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
every DVR file played. When the daemon gets there is not known to the app.

So the mixer's worker watches the codec instead of guessing: while it has nothing
to apply, it reads every control of the last batch back every 2 s, straight from
`/dev/snd/controlC0` with ALSA control ioctls (`audio_mixer_codec.c`, no process),
and applies the current state again as soon as one differs. A reset is therefore
undone within about 2 s whenever it happens, and logged (`codec changed by someone
else`). If the codec does not keep a value (three re-applies in a row for the same
state), the worker logs it and stops re-applying until the state changes. `amixer
cset` by hand on one of the mixer's controls is undone within 2 s; to experiment,
change the setting on the Audio page or stop the app. The emulator has no codec, so it
does not watch.

The mixer is the only part of the firmware that writes these controls: the HDMI-in
audio code tells the mixer (`audio_mixer_set_live_dac()`) instead of running amixer, so
the watcher and the HDMI audio code never undo each other, and after a record daemon
restart the watcher alone puts the DAC source back.

### DVR playback

While a file plays: live sources out of the output mixer, DACL -> left and DACR -> right
output mixer, `DAC volume` 152 (8 steps of 0.75 dB = 6 dB below the 160 of before,
because the line out adds left and right: a mono recording keeps the old level instead
of +6 dB), line out = Playback Volume (also when Volume is 0, the source is HDMI in, or
before the first analog source). Afterwards the live mix is restored. Before: the
player's `AW_MPI_SYS_Init` switched `Left Output Mixer DACL` on with line out 27, so
playback was audible on the left DAC channel only, with the live A/V monitor still mixed
in, and the DAC switch and volume 27 stayed after playback.

### HDMI-in audio (HDMI In Audio = On)

The DAC has two uses. DVR playback (above) plays alone. HDMI-in audio, which the
record daemon's bridge plays into the DAC while HDMI in is shown (hdmi-audio.md), is a
live source mixed like the analog ones: `hdmi_audio_line_out()` in
`src/core/hdmi_audio.c` calls `audio_mixer_set_live_dac(true)` when the bridge route
starts and `false` (waiting for the codec) before it stops. Then DACL -> left and DACR
-> right output mixer at the HDMI In level (1..8 = -4.5..+6 dB around the playback
level: `DAC volume` 152 + 2 x (level - 4), 146..160, never above the 160 of before;
default 0 dB), the other live sources stay (mic + HDMI-in audio works), and the line out
stays on (Volume permitting) although HDMI in is the source. Record What I Hear records
it; the direct Record switches cannot (no analog path from the DAC into the ADC; the
digital loopbacks `AIF1 AD0L/R Mixer AIF1 DA0L/R Switch`, `DACL/R Mixer ADCL/R Switch`
are never set, the test checks every batch). DVR playback takes precedence.

With HDMI In Audio off (the default) nothing calls it: HDMI in mutes the line out as in
the stock firmware, and the HDMI In row is greyed out.

### Per-source record levels: what the codec allows (evaluated, not implemented)

Asked for: DVR levels per source, independent of the headphone levels, e.g. two
source groups on the two ADC sides with their own digital volume, averaged to mono
by the codec (`AIF1OUT0L/R Mux` = `AVE_AIF1AD0L_AIF1AD0R`). Checked against the
driver and the dumps:

* Separate left/right ADC input mixers, a switch per source and side: yes.
* An independent digital level per side: yes. `ADC volume` is a double control
  (left bits 15..8, right 7..0, 0..255, 0.75 dB steps). Above 160 it is digital gain on
  the ADC output: no headroom is gained and the noise rises with it.
* The codec summing or averaging the two sides: no, not through the driver. The
  mux enums list SUM and AVE, but `ac_dapm_routes[]` has routes only for
  `AIF1_AD0L` / `AIF1_AD0R`; with SUM/AVE the mux has no input, is powered down,
  and that capture channel goes off. There is no digital cross-mix either.
  (The codec model reproduces this; the test checks it.)

So, with this driver, per-source record levels exist only through the headphone mix
(Record What I Hear: the recording follows the headphone levels), plus the direct inputs
at one shared Record Level. A split-stereo recording (group A left, group B right, own
digital levels) is possible but not implemented: players put the two on separate ears. A
mono mix with independent levels needs a kernel change (the four missing AVE routes) or a
software mix in the record daemon.

## Levels and defaults

* 0 dB of a source = gain register 3, the driver default seen in the dump. The
  sources can only be trimmed between -4.5 and +6 dB; below that there is only off.
* Mixing adds signals: two sources peaking together can reach +6 dB over either one.
  With Record What I Hear that sum goes into the ADC, plus any direct source, so keep
  mixed hot sources at 0 dB or below, or lower Record Level (down to -4.5 dB). Nothing in
  this path limits; whether it clips audibly has not been measured.
* What the page shows in dB, and why: only the values whose dB scale the BSP driver
  and the mainline driver of the same analog block (A64, `sun50i-codec-analog.c`)
  agree on: the three output mixer gains and the ADC input gain (Record Level),
  register 0..7 = -4.5..+6 dB in 1.5 dB steps. Everything else is shown in raw steps: the
  BSP gives `lineout volume` the -4.5 dB/1.5 dB scale of the mixer gains while the A64
  layout is 0..1 = mute, 2..31 = -43.5..0 dB; the preamps 0..14 dB (BSP) or 0 = 0 dB,
  1..7 = 24..42 dB (A64). A step on a real goggle should settle it.
* `ADC volume` and `DAC volume` (digital, 0.75 dB steps) stay at 160, the value
  libmedia_mpp sets and the old firmware recorded and played with.
* Defaults when `[mixer]` is missing, or of version 1 (test builds before this page; its
  keys `dvr_mode`, `dvr_mic`, `dvr_line_in`, `dvr_av` are removed): taken over from
  `[record] audio_source`, so the goggle records and plays exactly what it did before:
  * Record Mic / Line In / A/V In: only the old source = the old
    `audio_sel.sh in_mic1 / in_mic2 / in_linein` routing; Record What I Hear = No;
  * headphones as the old `out_on`: A/V +6 dB (register 7), line out 31, mics off, and
    silent at boot until the first analog source (`phones_at_boot` = 0);
  * Mic/Line In Boost 4 and Record Level 3 (0 dB): what libmedia_mpp and the driver
    have always set; Playback Volume 27: libmedia_mpp's line out level. The boosts come
    from the device tree (`[codec] maingain` / `headsetmicgain` in sys_config.fex); they
    are 4 / 4 in the official images of all three targets (Goggle Rev20250319, Goggle 2
    Rev20250715, BoxPro Rev20250517).
  * A fresh install has no `[record] audio_source`: Mic, as the stock default.

## Targets

* Goggle 2: A/V = LINEIN = AV-in jack, module bay and the internal receiver.
* Goggle (1): no internal receiver, A/V = AV-in jack / module bay; the RTC6715
  calls are no-ops, no Analog RX Audio row.
* BoxPro: same codec and the same three inputs, internal receiver like Goggle 2. Same
  page, smaller rows.
* All three have HDMI in, but HDMI audio is supported on Goggle 1 and Goggle 2 only (the
  BoxPro's device tree and board were not examined): the BoxPro's page has neither the
  HDMI In row nor the HDMI Audio section (the paths are unproven on the goggles too,
  hdmi-audio.md).

## Differences from the old firmware

For a unit that is updated and never opens the Audio page, `utilities/audio_mixer/
test_mixer.py` checks that recording, headphones (sources and gains), line out power
and the analog receiver's audio switch are what the old firmware produced, for every
old Audio Source, after a source switch, with HDMI in, at recording start and when
booting straight into HDZero. What differs:

* The record daemon's mixer reset (boot, SD check, SD format, DVR playback): the old
  firmware kept it until the next source switch or recording start (line out 27, the
  line out on even with HDMI in, the mic into the left ADC); now it is undone within
  about 2 s. Booting straight into HDZero the line out is switched off instead of left on
  with nothing audible connected (silent either way).
* DVR playback: both DAC channels, live sources muted, Playback Volume; before it was
  the left DAC only with the live mix and line out 27, which stayed after playback.
* HDMI in: line out off as before; the old `out_off` also set the LINEOUT muxes to
  their first item, now they stay at OMIX_LR_SUM (no effect while the line out is off).
* Changes are click-free where the batch can make them so; the old scripts switched one
  control per amixer process.

## Audio page layout

The page has its own columns (`conf/ui.h` `UI_AUDIO_*`, per target): label, two button
columns (arrow + button, the sliders span both), value, without column gaps, and rows as
tall as on the other pages (60 px goggles, 40 px BoxPro). The list is a scrollable
container (`LV_OBJ_FLAG_SCROLLABLE`, scrolled only by the selection): `ui_main_menu.c`
calls `lv_obj_scroll_to_view()` on the selected row of every page after each dial step,
which does nothing where the container is not scrollable (every other page). Headings
have the row height and the window is 11 rows, so scroll positions are whole rows.

`utilities/audio_mixer/check_strings.py` measures every string of the page in every
language with the firmware's LVGL fonts (built for the host) against its column, on the
goggles and the BoxPro, and checks that the font has every character. The CJK parts of
the fonts hold only the characters of the translations they were made from
(`utilities/font/generate_font_lib.py`): new Chinese and Japanese strings must use those
or the fonts must be regenerated. All strings fit (widest: a Russian hint at 95 % of the
row, the Japanese "Mix" at 96 % of the value column).

## Testing on a goggle

The host tests cover the routing logic; these need the hardware. Before updating,
note the old Audio Source and, if possible, keep a short DVR clip and listen to
the headphones in each source, to compare with afterwards.

1. Update, boot into HDZero, do not open the Audio page: the headphones are silent as
   before. `/mnt/app/setting.ini` has `[mixer]` with `version=2`, `phones_at_boot=0`;
   `/tmp/audio_mixer.cmd` exists. Log: `audio mixer: migrated from record/audio_source=N`.
2. Record a clip in HDZero and, on Goggle 2 / BoxPro, with the built-in analog
   receiver: the same source as before is recorded at the same level.
3. Analog receiver (Goggle 2, BoxPro), old Audio Source Mic or Line In: switch to
   the built-in receiver; no receiver audio ("Analog RX Audio" = Off). Old source
   A/V In: receiver audio as before. The Analog RSSI page stays silent.
4. Headphones after a switch to analog: A/V (AV-in jack or module bay) as loud as before.
   Back in HDZero it stays (as before).
5. HDMI in: headphones silent; record a clip: the recording has the selected source.
6. Audio page: set Mic to 0 dB: the mic is heard at once, and after a reboot into HDZero
   too. Record What I Hear = Yes: Record Mic shows "Mix"; record a clip: the mic once (not
   doubled), at its headphone level. Mic to Off: Record Mic loses "Mix", the clip has the
   mic at full level again.
7. Dial a slider quickly: the value follows without lag; listen for clicks or drop-outs
   while turning A/V In from Off to +6 dB and back, while switching Record What I Hear
   during a recording, and when a DVR file starts and stops playing.
8. Line out volume and preamp scales: step Volume 31 -> 1 and the boosts 0 -> 7 and note
   the level; this decides whether the raw steps can be shown in dB.
9. Record daemon reset: start a recording after an SD repair or format on the Storage
   page, or play a DVR file and return: within about 2 s the log shows `codec changed by
   someone else` and `amixer contents` matches `/tmp/audio_mixer.cmd` again.
10. Playback: both channels, Playback Volume applies, the live sources come back.

## Checking without hardware

`utilities/audio_mixer/test_mixer.py` (python3 and a host C compiler, ~30 s) builds
the real `src/core/audio_mixer.c` and `audio_mixer_routing.c` for the host three
times (`-DHDZGOGGLE`, `-DHDZGOGGLE2`, `-DHDZBOXPRO`) with a harness
(`harness/harness.c`: real minIni and setting.ini, the worker thread, a simulated
codec that takes the batches the code writes for `amixer -q -s`) and checks them
against `codec_model.py`, a model of the codec's signal graph made of the driver's
DAPM routes (`ac_dapm_routes[]`). For a codec state the model gives, per sink (recording
left / right, line out left / right), every source that reaches it and the gain settings
on the way. The test asserts:

* routing: for a matrix of mixer states (the mix alone, direct sources alone, the mix
  with direct sources at level 0 and above 0, every source, headphones 0, HDMI in,
  playback, out-of-range values, HDMI-in audio on and off) the sources and gains of the
  recording and the headphones are what this document specifies (a source in the
  recorded mix only once), and every batch line is a control of the device's `amixer
  contents` dump with a value in range, set once, never a digital DAC/ADC loopback;
* loading: migration from each old Audio Source (and -1, 3, 258, read as the old
  firmware did), what is written to `[mixer]`, clamping, missing fields, a version 1
  section; the page's "in the mix" query;
* migration equivalence: per target and old Audio Source, the old firmware (driver
  defaults + the device tree's preamp values, libmedia_mpp's reset, then the real
  `mkapp/app/script/audio_sel.sh` sequences of a source switch, HDMI in, the recording
  start, and booting straight into HDZero) against the new one: recording, headphones,
  line out power and the analog receiver's audio switch must be equal. Also with the
  record daemon resetting the codec after the app has routed it;
* headphones at boot: silent until the first analog source while untouched; a recording
  change keeps that, a headphone level change makes them live (and is stored);
* transitions: replaying the batch of a change line by line on the model, the line
  out never loses power, the recording never loses its sources, a source recorded before
  and after the change is recorded all the way through (the mic moving between its
  direct path and the mix), and a line out that goes off goes off first;
* worker: `audio_mixer_apply()` returns at once even when amixer takes 150 ms, 20
  dial steps coalesce into a few batches ending at the last value, setting.ini is
  only written by `audio_mixer_save()`;
* watcher: a libmedia_mpp reset is undone; a control the codec does not keep stops
  after three re-applies.

`--driver <BSP>/sound/soc/sunxi/sun8iw16-codec.c` also checks that every modelled
route is one of the driver's (done with the Lindenis V536 SDK copy). `utilities/
audio_mixer/mutants.py` (~10 min) applies 32 small mutations to a copy of the sources
(ADC switches swapped, What I Hear inverted, a heard source recorded twice, the old Custom
rule, the level in effect vs the configured level, the batch order, migration, the boot
behaviour, the version 1 section, the worker, the watcher, the live DAC, ...) and fails
unless the test catches every one. `check_strings.py` checks the page's strings (above).

`utilities/audio_mixer/stub/amixer` logs what it is given; put it first in `PATH`
(with `AMIXER_LOG`) to see the batches the emulator UI sends. On a goggle,
`/tmp/audio_mixer.cmd` holds the last batch and `amixer contents` shows the result.

What is proven only on the host or in the emulator: the routing logic against a model of
the driver, the order of every batch, migration, the page (layout, scrolling, strings,
widths). What needs a real goggle: everything audible (levels, clicks, noise, clipping of
mixed sources, the dB scale of the raw steps, whether the output mixers really sum into
the line out), the record daemon's reset timing, and every HDMI audio path.
