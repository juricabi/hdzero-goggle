#!/usr/bin/env python3
"""Mutation check of test_mixer.py: every mutant of the mixer sources must make it fail.

    utilities/audio_mixer/mutants.py [--targets HDZGOGGLE2,...] [name filter]

Each mutant is one small change to a copy of src/core/audio_mixer*.c; test_mixer.py runs
against the copy (--src). The first three are the mutants of the review that the old dry run
let through. Exit status 1 if a mutant survives (or the unmodified copy fails).
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FILES = ["audio_mixer.c", "audio_mixer_routing.c", "audio_mixer.h"]
R = "audio_mixer_routing.c"
M = "audio_mixer.c"

MUTANTS = [
    ("review: ADC MIC1 and LINEIN switches swapped", R, [
        ('{"LADC input Mixer MIC1 boost Switch", adc_mic}', '{"LADC input Mixer MIC1 boost Switch", adc_av}'),
        ('{"LADC input Mixer LINEINL Switch", adc_av}', '{"LADC input Mixer LINEINL Switch", adc_mic}')]),
    ("review: AIF1 AD0L/R ADC switches off", R, [
        ('{"AIF1 AD0L Mixer ADCL Switch", 1}', '{"AIF1 AD0L Mixer ADCL Switch", 0}'),
        ('{"AIF1 AD0R Mixer ADCR Switch", 1}', '{"AIF1 AD0R Mixer ADCR Switch", 0}')]),
    ("review: Record What I Hear inverted", R, [
        ("const bool rec_mix = v[AUDIO_MIXER_REC_MIX] != 0;", "const bool rec_mix = v[AUDIO_MIXER_REC_MIX] == 0;")]),
    ("mix + direct: a heard mic recorded twice", R, [
        ("const int adc_mic = v[AUDIO_MIXER_REC_MIC] && !(rec_mix && mon_mic > 0);",
         "const int adc_mic = v[AUDIO_MIXER_REC_MIC];")]),
    ("mix + direct: direct a/v only without the mix (old Custom)", R, [
        ("const int adc_av = v[AUDIO_MIXER_REC_AV] && !(rec_mix && mon_av > 0);",
         "const int adc_av = !rec_mix && v[AUDIO_MIXER_REC_AV];")]),
    ("mix + direct: line in left out whenever the mix is on", R, [
        ("const int adc_line = v[AUDIO_MIXER_REC_LINE_IN] && !(rec_mix && mon_line > 0);",
         "const int adc_line = v[AUDIO_MIXER_REC_LINE_IN] && !rec_mix;")]),
    ("mix + direct: configured level instead of the level in effect (playback)", R, [
        ("const int adc_mic = v[AUDIO_MIXER_REC_MIC] && !(rec_mix && mon_mic > 0);",
         "const int adc_mic = v[AUDIO_MIXER_REC_MIC] && !(rec_mix && v[AUDIO_MIXER_MIC] > 0);")]),
    ("page marker: level 0 counted as in the mix", R, [
        ("settings->value[AUDIO_MIXER_REC_MIX] && settings->value[level] > 0;",
         "settings->value[AUDIO_MIXER_REC_MIX] && settings->value[level] + 1 > 0;")]),
    ("migration: Record What I Hear on", R, [("v[AUDIO_MIXER_REC_MIX] = 0;", "v[AUDIO_MIXER_REC_MIX] = 1;")]),
    ("load: a version 1 [mixer] read as it is", M, [
        ("if (version < AUDIO_MIXER_VERSION) {", "if (version < 1) {")]),
    ("boot: headphones live from boot when untouched", M, [
        ("context.line_out = mixer.value[AUDIO_MIXER_PHONES_AT_BOOT] != 0;", "context.line_out = true;")]),
    ("boot: a headphone level change does not make them live", M, [
        ("        mixer.value[AUDIO_MIXER_PHONES_AT_BOOT] = 1;\n        context.line_out = true;\n",
         "        mixer.value[AUDIO_MIXER_PHONES_AT_BOOT] = 1;\n")]),
    ("boot: any change makes the headphones live", M, [
        ("    return field == AUDIO_MIXER_HEADPHONES || field == AUDIO_MIXER_MIC",
         "    return field != AUDIO_MIXER_PHONES_AT_BOOT || field == AUDIO_MIXER_MIC")]),
    ("load: version 1 keys left behind", M, [
        ("            ini_puts(AUDIO_MIXER_SECTION, old_keys[i], NULL, SETTING_INI);", "            (void)old_keys[i];")]),
    ("switches in table order (off before on)", R, [
        ("        if (sw[i].on)\n            cset(&cb, sw[i].name, 1);", "        cset(&cb, sw[i].name, sw[i].on);"),
        ("        if (!sw[i].on)\n            cset(&cb, sw[i].name, 0);", "        if (0)\n            cset(&cb, sw[i].name, 0);")]),
    ("line out switched off last", R, [
        ('    if (!line_out)\n        cset(&cb, "Lineout Switch", 0);\n', ""),
        ('    if (line_out)\n        cset(&cb, "Lineout Switch", 1);', '    cset(&cb, "Lineout Switch", line_out);')]),
    ("line out not mono (LOMIX)", R, [('cset(&cb, "LINEOUTL Mux", 1);', 'cset(&cb, "LINEOUTL Mux", 0);')]),
    ("playback: right DAC not in the mix", R, [
        ('{"Right Output Mixer DACR Switch", dac_on}', '{"Right Output Mixer DACR Switch", 0}')]),
    ("playback: DAC at 160", R, [("#define DAC_PLAYBACK       152", "#define DAC_PLAYBACK       160")]),
    ("migration: receiver audio always on", R, [
        ("v[AUDIO_MIXER_RX_AUDIO] = old_value == 2;", "v[AUDIO_MIXER_RX_AUDIO] = old_value == 2 || v[AUDIO_MIXER_AV] > 0;")]),
    ("migration: Line In boost 3", R, [("#define MIC2_BOOST_OLD 4", "#define MIC2_BOOST_OLD 3")]),
    ("migration: headphones line out 27", R, [("v[AUDIO_MIXER_HEADPHONES] = 31;", "v[AUDIO_MIXER_HEADPHONES] = 27;")]),
    ("migration: negative old value read as Mic", R, [
        ("uint8_t dvr_source = (uint8_t)audio_source;", "uint8_t dvr_source = audio_source < 0 ? 0 : (uint8_t)audio_source;")]),
    ("live dac mutes the other sources", R, [("const int mon_mic = playback ? 0 : v[AUDIO_MIXER_MIC];",
                                              "const int mon_mic = (playback || context->live_dac) ? 0 : v[AUDIO_MIXER_MIC];")]),
    ("live dac: line out stays off with hdmi in", R, [
        ("const int line_out = (context->line_out || live_dac || playback) && volume > 0;",
         "const int line_out = (context->line_out || playback) && volume > 0;")]),
    ("live dac: digital loopback into the ADC", R, [
        ('{"Right Output Mixer DACL Switch", 0},', '{"Right Output Mixer DACL Switch", 0},\n        {"AIF1 AD0L Mixer AIF1 DA0L Switch", dac_on},')]),
    ("load: no clamping", M, [("mixer.value[i] = clamp_field(i, value);", "mixer.value[i] = (uint8_t)value;")]),
    ("load: missing fields from Mic, not the old setting", M, [
        ("audio_mixer_defaults_from_audio_source(&defaults, audio_source);",
         "audio_mixer_defaults_from_audio_source(&defaults, audio_source);\n    if (ini_getl(AUDIO_MIXER_SECTION, \"version\", 0, SETTING_INI))\n        audio_mixer_defaults_from_audio_source(&defaults, 0);")]),
    ("set writes setting.ini", M, [
        ("    mixer.value[field] = clamp_field(field, value);\n    if (is_headphone_level",
         "    mixer.value[field] = clamp_field(field, value);\n    ini_putl(AUDIO_MIXER_SECTION, field_key[field], mixer.value[field], SETTING_INI);\n    if (is_headphone_level")]),
    ("apply in the caller's thread", M, [("    if (worker_running)\n        pthread_cond_signal(&work_cond);",
                                          "    if (0)\n        pthread_cond_signal(&work_cond);")]),
    ("no watcher", M, [("const int has = codec_has_last_batch();", "const int has = codec_has_last_batch() >= 0;")]),
    ("watcher never gives up", M, [("#define AUDIO_MIXER_WATCH_RETRIES 3", "#define AUDIO_MIXER_WATCH_RETRIES 1000")]),
]


def run_test(src, targets):
    cmd = [sys.executable, "-I", os.path.join(HERE, "test_mixer.py"), "--src", src, "--targets", targets]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    return p.returncode, p.stdout


def main():
    targets = "HDZGOGGLE,HDZGOGGLE2,HDZBOXPRO"
    args = sys.argv[1:]
    if args and args[0] == "--targets":
        targets, args = args[1], args[2:]
    only = args[0] if args else None

    work = tempfile.mkdtemp(prefix="audio_mixer_mutants_")
    survived = 0
    try:
        clean = os.path.join(work, "clean")
        os.makedirs(os.path.join(clean, "core"))
        for f in FILES:
            shutil.copy(os.path.join(ROOT, "src/core", f), os.path.join(clean, "core", f))
        status, out = run_test(clean, targets)
        if status != 0:
            print("the unmodified sources fail:\n" + out)
            return 1
        print("unmodified: passes")

        for i, (name, file, edits) in enumerate(MUTANTS):
            if only and only not in name:
                continue
            src = os.path.join(work, f"m{i}")
            shutil.copytree(clean, src)
            path = os.path.join(src, "core", file)
            text = open(path).read()
            for old, new in edits:
                if text.count(old) != 1:
                    print(f"mutant '{name}': cannot apply, '{old[:60]}' found {text.count(old)} times")
                    return 1
                text = text.replace(old, new)
            open(path, "w").write(text)
            status, out = run_test(src, targets)
            first = next((line for line in out.splitlines() if line.startswith("FAIL")), "")
            if "build failed" in out:
                print(f"mutant '{name}' does not build: {first[:150]}")
                return 1
            if status == 0:
                survived += 1
                print(f"SURVIVED  {name}")
            else:
                print(f"killed    {name}: {first[:150]}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print(f"{len(MUTANTS) if not only else 'selected'} mutants, {survived} survived")
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
