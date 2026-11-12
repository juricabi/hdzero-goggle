# Live stream audio (RTSP AAC track)

The WiFi live stream (`rtsp://<goggle-ip>:8554/hdzero`) can carry the goggle's audio as an
AAC track next to the video. It is off by default: **WiFi Module > Page: System > Stream Audio**.
With it off the stream is the video-only stream it always was.

Applies to HDZero Goggle (WiFi on the expansion module), Goggle 2 (built-in WiFi) and BoxPro:
they share `record`, `rtspLive`, the record conf and the WiFi page.

## 1. The live path (as found, before this change)

### Processes and configuration

| What | Where |
|---|---|
| `record` starts at boot and stays up (recording *and* live encoding) | `mkapp/app/script/rc.sh` |
| `rtspLive` (live555 RTSP server) starts with WiFi: the main app writes `/tmp/wlan_start_ap.sh` / `/tmp/wlan_start_sta.sh` ending in `/mnt/app/app/record/rtspLive > /tmp/rtspLive.log` | `src/ui/page_wifi.c` `page_wifi_update_services()` |
| WiFi off: `gogglecmd -live quit`, `killall rtspLive` | `mkapp/app/script/wlan_stop.sh` |
| Both read the record conf: `/data/confs/record.conf` if it exists, else `<exe dir>/confs/record.conf` = `/mnt/app/app/record/confs/record.conf`, which is `REC_CONF`, the file the main app writes (`src/core/dvr.c`) | `record_checkConf()` (`src/record/main.c`), `live_checkConf()` (`src/rtspLive/main.cpp`) |
| rtspLive settings: `[live] multicast` (shipped `0`: unicast RTSP), `port` 8554, `name` hdzero, `[venc_live] h265` (shipped `0`: H.264) | `src/rtspLive/stream/confparser.c` |

### Video, end to end

1. A client sends DESCRIBE. `H264LiveVideoServerMediaSubssion` (an `H264VideoFileServerMediaSubsession`,
   `reuseFirstSource`) builds its SDP from a dummy source: `H264FramedLiveSource` ->
   `avshare_start()` attaches the shared memory (resetting it) and sets the *video connected*
   byte; `getAuxSDPLine()` waits for SPS/PPS; the dummy is closed again (`avshare_stop()`).
   SETUP/PLAY create the real source the same way.
2. `record`'s main loop polls every 100 ms (`live_run()`): video connected -> `live_start()`: a
   second VI channel + VENC from `[venc_live]` (shipped: 1280x720, 30 fps, 2 Mbit/s, H.264 CBR);
   not connected -> `live_stop()`. The live encoder is independent of recording.
3. `vvLive_onFrame()`: the first frame must be an I frame (else an I frame is requested); before
   every I frame the SPS/PPS from `vi2venc_getSpsPpsInfo()` is queued as a frame of its own; each
   frame goes to the ring with `avshare_addvid()`.
4. rtspLive reads with `avshare_readFrame()` (all filled slots concatenated, as a byte stream) into
   `H264VideoStreamFramer`, which parses NAL units and **makes up its own presentation times**
   (wall clock at the first NAL + n / frame rate from the SPS, default 30). The timestamp in the
   ring (ms since the first live frame, passed through a `uint32_t`) was never used. The parser
   also holds the last NAL of each frame until the next frame's start code arrives (one frame of
   latency).

Multicast (`[live] multicast=1`, code default when the key is missing) uses a
`PassiveServerMediaSubsession` with the same source and framer.

### Audio in `record`

Only `record_start()` with `[record] audio=1` ("Record Audio") creates an AI channel bound to an
AENC channel (`src/record/ai2aenc.c`: `[ai]` 48 kHz / 16 bit / 2 ch from device 0, `[aenc]` AAC
64 kbit/s, `attachAACHeader = 1` -> ADTS), and `aa_onFrame()` hands the frames to the TS/MP4
muxer only. So **during live streaming there is no audio unless a recording with audio is
running, and never with Record Audio off**. Nothing feeds the audio side of the ring:
`avshare_addAudio()` (record) and `avshare_readAudio()` (rtspLive) are unused leftovers
(`addAudio` expects TS PES fragments; `readAudio` computes `tv_usec = ts*100/9` without a modulo).
Input routing (`audio_sel.sh`, applied when entering video mode) is unchanged by this work.

### The shared ring (`src/record/avshare.c` writer, `src/rtspLive/stream/avshare.c` reader)

SysV shm key `0x568066`, 632 578 bytes, identical constants on both sides:

