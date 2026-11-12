#!/usr/bin/env python3
"""Host test of the audio mixer: the real src/core/audio_mixer.c and audio_mixer_routing.c
(worker, setting.ini through minIni, the batches written for amixer) against a model of the
codec's signal graph (codec_model.py, from the driver's DAPM routes).

    utilities/audio_mixer/test_mixer.py [--src DIR] [--driver sun8iw16-codec.c] [-v]

For every target (Goggle, Goggle 2, BoxPro):
  routing     a matrix of mixer states: which sources reach the recording (left, right) and
              the headphones, through which gains; every batch line a real control in range,
              never a digital DAC/ADC loopback; HDMI-in audio (the live DAC) included; Record
              What I Hear and the direct Record switches together, never a source twice
  loading     migration from each old Audio Source, clamping, fields missing from [mixer], a
              [mixer] of the unreleased version 1
  migration   old firmware (libmedia_mpp reset + audio_sel.sh) vs new, per old Audio Source:
              recording, headphones and the analog receiver's audio switch must be the same
              (the one documented difference: booting straight into HDZero, docs/audio.md)
  transitions a change never leaves the line out / the recording without a source in between,
              and a source recorded before and after is recorded all the way through
  worker      apply never blocks, a burst of steps coalesces, setting.ini only on save
  watcher     a libmedia_mpp reset after the routing is undone; a control the codec does not
              keep does not make it loop
--src runs the same against other mixer sources (mutants.py); --driver also checks that every
modelled route is one of the driver's.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import codec_model as cm  # noqa: E402

TARGETS = ["HDZGOGGLE", "HDZGOGGLE2", "HDZBOXPRO"]
KEYS = ["mic", "line_in", "av", "mic_boost", "line_in_boost", "headphones", "playback",
        "rec_mix", "rec_mic", "rec_line_in", "rec_av", "dvr_level", "rx_audio", "hdmi", "phones_at_boot"]
MAX = {"mic": 8, "line_in": 8, "av": 8, "mic_boost": 7, "line_in_boost": 7, "headphones": 31,
       "playback": 31, "rec_mix": 1, "rec_mic": 1, "rec_line_in": 1, "rec_av": 1, "dvr_level": 7,
       "rx_audio": 1, "hdmi": 8, "phones_at_boot": 1}
OLD_KEYS = ["dvr_mode", "dvr_mic", "dvr_line_in", "dvr_av"]  # [mixer] version 1 (never released)
VERSION = "2"
# the Record source switch of each analog source, and its headphone level
DIRECT = {"mic": "rec_mic", "line_in": "rec_line_in", "av": "rec_av"}
# DAC -> ADC and ADC -> DAC digital loopbacks: the mixer must never switch them on
LOOPBACKS = ["AIF1 AD0L Mixer AIF1 DA0L Switch", "AIF1 AD0R Mixer AIF1 DA0R Switch",
             "DACL Mixer ADCL Switch", "DACR Mixer ADCR Switch"]
DUMP = os.path.join(ROOT, "mkapp/app/app/record/audio/audiocodec/alsa-util/amixer-record.txt")
AUDIO_SEL = os.path.join(ROOT, "mkapp/app/script/audio_sel.sh")
WATCH_MS = 50  # the harness's watcher period (firmware: 2000)

CONTROLS = cm.parse_amixer_contents(DUMP)
SEL = cm.AudioSel(AUDIO_SEL)
failures = []
verbose = False


def fail(test, message):
    failures.append(f"{test}: {message}")
    print(f"FAIL {test}: {message}")


def ok(test):
    if verbose:
        print(f"ok   {test}")


# ---------------------------------------------------------------------------------------------
# harness

class Harness:
    def __init__(self, target, src, work):
        self.target = target
        self.dir = os.path.join(work, target)
        os.makedirs(self.dir, exist_ok=True)
        self.binary = os.path.join(self.dir, "harness")
        minini = os.path.join(work, "minIni.o")
        if not os.path.exists(minini):
            subprocess.run(["cc", "-std=gnu99", "-w", "-c", os.path.join(ROOT, "lib/minIni/src/minIni.c"),
                            "-I", os.path.join(ROOT, "lib/minIni/src"), "-o", minini], check=True)
        cmd = ["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-pthread", f"-D{target}=1",
               f"-DAUDIO_MIXER_WATCH_MS={WATCH_MS}",
               "-I", os.path.join(HERE, "harness/include"), "-I", src, "-I", os.path.join(ROOT, "src"),
               "-I", os.path.join(ROOT, "lib/log/include"), "-I", os.path.join(ROOT, "lib/minIni/src"),
               os.path.join(HERE, "harness/harness.c"),
               os.path.join(src, "core/audio_mixer.c"), os.path.join(src, "core/audio_mixer_routing.c"),
               minini, "-o", self.binary]
        subprocess.run(cmd, check=True)
        self.count = 0

    def run(self, ini, lines, timeout=30):
        """Run a script; returns (batches, queries, codec, log, ini text after)."""
        self.count += 1
        case = os.path.join(self.dir, f"case{self.count}")
        os.makedirs(case, exist_ok=True)
        ini_path = os.path.join(case, "setting.ini")
        with open(ini_path, "w") as f:
            f.write(ini)
        script = "\n".join([f"ini {ini_path}"] + lines) + "\n"
        p = subprocess.run([self.binary], input=script, cwd=case, capture_output=True, text=True, timeout=timeout)
        if p.returncode != 0:
            raise RuntimeError(f"harness failed ({p.returncode}): {p.stderr[-2000:]}")
        batches, queries, codec = [], [], {}
        self.dumps, self.marks = [], []  # every dump; number of batches at each "mark"
        it = iter(p.stdout.splitlines())
        for line in it:
            if line.startswith("BATCH "):
                text = []
                for b in it:
                    if b == "END":
                        break
                    text.append(b)
                batches.append(cm.parse_batch("\n".join(text)))
            elif line.startswith("CODEC "):
                if not codec:
                    self.dumps.append(codec)
                name, value = line[6:].rsplit("=", 1)
                codec[name] = int(value)
            elif line == "MARK":
                self.marks.append(len(batches))
                codec = {}
            else:
                if codec:
                    codec = {}
                queries.append(line)
        codec = self.dumps[-1] if self.dumps else {}
        return batches, queries, codec, p.stderr, open(ini_path).read()


def base_codec(target, libmedia=True):
    state = cm.driver_defaults(CONTROLS, target)
    if libmedia:  # the record daemon started by rc.sh has initialised
        state = cm.apply(state, cm.LIBMEDIA_RESET)
    return state


def codec_lines(state):
    return [f"codec {k}={v}" for k, v in sorted(state.items())]


def mixer_ini(values):
    return "[mixer]\n" + "".join(f"{k}={v}\n" for k, v in values.items()) + f"version={VERSION}\n"


def query(queries, prefix):
    return [q for q in queries if q.startswith(prefix)]


# ---------------------------------------------------------------------------------------------
# the specification (docs/audio.md), independent of audio_mixer_routing.c

def clamp_values(values):
    v = {k: 0 for k in KEYS}
    for k, x in values.items():
        v[k] = max(0, min(MAX[k], int(x)))
    return v


def expected(v, line_out=True, playback=False, live_dac=False):
    """What each sink must get: {sink: {source: {(gains, output mixer side)}}}."""
    mix = {}  # sources in the output mixers: gains up to and including the output mixer
    live_dac = live_dac and not playback
    if live_dac and v["hdmi"]:
        # live digital source (HDMI-in audio): level 1..8 = -4.5..+6 dB around playback's 152
        mix["dac"] = {("DAC volume", 152 + 2 * (v["hdmi"] - 4))}
    if not playback:
        if v["mic"]:
            mix["mic"] = {("MIC1 boost AMP gain control", v["mic_boost"]), ("MIC1_G boost stage output mixer control", v["mic"] - 1)}
        if v["line_in"]:
            mix["line_in"] = {("MIC2 boost AMP gain control", v["line_in_boost"]), ("MIC2 BST stage to L_R outp mixer gain", v["line_in"] - 1)}
        if v["av"]:
            mix["av"] = {("LINEINL/R to L_R output mixer gain", v["av"] - 1)}
    else:
        mix["dac"] = {("DAC volume", 152)}  # 6 dB less: the line out adds left and right

    volume = v["playback"] if playback else v["headphones"]
    phones = {}
    if (line_out or playback or live_dac) and volume > 0:
        # mono line out: both outputs carry left + right output mixer
        for source, gains in mix.items():
            g = frozenset(gains | {("lineout volume", volume)})
            phones[source] = {(g, "L"), (g, "R")}

    adc = {("ADC input gain control", v["dvr_level"]), ("ADC volume", 160)}
    record_l, record_r = {}, {}
    if v["rec_mix"]:  # Record What I Hear: left ADC <- left output mixer, right ADC <- right
        for source, gains in mix.items():
            record_l[source] = {(frozenset(gains | adc), "L")}
            record_r[source] = {(frozenset(gains | adc), "R")}
    # Record Mic / Line In / A/V In: straight into both ADCs, only the preamp in front; a source
    # that is in the recorded mix (the mix as it is: playback takes the live sources out) is
    # recorded once, at its mix level
    direct = {"mic": {("MIC1 boost AMP gain control", v["mic_boost"])},
              "line_in": {("MIC2 boost AMP gain control", v["line_in_boost"])},
              "av": set()}
    for source, key in DIRECT.items():
        if v[key] and not (v["rec_mix"] and source in mix):
            record_l[source] = {(frozenset(direct[source] | adc), None)}
            record_r[source] = {(frozenset(direct[source] | adc), None)}
    return {"record_l": record_l, "record_r": record_r, "phones_l": phones, "phones_r": dict(phones)}


def migration_spec(target, audio_source):
    """[mixer] after migration from the old [record] audio_source."""
    sel = min(audio_source & 0xFF, 2)
    dt = cm.DEVICE_TREE[target]
    return {"mic": 0, "line_in": 0, "av": 8, "mic_boost": dt["MIC1 boost AMP gain control"],
            "line_in_boost": dt["MIC2 boost AMP gain control"], "headphones": 31, "playback": 27,
            "rec_mix": 0, "rec_mic": int(sel == 0), "rec_line_in": int(sel == 1), "rec_av": int(sel == 2),
            "dvr_level": 3, "rx_audio": int(audio_source == 2), "hdmi": 4, "phones_at_boot": 0}


def compare(test, got, want):
    if got == want:
        ok(test)
        return True
    fail(test, "\n got:\n" + cm.describe(got) + "\n want:\n" + cm.describe(want))
    return False


def check_batches(test, batches):
    for i, batch in enumerate(batches):
        errors = cm.check_batch(CONTROLS, batch)
        errors += [f"digital loopback '{n}' switched on" for n, value in batch if n in LOOPBACKS and value]
        if errors:
            fail(f"{test} batch {i + 1}", "; ".join(errors))
            return False
    return True


# ---------------------------------------------------------------------------------------------
# tests

def mix_only(**values):  # Record What I Hear, no direct source
    return dict({"rec_mix": 1, "rec_mic": 0, "rec_line_in": 0, "rec_av": 0}, **values)


def direct_only(**values):  # the stock path: direct sources only
    return dict({"rec_mix": 0, "rec_mic": 0, "rec_line_in": 0, "rec_av": 0}, **values)


ROUTING_STATES = [
    ("mix: mic + av", mix_only(mic=4, av=3, mic_boost=2, headphones=25, dvr_level=3), {}),
    ("mix: line in + av", mix_only(line_in=6, line_in_boost=2, av=4, headphones=31, dvr_level=5), {}),
    ("mix, headphones off still records", mix_only(mic=8, headphones=0, dvr_level=2, mic_boost=4), {}),
    ("direct mic, hear a/v (stock)", direct_only(rec_mic=1, av=8, mic_boost=5, headphones=31, dvr_level=3), {}),
    ("direct line in + a/v, hear mic", direct_only(rec_line_in=1, rec_av=1, mic=2, line_in_boost=6, headphones=12, dvr_level=0), {}),
    ("nothing recorded", direct_only(av=8, headphones=31, dvr_level=3), {}),
    ("all max, mix", mix_only(mic=8, line_in=8, av=8, mic_boost=7, line_in_boost=7, headphones=31, dvr_level=7), {}),
    ("all off", direct_only(headphones=0), {}),
    # Record What I Hear together with direct sources
    ("mix + direct mic at level 0: mic direct, a/v from the mix", mix_only(rec_mic=1, mic=0, av=8, headphones=31), {}),
    ("mix + direct mic at level > 0: once, at its mix level", mix_only(rec_mic=1, mic=5, av=8, mic_boost=3, headphones=31), {}),
    ("mix + all direct, some heard", mix_only(rec_mic=1, rec_line_in=1, rec_av=1, mic=0, line_in=2, av=7, dvr_level=4), {}),
    ("mix + direct line in at level 0", mix_only(rec_line_in=1, line_in=0, line_in_boost=5, mic=3), {}),
    ("mix of nothing + direct a/v", mix_only(rec_av=1, mic=0, line_in=0, av=0, headphones=31), {}),
    ("hdmi in (line out off), mix", mix_only(mic=4, av=8, headphones=31, dvr_level=3), {"line_out": 0}),
    ("hdmi in (line out off), direct line in", direct_only(rec_line_in=1, av=8, headphones=31), {"line_out": 0}),
    ("playback, mix", mix_only(mic=4, av=8, headphones=31, playback=20, dvr_level=3), {"playback": 1}),
    ("playback, mix + direct mic: mic direct while the file plays", mix_only(rec_mic=1, mic=4, av=8, playback=27), {"playback": 1}),
    ("playback after hdmi in", direct_only(rec_av=1, av=8, headphones=31, playback=27, dvr_level=3), {"line_out": 0, "playback": 1}),
    ("playback volume 0", direct_only(rec_mic=1, av=8, headphones=31, playback=0, dvr_level=3), {"playback": 1}),
    ("out of range in setting.ini", {"mic": 99, "line_in": -3, "av": 12, "mic_boost": 8, "line_in_boost": 100,
                                     "headphones": 200, "playback": 40, "rec_mix": 5, "rec_mic": 7, "rec_line_in": 2,
                                     "rec_av": -1, "dvr_level": 9, "rx_audio": 3, "hdmi": 11}, {}),
    # HDMI In Audio on: the bridge plays HDMI-in audio into the DAC (live_dac); mixed with the mic,
    # line out on despite HDMI in, recorded only with Record What I Hear
    ("hdmi in audio, mix, with mic", mix_only(mic=4, av=0, hdmi=6, headphones=25, dvr_level=3),
     {"line_out": 0, "live_dac": 1}),
    ("hdmi in audio, direct mic: hdmi not recorded", direct_only(rec_mic=1, mic=4, hdmi=1, headphones=31),
     {"line_out": 0, "live_dac": 1}),
    ("hdmi in audio + direct mic at level > 0, mix", mix_only(rec_mic=1, mic=3, hdmi=4, headphones=31),
     {"line_out": 0, "live_dac": 1}),
    ("hdmi in audio at level 0, hdmi in", mix_only(mic=4, hdmi=0, headphones=31), {"line_out": 0, "live_dac": 1}),
    ("hdmi in audio during playback", mix_only(mic=4, hdmi=8, playback=27), {"live_dac": 1, "playback": 1}),
    ("hdmi in audio off (stock): line out off", mix_only(mic=4, hdmi=8, av=8, headphones=31), {"line_out": 0, "live_dac": 0}),
]


def test_routing(h):
    for name, values, ctx in ROUTING_STATES:
        test = f"{h.target} routing '{name}'"
        lines = codec_lines(base_codec(h.target)) + ["init", "apply"]
        # (after a switch to an analog source unless the state says otherwise)
        lines.append(f"line_out {ctx.get('line_out', 1)}")
        if "live_dac" in ctx:
            lines.append(f"live_dac {ctx['live_dac']}")
        if "playback" in ctx:
            lines.append(f"playback {ctx['playback']}")
        lines += ["wait", "dump"]
        batches, _, codec, _, _ = h.run(mixer_ini(values), lines)
        if not batches:
            fail(test, "no batch written")
            continue
        check_batches(test, batches)
        # fields not in [mixer] come from the old setting (absent: Mic)
        full = dict(migration_spec(h.target, 0), **values)
        playback, live_dac = ctx.get("playback", 0) == 1, ctx.get("live_dac", 0) == 1
        want = expected(clamp_values(full), line_out=ctx.get("line_out", 1) == 1, playback=playback, live_dac=live_dac)
        compare(test, cm.evaluate(codec, dac_active=playback or live_dac), want)


def test_loading(h):
    # clamping on load
    values = dict(next(v for name, v, _ in ROUTING_STATES if name == "out of range in setting.ini"))
    _, queries, _, _, _ = h.run(mixer_ini(values), ["init"] + [f"get {k}" for k in KEYS] + ["rx"])
    got = {q.split()[1]: int(q.split()[2]) for q in query(queries, "GET ")}
    want = clamp_values(values)
    if got != want:
        fail(f"{h.target} load clamps", f"got {got}, want {want}")
    else:
        ok(f"{h.target} load clamps")

    # a field missing from [mixer] comes from the old setting
    _, queries, _, _, _ = h.run(f"[record]\naudio_source=2\n[mixer]\nversion={VERSION}\nmic=5\n", ["init"] + [f"get {k}" for k in KEYS])
    got = {q.split()[1]: int(q.split()[2]) for q in query(queries, "GET ")}
    want = dict(migration_spec(h.target, 2), mic=5)
    if got != want:
        fail(f"{h.target} load missing fields", f"got {got}, want {want}")
    else:
        ok(f"{h.target} load missing fields")

    # a [mixer] of the unreleased version 1 (As Heard / Custom): taken over like a stock
    # setting.ini, the old keys removed
    for audio_source in (0, 1, 2):
        test = f"{h.target} load version 1 [mixer], audio_source={audio_source}"
        old = ("[record]\naudio_source=%d\n[mixer]\nmic=4\nline_in=3\nav=2\nheadphones=12\ndvr_mode=0\ndvr_mic=1\n"
               "dvr_line_in=1\ndvr_av=1\ndvr_level=6\nhdmi=7\nversion=1\n" % audio_source)
        _, queries, _, _, after = h.run(old, ["init"] + [f"get {k}" for k in KEYS])
        got = {q.split()[1]: int(q.split()[2]) for q in query(queries, "GET ")}
        want = migration_spec(h.target, audio_source)
        section = after.split("[mixer]", 1)[-1]
        written = dict(re.findall(r"^(\w+)=(-?\d+)$", section, re.M))
        if got != want:
            fail(test, f"got {got}, want {want}")
        elif any(k in written for k in OLD_KEYS) or written.get("version") != VERSION:
            fail(test, f"[mixer] written as {written}")
        else:
            ok(test)

    # the page's "in the mix" marker: Record What I Hear on and the source's level above 0
    for values, want in ((mix_only(mic=3, line_in=0, av=8), {"rec_mic": 1, "rec_line_in": 0, "rec_av": 1}),
                         (direct_only(rec_mic=1, mic=3, av=8), {"rec_mic": 0, "rec_line_in": 0, "rec_av": 0})):
        _, queries, _, _, _ = h.run(mixer_ini(values), ["init"] + [f"inmix {k}" for k in DIRECT.values()])
        got = {q.split()[1]: int(q.split()[2]) for q in query(queries, "INMIX ")}
        if got != want:
            fail(f"{h.target} in recorded mix", f"{values}: got {got}, want {want}")
        else:
            ok(f"{h.target} in recorded mix")

    # migration from each old Audio Source (and what the old firmware made of odd values)
    for audio_source in (0, 1, 2, 3, -1, 258):
        test = f"{h.target} migrate audio_source={audio_source}"
        ini = f"[record]\naudio_source={audio_source}\n" if audio_source != 0 else "[record]\n"
        _, queries, _, _, after = h.run(ini, ["init"] + [f"get {k}" for k in KEYS] + ["rx", "unsaved"])
        got = {q.split()[1]: int(q.split()[2]) for q in query(queries, "GET ")}
        want = migration_spec(h.target, audio_source)
        if got != want:
            fail(test, f"got {got}, want {want}")
            continue
        if query(queries, "RX ") != [f"RX {want['rx_audio']}"]:
            fail(test, f"receiver audio {query(queries, 'RX ')}")
            continue
        written = dict(re.findall(r"^(\w+)=(-?\d+)$", after.split("[mixer]", 1)[-1], re.M))
        if {k: int(written.get(k, -99)) for k in KEYS} != want or written.get("version") != VERSION:
            fail(test, f"[mixer] written as {written}")
            continue
        ok(test)


def old_firmware(target, audio_source, flow):
    """Codec state and receiver audio of the old firmware (record daemon up, then the app)."""
    state = base_codec(target)
    sel = cm.old_source_command(audio_source)
    if flow in ("switch", "hdmi"):
        # page_source / app_switch_to_analog: dvr_select_audio_source + dvr_enable_line_out(true)
        state = cm.apply(state, SEL(sel) + SEL("out_on") + SEL("out_linein_on") + SEL("out_dac_off"))
    if flow == "hdmi":
        state = cm.apply(state, SEL("out_off"))  # app_switch_to_hdmi_in: dvr_enable_line_out(false)
    state = cm.apply(state, SEL(sel))  # recording start: dvr_update_record_conf
    rx = int(audio_source == 2)  # rtc6715.init(1, audio_source == SETTING_RECORD_AUDIO_SOURCE_AV_IN)
    return state, rx


def new_firmware(h, audio_source, flow):
    ini = f"[record]\naudio_source={audio_source}\n"
    lines = ["init", "apply"]  # main.c
    if flow == "late_reset":
        lines = codec_lines(base_codec(h.target, libmedia=False)) + lines + ["wait", "wait", "line_out 1", "wait"]
        # the record daemon initialises after the app has routed (boot race, SD check, format)
        lines += codec_lines(dict(cm.LIBMEDIA_RESET)) + [f"sleep {WATCH_MS * 8}"]
    else:
        lines = codec_lines(base_codec(h.target)) + lines
        if flow in ("switch", "hdmi"):
            lines += ["wait", "line_out 1"]  # dvr_select_audio_source (waits), dvr_enable_line_out(true)
        if flow == "hdmi":
            lines += ["line_out 0"]
        lines += ["wait"]  # recording start
    lines += ["wait", "rx", "dump"]
    batches, queries, codec, log, _ = h.run(ini, lines)
    rx = int(query(queries, "RX ")[0].split()[1])
    return codec, rx, batches, log


def test_migration(h):
    names = {0: "Mic", 1: "Line In", 2: "A/V In", 3: "3 (= A/V In)", -1: "-1 (= A/V In)"}
    for audio_source in (0, 1, 2, 3, -1):
        for flow in ("switch", "hdmi", "boot", "late_reset"):
            test = f"{h.target} old Audio Source {names[audio_source]}, {flow}"
            new, new_rx, batches, log = new_firmware(h, audio_source, flow)
            check_batches(test, batches)
            old, old_rx = old_firmware(h.target, audio_source, "switch" if flow == "late_reset" else flow)
            got, want = cm.evaluate(new), cm.evaluate(old)
            if new_rx != old_rx:
                fail(test, f"analog receiver audio {new_rx}, old firmware {old_rx}")
                continue
            if cm.line_out_powered(new) != cm.line_out_powered(old):
                fail(test, f"line out powered {cm.line_out_powered(new)}, old firmware {cm.line_out_powered(old)}")
                continue
            if flow == "late_reset" and "codec changed by someone else" not in log:
                fail(test, "the reset was not noticed")
                continue
            # booting straight into HDZero included: the old firmware left the record daemon's
            # silent line out (27, idle left DAC only) until the first switch to an analog source
            compare(test, got, want)


def test_phones_at_boot(h):
    """The headphones at boot: stock (silent until the first analog source) until a headphone level is
    changed; from then on, from boot. Other changes (recording, boosts) keep the stock behaviour."""
    values = dict(migration_spec(h.target, 2))
    for name, ini_values, change, want_live, want_flag in (
            ("untouched", values, [], False, 0),
            ("a recording switch changed", values, ["set rec_mix 1", "apply"], False, 0),
            ("Mic changed", values, ["set mic 3", "apply"], True, 1),
            ("Volume changed", values, ["set headphones 20", "apply"], True, 1),
            ("Volume set to its value", values, ["set headphones 31", "apply"], False, 0),
            ("phones_at_boot=1 in setting.ini", dict(values, phones_at_boot=1), [], True, 1)):
        test = f"{h.target} headphones at boot, {name}"
        lines = codec_lines(base_codec(h.target)) + ["init", "apply", "wait"] + change + ["wait", "get phones_at_boot", "dump"]
        _, queries, codec, _, _ = h.run(mixer_ini(ini_values), lines)
        live = cm.line_out_powered(codec)
        flag = int(query(queries, "GET phones_at_boot")[0].split()[2])
        if live != want_live or flag != want_flag:
            fail(test, f"line out powered {live}, phones_at_boot {flag}; want {want_live}, {want_flag}")
        else:
            ok(test)


TRANSITIONS = [
    # name, start values, start context, change (set lines / context), dac active during the batch
    ("switch the only source (mic -> a/v)", mix_only(mic=4, headphones=31, dvr_level=3), [],
     ["set mic 0", "set av 8", "apply"], False),
    ("a/v -> mic, direct recording mic -> a/v", direct_only(rec_mic=1, av=8, headphones=31), [],
     ["set av 0", "set mic 3", "set rec_mic 0", "set rec_av 1", "apply"], False),
    ("mix -> direct mic while recording", mix_only(mic=4, headphones=31), [],
     ["set rec_mix 0", "set rec_mic 1", "apply"], False),
    ("direct a/v -> mix (a/v heard) while recording", direct_only(rec_av=1, av=8, headphones=31), [],
     ["set rec_mix 1", "apply"], False),
    ("direct mic -> mix: mic level 0 -> 4 with Record Mic and the mix", mix_only(rec_mic=1, mic=0, av=8, headphones=31), [],
     ["set mic 4", "apply"], False),
    ("mix -> direct mic: mic level 4 -> 0 with Record Mic and the mix", mix_only(rec_mic=1, mic=4, av=8, headphones=31), [],
     ["set mic 0", "apply"], False),
    ("Record What I Hear off with direct mic and mic heard", mix_only(rec_mic=1, mic=4, av=8, headphones=31), [],
     ["set rec_mix 0", "apply"], False),
    ("playback starts", direct_only(rec_av=1, av=8, headphones=31, playback=27), [],
     ["playback 1"], True),
    ("playback ends", direct_only(rec_av=1, av=8, headphones=31, playback=27), ["playback 1"],
     ["playback 0"], True),
    ("playback starts, mix + direct mic", mix_only(rec_mic=1, mic=4, av=8, headphones=31, playback=27), [],
     ["playback 1"], True),
    ("hdmi in: line out off", mix_only(mic=4, av=8, headphones=31), [],
     ["line_out 0"], False),
    ("headphones 0", mix_only(av=8, headphones=20), [],
     ["set headphones 0", "apply"], False),
    ("hdmi-in audio starts (mic heard)", mix_only(mic=4, hdmi=4, headphones=31), ["line_out 0"],
     ["live_dac 1"], True),
    ("hdmi-in audio: the only source changes (dac -> mic)", mix_only(hdmi=4, headphones=31),
     ["line_out 0", "live_dac 1"], ["set hdmi 0", "set mic 4", "apply"], True),
    ("hdmi-in audio ends (hdmi in still shown)", mix_only(mic=4, hdmi=4, headphones=31),
     ["line_out 0", "live_dac 1"], ["live_dac 0"], True),
    ("playback while hdmi-in audio plays", mix_only(mic=4, hdmi=4, headphones=31, playback=27),
     ["line_out 0", "live_dac 1"], ["playback 1"], True),
]


def recorded_sources(state, dac):
    result = cm.evaluate(state, dac_active=dac)
    return set(result["record_l"]) | set(result["record_r"])


def test_transitions(h):
    for name, values, start, change, dac in TRANSITIONS:
        test = f"{h.target} transition '{name}'"
        # everything before the mark is applied (after a switch to an analog source); the change
        # must be one batch on its own
        lines = codec_lines(base_codec(h.target)) + ["init", "wait", "line_out 1"] + start + ["wait", "dump", "mark"] + change + ["sleep 300", "dump"]
        batches, _, _, _, _ = h.run(mixer_ini(values), lines)
        before, after = h.dumps[0], h.dumps[-1]
        new = batches[h.marks[0]:]
        if len(new) != 1:
            fail(test, f"{len(new)} batches for the change")
            continue
        batch = new[0]
        state = dict(before)
        powered = [cm.line_out_powered(state, dac)]
        capture = [cm.capture_connected(state, dac)]
        recorded = [recorded_sources(state, dac)]
        for name_value in batch:
            state = cm.apply(state, [name_value])
            powered.append(cm.line_out_powered(state, dac))
            capture.append(cm.capture_connected(state, dac))
            recorded.append(recorded_sources(state, dac))
        if state != after:
            fail(test, "replaying the batch does not give the codec state")
            continue
        problems = []
        # a source that is recorded before and after the change is recorded all the way through
        # (e.g. the mic moving between its direct path and the mix)
        kept = recorded[0] & recorded[-1]
        gap = next((i for i, r in enumerate(recorded) if not kept <= r), None)
        if gap is not None:
            problems.append(f"{sorted(kept - recorded[gap])} not recorded in between (after '{batch[gap - 1][0]}')")
        if powered[0] and powered[-1] and not all(powered):
            problems.append(f"line out unpowered in between (after '{batch[powered.index(False) - 1][0]}')")
        if capture[0] and capture[-1] and not all(capture):
            problems.append(f"recording without a source in between (after '{batch[capture.index(False) - 1][0]}')")
        if powered[0] and not powered[-1]:
            first_change = next((n for n, v in batch if before.get(n) != v), None)
            if first_change != "Lineout Switch":
                problems.append(f"line out goes off, but '{first_change}' changes before it")
        if problems:
            fail(test, "; ".join(problems))
        else:
            ok(test)


def test_worker(h):
    test = f"{h.target} worker"
    values = mix_only(av=8, headphones=31)
    ini = mixer_ini(values)
    lines = codec_lines(base_codec(h.target)) + ["init", "wait", "mark", "amixer_delay 150",
                                                 "time_apply 20", "unsaved", "wait", "dump"]
    batches, queries, codec, _, after = h.run(ini, lines)
    slowest = int(query(queries, "APPLY_US ")[0].split()[1])
    burst = len(batches) - h.marks[0]  # the 20 steps and the final wait
    problems = []
    if slowest > 30000:
        problems.append(f"an apply call took {slowest / 1000:.0f} ms (amixer takes 150 ms)")
    if not 1 <= burst <= 4:
        problems.append(f"20 dial steps gave {burst} batches")
    if codec.get("lineout volume") != 30:
        problems.append(f"the codec ended at line out {codec.get('lineout volume')}, not the last step (30)")
    if after != ini:
        problems.append("setting.ini written before audio_mixer_save()")
    if query(queries, "UNSAVED ") != ["UNSAVED 1"]:
        problems.append("changes not reported as unsaved")
    _, queries, _, _, after = h.run(ini, ["init", "set headphones 17", "set av 5", "save", "unsaved"])
    if "headphones=17" not in after or "av=5" not in after or query(queries, "UNSAVED ") != ["UNSAVED 0"]:
        problems.append("audio_mixer_save() did not write the changes")
    if problems:
        fail(test, "; ".join(problems))
    else:
        ok(test)


def test_watcher(h):
    test = f"{h.target} watcher"
    values = direct_only(rec_line_in=1, av=8, headphones=31)
    lines = codec_lines(base_codec(h.target, libmedia=False)) + ["init", "wait", "dump", "mark"]
    lines += codec_lines(dict(cm.LIBMEDIA_RESET)) + [f"sleep {WATCH_MS * 8}", "dump"]
    batches, _, _, log, _ = h.run(mixer_ini(values), lines)
    before, after = h.dumps[0], h.dumps[-1]
    reapplied = len(batches) - h.marks[0]
    if after != before or reapplied != 1 or "codec changed by someone else" not in log:
        fail(test, f"reset not undone: {reapplied} batches, "
                   f"differences {sorted(k for k in after if after[k] != before.get(k))}")
    else:
        ok(test)

    test = f"{h.target} watcher, control the codec does not keep"
    lines = codec_lines(base_codec(h.target, libmedia=False)) + ["stuck lineout volume=27", "init", "wait",
                                                                   f"sleep {WATCH_MS * 16}", "mark", "set av 4", "wait"]
    batches, _, _, log, _ = h.run(mixer_ini(values), lines)
    # 1 batch, 3 retries, then quiet until the state changes (1 more)
    if h.marks[0] != 4 or len(batches) != 5 or "does not keep the routing" not in log:
        fail(test, f"{h.marks[0]} batches before the change, {len(batches)} in all")
    else:
        ok(test)


def test_driver_routes(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    table = src[src.index("ac_dapm_routes[]"):]
    table = table[:table.index("};")]
    driver = set()
    for m in re.finditer(r'\{\s*"([^"]+)",\s*(NULL|"[^"]*"),\s*"([^"]+)"\s*\}', table):
        driver.add((m.group(1), None if m.group(2) == "NULL" else m.group(2).strip('"'), m.group(3)))
    missing = [r for r in cm.ROUTES if r not in driver]
    if missing:
        fail("model vs driver routes", f"not in the driver: {missing}")
    else:
        ok(f"model: all {len(cm.ROUTES)} routes are the driver's")


def test_model_facts():
    """What the driver's routes allow for per-side record levels (docs/audio.md)."""
    state = {"AIF1 AD0L Mixer ADCL Switch": 1, "AIF1 AD0R Mixer ADCR Switch": 1,
             "LADC input Mixer MIC1 boost Switch": 1, "RADC input Mixer LINEINR Switch": 1}
    split = cm.evaluate(state)
    if set(split["record_l"]) != {"mic"} or set(split["record_r"]) != {"av"}:
        fail("model: split stereo", cm.describe(split))
    for item in (2, 3):  # SUM / AVE: no route, the mux (and the slot channel) is disconnected
        result = cm.evaluate(dict(state, **{"AIF1OUT0L Mux": item, "AIF1OUT0R Mux": item}))
        if result["record_l"] or result["record_r"]:
            fail("model: SUM/AVE mux", f"item {item} still connects: {cm.describe(result)}")
    if any(sink == "AIF1 AD0L Mixer" and source == "RADC input Mixer" for sink, _, source in cm.ROUTES):
        fail("model: cross-mix", "left AIF1 slot can take the right ADC")
    ok("model: split stereo possible, codec-side SUM/AVE and cross-mix not")


def main():
    global verbose
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", default=os.path.join(ROOT, "src"), help="directory with core/audio_mixer*.c")
    parser.add_argument("--driver", help="sun8iw16-codec.c of the BSP: check the modelled routes")
    parser.add_argument("--targets", default=",".join(TARGETS))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    verbose = args.verbose

    if args.driver:
        test_driver_routes(args.driver)
    test_model_facts()

    work = tempfile.mkdtemp(prefix="audio_mixer_test_")
    try:
        for target in args.targets.split(","):
            try:
                h = Harness(target, os.path.abspath(args.src), work)
            except subprocess.CalledProcessError as e:
                fail(target, f"build failed: {e}")
                continue
            for test in (test_routing, test_loading, test_migration, test_phones_at_boot, test_transitions,
                         test_worker, test_watcher):
                try:
                    test(h)
                except Exception as e:  # a crashing mutant is a failure too
                    fail(f"{target} {test.__name__}", repr(e))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print(f"{len(failures)} FAILED")
        return 1
    print("audio mixer test: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
