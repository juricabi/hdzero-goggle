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
| Audio | Audio > Recording > Record What I Hear | `[mixer] rec_mix` | 0 = No | Yes: the headphone mix at its levels goes into the recording (the only way HDMI-in audio is recorded) |
| Audio | Audio > Recording > Record Mic / Line In / A/V In | `[mixer] rec_mic`, `rec_line_in`, `rec_av` | the stock Audio Source (`[record] audio_source`, default Mic): that one Yes, the others No | that source recorded at full level, heard or not (once only when it is already in the recorded mix) |
| Audio | Audio > Recording > Record Level | `[mixer] dvr_level` | 3 = 0 dB | ADC input gain of everything recorded, streamed and sent to HDMI out |
| Audio | Audio > Inputs > Mic Boost / Line In Boost | `[mixer] mic_boost`, `line_in_boost` | 4 / 4 | mic preamps (stock: 4 / 4) |
| Audio | Audio > Inputs > Analog RX Audio (Goggle 2, BoxPro) | `[mixer] rx_audio` | On only if the stock Audio Source was A/V In | audio output of the built-in analog receiver (stock: the same rule) |
| Audio | (internal) | `[mixer] phones_at_boot` | 0 | 0: as stock, the headphones get the mix from the first switch to an analog source on (booting into HDZero leaves them silent). Set to 1 by the first change of a headphone level on the Audio page: from then on what the page shows is heard from boot |
| Audio | (internal) | `[mixer] version` | 2 | written with the section; a section without it (or of a test build) is taken over from `[record] audio_source` |
| Audio | (was Record Option > Audio Source) | `[record] audio_source` | 0 Mic | read once to set the defaults above; no longer written (the row moved to the Audio page) |
| Live stream audio | WiFi Module > System > Stream Audio | `[wifi] stream_audio` | false = Off | On: the RTSP stream gets an AAC track (written to `record.conf [live] audio` when the WiFi settings are applied) |
| Live stream audio | record.conf only | `[live] audio` | 0 | set by the app from Stream Audio |
| Live stream audio | record.conf only | `[live] audio_offset_ms` | 0 | delay of the stream's audio against its video (-500..500 ms), for lip sync |
| HDMI audio | Audio > HDMI Audio > HDMI Out Audio | `[hdmi] audio_out` | 0 = Off | On: the IT66121 sends audio, the record daemon copies the recorded audio to it. Applies at once |
| HDMI audio | Audio > HDMI Audio > HDMI In Audio | `[hdmi] audio_in` | 0 = Off | On: while HDMI in is shown, its audio is played into the codec (headphones at the HDMI In level, recorded with Record What I Hear) and the line out stays on during HDMI in; the EDID offers 48 kHz only from the next start |
| HDMI audio | record.conf only | `[hdmi_audio] latency_ms` | 60 (key absent) | buffer of the HDMI audio bridge |

Changed on purpose, also with every default: nothing. Known differences that remain:

* The Record Option page no longer has "Audio Source"; the same choice is Record Mic /
  Line In / A/V In on the Audio page (migrated, so the recording is the same).
* The audio mixer writes the codec in one batch and keeps it: the record daemon's mixer
  reset (SD check, SD format, DVR playback) is undone within about 2 s, where the stock
  firmware kept it until the next source switch; DVR playback uses both DAC channels and
  mutes the live sources (stock: left channel only, mixed with the A/V monitor); a
  change is made without the clicks of the stock one-control-per-process scripts.
  Details: [audio.md](audio.md), "Differences from the old firmware".

## Checked in the emulator

The SDL emulator of this branch (Goggle 2 and Goggle 1), started five ways, nothing touched
for 6 s, then the Audio page opened (the page writes nothing when it is only opened):

| Start | `[mixer]` written at boot | codec batch at boot | `[hdmi]` | `[module]` | `[wifi] stream_audio` |
|---|---|---|---|---|---|
| no setting.ini | Record Mic Yes, others No, What I Hear No, A/V +6 dB, Volume 31, Playback 27, boosts 4/4, Record Level 0 dB, RX audio Off, `phones_at_boot=0` | line out off; ADC from MIC1 only; A/V in the output mixer at +6 dB | absent (Off) | absent (None) | false |
| the shipped `mkapp/app/setting.ini` (no file_version: the app resets it, as stock) | as above | as above | absent | absent | false |
| stock setting.ini, Audio Source Mic | as above | as above | absent | absent | false |
| stock setting.ini, Audio Source Line In | Record Line In Yes, others No | ADC from MIC2 only | absent | absent | false |
| stock setting.ini, Audio Source A/V In | Record A/V In Yes, others No, RX audio On | ADC from LINEIN only | absent | absent | false |

In every case the Audio page shows these values, HDMI In greyed out, HDMI Out Audio and
HDMI In Audio Off; nothing new is written to `setting.ini` except `[mixer]` (and
`[wifi] stream_audio=false`, written with the other WiFi keys by the WiFi page's boot
action). The line out stays off until the first switch to an analog source, as stock.

The module bay rows of the table follow the final module-bay branch (`module-control`
9a30a84: one Module setting, default None). This branch still carries its earlier state
(Type Unknown and Pin Control Off by default, the same "bay pins never touched"); check the
table again in the emulator after the module-bay branch is merged.

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
password) at every start. Run it with stubs first in `PATH` for `passwd`, `insmod`,
`ifconfig`, `hostapd`, `udhcpd`, `route`, `killall`, `aww`, `dispw` and the like.
