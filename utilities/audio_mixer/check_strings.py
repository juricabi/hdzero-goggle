#!/usr/bin/env python3
"""Checks every string of the Audio page in every language with the firmware's own fonts:

  - translated in every mkapp/app/language/*.ini,
  - every character present in the font (the CJK glyphs of the fonts are only those of the
    translations the fonts were generated from, utilities/font/generate_font_lib.py),
  - as wide as its column at most (conf/ui.h UI_AUDIO_*), on the goggles and on the BoxPro.

    utilities/audio_mixer/check_strings.py [-v]

Needs a host C compiler; builds LVGL for the host once (about a minute) in a temporary directory.
"""

import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PAGE = os.path.join(ROOT, "src/ui/page_audio.c")
LANG_DIR = os.path.join(ROOT, "mkapp/app/language")


def page_strings():
    """(kind, English text) of every string the page shows, from page_audio.c."""
    src = open(PAGE, encoding="utf-8").read()
    out = []
    for m in re.finditer(r'^\s*HEADING\("([^"]+)"\)', src, re.M):
        out.append(("heading", m.group(1)))
    for m in re.finditer(r'^\s*(?:SLIDER|YES_NO)\([^,"]+,\s*"([^"]+)",\s*"([^"]+)"\)', src, re.M):
        out += [("label", m.group(1)), ("hint", m.group(2))]
    for m in re.finditer(r'^\s*ON_OFF\([^,"]+,\s*[^,"]+,\s*"([^"]+)",\s*"([^"]+)"\)', src, re.M):
        out += [("label", m.group(1)), ("hint", m.group(2))]
    for m in re.finditer(r'^\s*RECORD_SOURCE\([^,"]+,\s*"([^"]+)"\)', src, re.M):
        out.append(("label", m.group(1)))
    for m in re.finditer(r'^#define HINT_\w+\s+"([^"]+)"', src, re.M):
        out.append(("hint", m.group(1)))
    if '.name = "Back"' in src:
        out.append(("label", "< Back"))
    out += [("button", b) for b in ("Yes", "No", "On", "Off")]
    out += [("value", "Off"), ("value", "Mix")] + [("value", v) for v in NUMBERS]
    return sorted(set(out))


NUMBERS = ["+6.0 dB", "-4.5 dB", "31"]  # widest values, shown as they are in every language


def translations(path):
    table = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r'^([^;#\[][^=]*?)\s*=\s*"(.*)"\s*$', line)
        if m:
            table[m.group(1)] = m.group(2)
    return table


def translate(text, table):
    if text.startswith("< "):  # "< %s" of the Back row
        inner = table.get(text[2:])
        return None if inner is None else "< " + inner
    return table.get(text)


def build(work):
    objs = os.path.join(work, "lvgl")
    os.makedirs(objs)
    sources = glob.glob(os.path.join(ROOT, "lib/lvgl/lvgl/src/**/*.c"), recursive=True)
    cflags = ["-std=gnu99", "-w", "-O1", "-DHDZGOGGLE2=1", "-I", os.path.join(ROOT, "lib/lvgl"),
              "-I", os.path.join(ROOT, "lib/lvgl/lvgl")]
    procs, outs = [], []
    for i, s in enumerate(sources):
        o = os.path.join(objs, f"{i}.o")
        outs.append(o)
        procs.append(subprocess.Popen(["cc", *cflags, "-c", s, "-o", o]))
        if len(procs) >= os.cpu_count() * 2:
            if procs.pop(0).wait():
                raise RuntimeError("LVGL build failed")
    for p in procs:
        if p.wait():
            raise RuntimeError("LVGL build failed")
    lib = os.path.join(work, "liblvgl.a")
    subprocess.run(["ar", "rcs", lib, *outs], check=True)
    tools = {}
    for target in ("HDZGOGGLE2", "HDZBOXPRO"):
        exe = os.path.join(work, f"check_{target}")
        p = subprocess.run(["cc", "-std=gnu99", "-Wall", "-Werror", f"-D{target}=1", "-I", os.path.join(ROOT, "lib/lvgl"),
                            "-I", os.path.join(ROOT, "src"), os.path.join(HERE, "check_strings.c"), lib,
                            "-o", exe, "-lm"], capture_output=True, text=True)
        if p.returncode:
            raise RuntimeError(f"check_strings.c ({target}) does not build:\n{p.stderr[-3000:]}")
        tools[target] = exe
    return tools


def main():
    verbose = "-v" in sys.argv
    strings = page_strings()
    langs = {"en": {}}
    for path in sorted(glob.glob(os.path.join(LANG_DIR, "*.ini"))):
        langs[os.path.basename(path)[:-4]] = translations(path)

    problems = []
    lines = []
    for lang, table in langs.items():
        for kind, text in strings:
            shown = text if lang == "en" or text in NUMBERS else translate(text, table)
            if shown is None:
                problems.append(f"not translated in {lang}: {text!r}")
                shown = text  # the firmware shows the English text
            lines.append(f"{kind}\t{lang}\t{shown}")

    work = tempfile.mkdtemp(prefix="audio_strings_")
    try:
        tools = build(work)
        widest = {}
        for target, exe in tools.items():
            p = subprocess.run([exe], input="\n".join(lines) + "\n", capture_output=True, text=True)
            for out in p.stdout.splitlines():
                verdict, kind, lang, size = out.split(" ")[:4]
                used, room = (int(x) for x in size.split("/"))
                key = (target, kind)
                if used * 100 // room > widest.get(key, (0, ""))[0]:
                    widest[key] = (used * 100 // room, out)
                if verdict != "OK":
                    problems.append(f"{target}: {out}")
                elif verbose:
                    print(f"{target}: {out}")
        for (target, kind), (pct, out) in sorted(widest.items()):
            print(f"widest {kind:8s} {target:10s} {pct:3d} %: {out}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    for p in problems:
        print("FAIL " + p)
    print(f"{len(strings)} strings x {len(langs)} languages x 2 layouts: "
          + ("all fit" if not problems else f"{len(problems)} problems"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
