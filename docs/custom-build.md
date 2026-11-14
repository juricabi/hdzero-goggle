# Custom build: what is new, and what it does after flashing

This firmware is the upstream HDZero goggle firmware (a85841d) with four features:

* **Module bay control** (pkendall64's analog-module-control): the goggle sets the
  channel of an analog module in the bay. [module-bay.md](module-bay.md)
* **Audio mixer**: the Audio page (headphone levels, what is recorded).
  [audio.md](audio.md)
* **Live stream audio**: the WiFi stream can carry audio. [live-audio.md](live-audio.md)
* **HDMI audio** (experimental): HDMI out carries the recorded audio; HDMI-in audio in the
  headphones and the recording. [hdmi-audio.md](hdmi-audio.md)

**Rule: after flashing and touching nothing, the goggle behaves like the stock firmware.**
Every new setting below is off, or set to what the stock firmware did, until it is changed.
This holds for a goggle that keeps its `setting.ini` from the stock firmware and for one
without a `setting.ini` (fresh install, or after Storage > Reset): both are shown below.

## Every new setting

`setting.ini` is `/mnt/app/setting.ini`; `record.conf` is
`/mnt/app/app/record/confs/record.conf` (replaced by every firmware update).

| Feature | Where to change it | Key | Default | What it changes |
|---|---|---|---|---|
| Module bay | Module Bay > Module | `[module] type` | 0 = **None** | None: the goggle never touches the bay pins (PG0-PG2, shared with WiFi). 1 Switch, 2 rapidFIRE, 3 SteadyView, 4 SteadyView X: the goggle sets the module's channel (needs the 3-wire mod). Any other value loads as None; the `[module] pins` key of test builds is not read |
| Module bay | Module Bay > Mode | `[module] setting` | 0 | the mode of the chosen module type (none for most) |
| Module bay | (channel selection with a bay module, Goggle 2) | `[module] channel` | the analog channel the goggle had (`[source] analog_channel`) | the bay module's own channel, separate from the built-in receiver's |
| Audio | Audio > Headphones > Volume | `[mixer] headphones` | 31 | headphone (line out) volume, 0 = off; not the recording |
| Audio | Audio > Headphones > Mic | `[mixer] mic` | 0 = off | the built-in mic in the headphones (-4.5..+6 dB) |
| Audio | Audio > Headphones > Line In | `[mixer] line_in` | 0 = off | the Line In jack in the headphones |
| Audio | Audio > Headphones > A/V In | `[mixer] av` | 8 = +6 dB | AV-in jack, bay module, built-in receiver in the headphones (stock: +6 dB) |
| Audio | Audio > Headphones > HDMI In | `[mixer] hdmi` | 4 = 0 dB | HDMI-in audio in the headphones; only with HDMI In Audio on |
| Audio | Audio > Headphones > Playback Volume | `[mixer] playback` | 27 | headphone volume while a DVR file plays (stock: 27) |
| Audio | Audio > Recording > Record Headphone Mix | `[mixer] rec_mix` | 0 = No | Yes: the headphone mix at its levels goes into the recording (the only way HDMI-in audio is recorded) |
| Audio | Audio > Recording > Record Mic / Line In / A/V In | `[mixer] rec_mic`, `rec_line_in`, `rec_av` | the stock Audio Source (`[record] audio_source`, default Mic): that one Yes, the others No | that source recorded at full level, heard or not (once only when it is already in the recorded mix) |
| Audio | Audio > Recording > Record Level | `[mixer] dvr_level` | 3 = 0 dB | ADC input gain of everything recorded, streamed and sent to HDMI out |
| Audio | Audio > Inputs > Mic Boost / Line In Boost | `[mixer] mic_boost`, `line_in_boost` | 4 / 4 | mic preamps (stock: 4 / 4) |
| Audio | Audio > Inputs > Built-in RX Audio (Goggle 2, BoxPro) | `[mixer] rx_audio` | On only if the stock Audio Source was A/V In | audio output of the built-in analog receiver (stock: the same rule) |
| Audio | (internal) | `[mixer] phones_at_boot` | 0 | 0: as stock, the headphones get the mix from the first switch to an analog source on (booting into HDZero leaves them silent). Set to 1 by the first change of a headphone level on the Audio page: from then on what the page shows is heard from boot |
| Audio | (internal) | `[mixer] version` | 2 | written with the section; a section without it (or of a test build) is taken over from `[record] audio_source` |
| Audio | (was Record Option > Audio Source) | `[record] audio_source` | 0 Mic | read once to set the defaults above; no longer written (the row moved to the Audio page) |
| Live stream audio | WiFi Module > System > Stream Audio | `[wifi] stream_audio` | false = Off | On: the RTSP stream gets an AAC track (written to `record.conf [live] audio` when the WiFi settings are applied) |
| Live stream audio | record.conf only | `[live] audio` | 0 | set by the app from Stream Audio |
| Live stream audio | record.conf only | `[live] audio_offset_ms` | 0 | delay of the stream's audio against its video (-500..500 ms), for lip sync |
| HDMI audio (Goggle 1, Goggle 2, BoxPro; experimental, NO-GO until the probe has run on the device) | Audio > HDMI Audio > HDMI Out Audio | `[hdmi] audio_out` | 0 = Off | On (exactly `1`), from the next start: the rc.sh step `hdmi_audio_boot` switches PH1-PH3 to I2S0 once per boot (only from the stock PH_CFG0 value), the IT66121 sends audio, the record daemon copies the recorded audio to it. Off: nothing reads or writes PH_CFG0 or any pin function |
| HDMI audio (Goggle 1, Goggle 2, BoxPro; experimental, NO-GO until the probe has run on the device) | Audio > HDMI Audio > HDMI In Audio | `[hdmi] audio_in` | 0 = Off | On (exactly `1`), from the next start: the EDID offers 48 kHz only; while HDMI in is shown, its audio is played into the codec (headphones at the HDMI In level, recorded with Record Headphone Mix) and the line out stays on during HDMI in |
| HDMI audio | record.conf only | `[hdmi_audio] latency_ms` | 60 (key absent) | buffer of the HDMI audio bridge |

Changed on purpose, also with every default: a few visible things (first-flash.md, "What
looks different from stock"; the full list as of 2989f32: review-final.md, "Differences from
stock on an untouched Goggle 1", most of them addressed since, status.md). Known
differences that remain:

* ELRS / MSP in analog mode: GET_BAND_CHAN / GET_FREQ answer the analog channel (255 / 0
  while the bay is not controlled); channel commands are ignored while the bay is not
  controlled; on a Goggle 1 whose last source was analog, the ELRS page's "Send VTX freq"
  and the dial's long-press confirm send nothing (upstream sent the HDZero channel).
* L band: the goggle's MSP frequency table has L1-L8 at 5362-5621 MHz; a Furious True-D's
  L band is LowRace 5333-5613 MHz. A VTX set by band and channel lands right; a VTX set by
  frequency on the L band ends up ~29 MHz from the True-D's channel.

* The Record Option page no longer has "Audio Source"; the same choice is Record Mic /
  Line In / A/V In on the Audio page (migrated, so the recording is the same).
* The audio mixer writes the codec in one batch and keeps it: the record daemon's mixer
  reset (SD check, SD format, DVR playback) is undone within about 250 ms (the watcher's
  period; at once after the Storage page's SD repair or format), where the stock firmware
  kept it until the next source switch; DVR playback uses both DAC channels and
  mutes the live sources (stock: left channel only, mixed with the A/V monitor); a
  change is made without the clicks of the stock one-control-per-process scripts.
  Details: [audio.md](audio.md), "Differences from the old firmware".
* At every boot `rc.sh` runs `/mnt/app/app/hdmi_audio_boot`; with HDMI Out Audio off it
  reads and writes no register, only the RAM file `/tmp/hdmi_audio.boot`. The app's own
  `aww` / `awr` calls (display set-up, the analog-RSSI ADC) take the lock
  `/tmp/.sunxi_dump.lock` shared by every `sunxi_dump` user (at most 1 s, then they run
  as stock did); the same registers and values as stock.

## Checked in the emulator

The SDL emulator of `all` (the code of 24d8ac8, after the `hdmi-hardening` merge; run
again with the code of 51bf28a on 2026-11-14: the same), Goggle 1 and Goggle 2, each started five ways
through `utilities/dev/emu_run.sh` with `utilities/dev/keys/check_start.txt`: nothing
touched for 6 s, then the Audio page opened and scrolled through, left, the Module Bay page
opened and left. "Stock setting.ini" is the file the stock app (upstream a85841d, in the
emulator) wrote at its first start, with `[record] audio_source` as its Record Option page
stores it. Same results on both goggles:

| Start | `setting.ini` after 6 s | the Audio page shows | codec batch at boot (the one stubbed `amixer -s`) |
|---|---|---|---|
| no setting.ini | the keys the stock app writes at its first start (`[settings] file_version`, `[osd]`, `[record] naming`, `[power]`, `[wifi]` defaults); no `[mixer]`, `[hdmi]`, `[module]`, `stream_audio` | Record Mic Yes, the other Record switches No, Record Headphone Mix No, Volume 31, Mic/Line In Off, A/V In +6.0 dB, HDMI In 0.0 dB greyed, Playback 27, Record Level 0.0 dB, boosts 4/4, Built-in RX Audio Off (Goggle 2), HDMI Out/In Audio Off, no Restart/Error marker | Lineout Switch 0, lineout volume 31; ADC from MIC1 only; every output-mixer switch 0 (nothing in the headphone mix while the line out is off); boosts 4/4, ADC gain 0 dB |
| the shipped `mkapp/app/setting.ini` (no file_version: the app runs `rm -f setting.ini` as stock, stubbed in the sandbox, so the file stays) | the shipped file plus the same stock keys; nothing of this build | as above | as above |
| stock setting.ini, Audio Source Mic | **unchanged** | as above | as above |
| stock setting.ini, Audio Source Line In | **unchanged** | Record Line In Yes, the others No | ADC from MIC2 only |
| stock setting.ini, Audio Source A/V In | **unchanged** | Record A/V In Yes, the others No, Built-in RX Audio On (Goggle 2) | ADC from LINEIN only |

In all ten runs opening the Audio page and the Module Bay page (Module None, Mode greyed,
"Status: Off, bay pins untouched") wrote nothing to `setting.ini`, and nothing was written
until the app was stopped; there was one amixer batch per run. No HDMI audio record
(`/tmp/hdmi_audio.boot`) exists in the emulator, so HDMI audio is not in effect and nothing
reads a register for it. The line out stays off until the first switch to an analog
source, as stock. Screenshots and logs: `$EMU_OUT/shots`, `$EMU_OUT/logs` (prefixes
`g1_`/`g2_` + start); the other key scripts of `utilities/dev/keys` were run on the goggle
named in their first line and their screenshots checked.

The same five starts on the **BoxPro** (2026-11-14, with HDMI audio enabled there; key
script `check_start_boxpro.txt`: 2 s untouched, the Audio page opened and scrolled down
to its last row, left; the BoxPro has no Module Bay): the same results as in the table
(Built-in RX Audio as on the Goggle 2); the page has the HDMI In row greyed and HDMI
Out/In Audio Off; nothing written to `setting.ini` at the start, by the page or until the
app was stopped; with no `setting.ini` the file after the start is identical to the one
upstream a85841d's BoxPro build writes, and the system commands are upstream's plus the
one amixer batch. Screenshots and logs: prefix `bp_`.

The language table (`TRANSLATE_STRING_NUM`, the entries one language file may have) is
raised from 384 to 512: with the Audio page, the stream and the module bay strings the
largest file (ja_jp) has about 378 entries after the merge, and entries beyond the table
are dropped silently (shown in English). The fonts were not regenerated: the Chinese and
Japanese strings use only characters the fonts already have (`check_strings.py`).

What the emulator cannot show: anything the hardware does (the codec, I2S, the bay pins,
the record daemon, WiFi). The routing that the defaults produce is checked against a model
of the codec by `utilities/audio_mixer/test_mixer.py` ("migration": stock firmware and
this one, per old Audio Source and flow, booting straight into HDZero included).

Note for anyone running the emulator: it executes the goggle's shell commands on the host,
among them the WiFi page's `/tmp/root_pw_set.sh` (`passwd` with the goggle's root
password) at every start. Run it only through `utilities/dev/emu_run.sh` (its own Xvfb, a
private mount namespace, an unprivileged uid, logging stubs for every system command).