| Offset | Size | Content |
|---|---|---|
| 0 | 2 | connect bytes: [0] video reader, [1] audio reader (set by rtspLive) |
| 2 | 56 x 16 | video slot headers |
| 898 | 56 x 16 | audio slot headers |
| 1 794 | 56 x 10 KiB | video slot data |
| 575 234 | 56 x 1 KiB | audio slot data |

Slot header, `#pragma pack(1)`, 16 bytes, same on both sides: `char wflag` (1 = free, 0 = filled),
`char frameType`, `char mediaType`, `char buff_type` (BEGIN / MIDDLE / END / BEG_END: a frame
spans one or more consecutive slots), `uint64_t timestamp`, `int length`.

Writer and reader keep their own slot index. The writer never overwrites a filled slot: when its
next slot is not free the frame is dropped (`avshare_add()` may leave the first slots of a
multi-slot frame behind) and video then drops everything until the next I frame. When the reader
finds its slot free it scans for any filled slot. Writing to a reader that is not connected
returns -3 and resets the writer to slot 0.

Defects found on the way (fixed where the new path relies on them):
- rtspLive `avshare_connect()`/`avshare_disconnect()` indexed by media *type* (1/2) instead of
  index (0/1): disconnecting video reset the **audio** headers, connecting audio wrote out of
  bounds. Fixed.
- No memory barriers between writing a slot and flipping `wflag` (ARM is weakly ordered). Added.
- Video timestamps truncated to 32 bit by `avshare_add(..., uint32_t millis, ...)`. Now 64 bit.
- `audioStreamSize` / `largeStreamSize` macros are not parenthesized (`1 * 1024`). The new code
  parenthesizes its uses.
- `disk_checkFile()` (record `disk.c` and rtspLive `main.cpp`) tested `st_mode` after a failed
  `stat()`: a missing `/data/confs/record.conf` could be taken as present (stack garbage) and the
  process then ran on built-in defaults (rtspLive: multicast, H.265, no `[live] audio`). Fixed.

### live555 in `lib/live`

Prebuilt static libraries, version 2022.12.01. Present and used: `OnDemandServerMediaSubsession`,
`FramedSource`, `MPEG4GenericRTPSink`, `H264VideoStreamDiscreteFramer`,
`H265VideoStreamDiscreteFramer` (virtual `nalUnitEndsAccessUnit()`), `RTCPInstance`.
Present but not suitable: `ADTSAudioFileServerMediaSubsession` / `ADTSAudioFileSource` (read a
file), `ADTSAudioStreamDiscreteFramer` (*adds* ADTS headers).

## 2. What was added

### Setting

- `[wifi] stream_audio` in `setting.ini`, default `false` (`src/core/settings.{c,h}`).
- WiFi Module, page System: **Stream Audio On/Off** between SSH and Apply Settings
  (`src/ui/page_wifi.c`). Applying the WiFi settings (also done once after boot) writes
  `[live] audio=0|1` into the record conf **before** rtspLive is (re)started.
- `[live] audio` is read by rtspLive at start (`conf_loadLiveParams()`) and by `record` when the
  audio reader connects (`conf_loadLiveAudio()`). Shipped `record.conf` has `audio=0`.

### `record` (`src/record/live_audio.{c,h}`, hooks in `main.c`)

- `liveaudio_run()` is called from `live_run()`. When rtspLive connects the audio side of the ring
  (and `[live] audio=1`), a **dedicated AI -> AENC chain** (`ai2aenc` instance of its own, AAC,
  ADTS, `[ai]`/`[aenc]` conf) starts, independent of recording and of Record Audio. It stops 3 s
  after the reader goes away (DESCRIBE and SETUP each connect, the delay avoids a restart in
  between); a failed start is retried after 5 s.
- The chain captures from the same `[ai] device` as recording, so whatever input routing/mixer
  settings the main app applies to the codec (`audio_sel.sh`, applied on source selection) are
  what the stream carries. HDMI-in does not select an input (out of scope here).
- Recording with audio and live audio can run at the same time (two AI channels on the device).
  Stopping either no longer disables the AI device while the other uses it (`record_stop()`,
  `AIO_cmdSTOP`).
- `liveaudio_onFrame()` queues each ADTS frame **with its header** via the new
  `avshare_addAudioFrame()`: all or nothing (a frame never ends up half in the ring), slots
  published in order behind a barrier. Ring full -> the frame is dropped and counted.
- Timestamps: `liveclock_stamp()` turns encoder pts (us) into **CLOCK_MONOTONIC us**. MPP stamps
  captured audio with `CDX_GetSysTimeUsMonotonic()` (seen in `audioHw_AI_CapThread`); a pts that
  is up to 1 s in the past is taken as the capture time as it is, any other clock is anchored to
  the arrival of the first frame and then follows the encoder pts; it re-anchors if the stamps
  leave [now - 2 s, now + 0.2 s]. Live video frames (`vvLive_onFrame()`) get the same treatment,
  so both media share one clock.

### `rtspLive`

- `[live] audio=1`: the session gets an `AACLiveAudioServerMediaSubsession` (`track2`) and the video
  subsession switches to `LiveNalVideoSource` + `Live{H264,H265}VideoStreamDiscreteFramer`. With
  `audio=0` the server builds exactly the session it built before (same classes, same source).
- Audio: `LiveAudioSource` reads one frame at a time (`avshare_readMediaFrame()`), checks and strips
  the ADTS header, delivers the raw AAC access unit with presentation time = ring timestamp
  converted to wall clock (`avshare_timestampToTimeval()`: one process-wide CLOCK_MONOTONIC ->
  gettimeofday offset, so audio and video stay consistent and RTCP sender reports, which pair
  wall clock with RTP time, are right). Frames older than 1 s (a paused or stalled reader) are
  skipped, so the audio never lags.
- Payload: RFC 3640 `MPEG4-GENERIC`, mode `AAC-hbr`, one AU per RTP packet, RTP clock = sample
  rate. SDP for the shipped conf:
  ```
  m=audio 0 RTP/AVP 97
  a=rtpmap:97 MPEG4-GENERIC/48000/2
  a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1190
  ```
  `config` is the AudioSpecificConfig built from the **first ADTS header the encoder actually
  produces** (object type = profile + 1, frequency index, channel configuration; AAC-LC 48 kHz
  stereo = `1190`). The subsession waits for it at DESCRIBE (nested event loop, like the H.264
  subsession waits for SPS/PPS); frames in another format later are dropped.
- **No audio frame within 2.5 s** (chain failed, setting off on the record side): the audio
  subsession returns no RTP sink, live555 leaves it out of the SDP and the client gets the
  video-only session; the next DESCRIBE tries again.
- Video with audio on: each ring frame (`avshare_readMediaFrame()`) is split into NAL units, start
  codes removed, every NAL carries the frame's own timestamp; the RTP marker is set on the last
  VCL NAL of a frame only (`nalUnitEndsAccessUnit()` override). Same 1 ms polling as before; no
  NAL waits for the next frame, so this path is one frame *faster* than the parser path.
- Ring reader (`src/rtspLive/stream/avshare.c`): media attached/connected per media with a user
  count (`avshare_startMedia()` / `avshare_stopMedia()`; `avshare_start()`/`avshare_stop()` are the
  video case, unchanged in effect), connect resets that media's slots, frame-by-frame reader with
  resync, release of orphan slots and recovery from partial frames, `avshare_peekAudio()`.
- Multicast mode: audio is not supported (logged), video only as before.

### Behaviour summary

| Situation | Stream |
|---|---|
| Stream Audio off | video only, unchanged path |
| On, audio chain runs | video (NAL path) + AAC, shared clock |
| On, no audio frame (chain failed / not producing) | video only (track left out of SDP), DESCRIBE takes up to 2.5 s longer |
| On, Record Audio off | audio present (live chain does not depend on it) |
| On, recording with audio at the same time | both chains run; start/stop of one does not stop the other |
| Client paused / slow | ring holds ~1.2 s of audio, then drops new frames; reader skips frames older than 1 s |
| Several clients | one shared source per track (`reuseFirstSource`), the ring has one reader per media |

## 3. Tests run on the build host

All host tests are in the session scratchpad (`rtsp/test/`), nothing of it is in the tree.

- Cross builds (Release, `-Werror`): Goggle, Goggle 2, BoxPro build; the only warnings are the two
  pre-existing `liblog.so` linker warnings. New files also compiled natively with
  `-Wall -Wextra`: no warnings.
- Ring unit test (`ring_test.c`): both avshare implementations linked into one x86 binary
  (globals renamed with objcopy), sharing the real SysV segment. 7186 checks, 0 failures, also
  under ASan/UBSan: 2000 ADTS frames of 157..3012 bytes (1..3 slots) across many wrap-arounds;
  overrun (writer stalls after 29 frames, 71 whole frames dropped, no partial frame, queued frames
  read back intact and in order, then normal operation); peek; reader reconnect while the writer
  index is elsewhere; video partial-frame recovery with the legacy writer (18 whole frames, the
  partial one dropped, resync on the next I frame); legacy `avshare_start()`/`readFrame()` path;
  ~200 000 frames with concurrent writer/reader threads (0 corrupt/out of order, every queued
  frame read); monotonic -> wall clock conversion.
- End to end: the **real rtspLive** built natively against live555 2023.06.14 (same classes as
  the 2022.12.01 prebuilt), fed by a stand-in for `record` that uses the real `avshare.c`,
  `live_audio.c` and `confparser.c` with the MPP chain stubbed (H.264 file at 30 fps with SPS/PPS
  before each I frame, ADTS file AAC-LC 48 kHz stereo in real time, both cut from one timeline
  with aperiodic flash/beep markers). Clients: ffprobe, ffmpeg (UDP and TCP), openRTSP.
  - Stream Audio on: SDP exactly as above (`config=1190`), ffprobe sees `h264` + `aac (LC) 48000 Hz
    stereo`, a 15 s ffmpeg capture holds 450-451 video and 705-706 audio packets that decode without errors.
  - Lip sync: flash -> beep offset in the capture is 20.7..21.8 ms (UDP and TCP) once the client
    has the RTCP sender reports, against 21.5 ms in the source files themselves (AAC encoder delay
    of the test file), i.e. < 1 ms error. Before the first sender report (about 2.5 s, live555
    does not send one while the RTP-Info timestamp is preset) ffmpeg aligns the tracks by their
    first packets: 35 ms off in these runs, then it corrects.
  - Latency, capture -> reception at the client (includes the 15 ms the stand-in adds as encoder
    delay): audio-on video path 15.6..17.9 ms; audio off 49.3..51.4 ms; unmodified server from the
    base commit 49.1..50.8 ms. The byte-stream parser of the old path holds each frame until the
    next one (33 ms at 30 fps); the NAL path does not.
  - Stream Audio off: SDP identical to the base commit's server apart from the `o=` session id,
    same capture (452 packets).
  - No audio frames although rtspLive has audio on: `no audio frame within 2500 ms, audio track
    not announced`, video-only SDP, captures complete normally.
  - Two concurrent ffmpeg clients: both get both tracks. Record side: the audio chain starts on the
    first DESCRIBE, survives DESCRIBE -> SETUP and later sessions, stops 3 s after the last reader,
    0 frames dropped over ~2000 frames.
  - openRTSP (live555 client, the stack VLC uses) receives both tracks; its `-4` MP4 writer counts
    SPS/PPS as video samples, so that file is not usable for a timing measurement.

Not covered on the host: the MPP AI/AENC chain itself (two AI channels at once, the encoder's
real ADTS output, its real timestamps), the Allwinner encoder's NAL layout, WiFi throughput, the
live555 2022.12.01 binaries at run time, the UI page.

### Known limitations

- Multicast mode (`[live] multicast=1`): video only.
- Two AI channels at once (recording with audio + stream audio) is supported by the MPP AI device
  model (channel list per device) but was not run on hardware; `AW_MPI_AI_SetPubAttr()` is
  called again by whichever chain starts second.
- DESCRIBE waits for the first AAC frame (normally a few hundred ms; 2.5 s if none comes).
- "Stream Audio" has no translations yet (falls back to English).

## 4. Testing on a goggle

1. WiFi Module > System > Stream Audio: On, Apply Settings. `grep audio /mnt/app/app/record/confs/record.conf`
   shows `audio=1` under `[live]`; `/tmp/rtspLive.log` shows `live audio: on`.
2. `ffprobe -rtsp_transport tcp rtsp://192.168.2.122:8554/hdzero` (AP mode default address) lists
   `Video: h264` and `Audio: aac (LC), 48000 Hz, stereo`; `/tmp/rtspLive.log` shows
   `live audio: AAC object type 2, 48000 Hz, 2 channels, config=1190`.
3. Play: `ffplay -rtsp_transport tcp -fflags nobuffer -flags low_delay rtsp://192.168.2.122:8554/hdzero`
   or VLC "Open Network Stream" with the same URL (`vlc --network-caching=300 rtsp://192.168.2.122:8554/hdzero`).
   Check sound from the selected input (mic / line in), lip sync (clap in front of the camera with
   the goggle mic near it; the camera->goggle link latency adds to what the stream shows), latency
   unchanged, CPU (`top`).
4. Repeat with Record Audio off; while recording with audio (both the recording and the stream
   have sound; stop/start recording during streaming: the stream audio keeps going).
5. Stream Audio Off: SDP video-only as on the previous firmware.
6. Long run (30 min) for A/V drift: `ffmpeg -rtsp_transport udp -i rtsp://192.168.2.122:8554/hdzero -t 1800 -c copy long.mkv`.
7. Same on Goggle V1 (WiFi module) and BoxPro.
