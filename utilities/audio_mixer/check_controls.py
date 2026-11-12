#!/usr/bin/env python3
"""Check amixer batches (amixer -s syntax) against an `amixer contents` dump.

usage: check_controls.py <amixer contents dump> <batch file>...

Every `cset name='...' value` line must name a control of the dump and use a
value inside its range (INTEGER min..max, BOOLEAN 0/1, ENUMERATED item index).
Prints one line per batch and exits non-zero on the first bad batch.
"""

import re
import sys


def parse_dump(path):
    controls = {}
    current = None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"numid=\d+,iface=MIXER,name='(.*)'", line)
        if m:
            current = {"name": m.group(1)}
            controls[current["name"]] = current
            continue
        if current is None:
            continue
        m = re.search(r"type=(\w+),.*values=(\d+)", line)
        if m:
            current["type"] = m.group(1)
            current["count"] = int(m.group(2))
            mm = re.search(r"min=(-?\d+),max=(-?\d+)", line)
            if mm:
                current["min"], current["max"] = int(mm.group(1)), int(mm.group(2))
            mi = re.search(r"items=(\d+)", line)
            if mi:
                current["items"] = int(mi.group(1))
    return controls


def check_batch(controls, path):
    errors = []
    names = []
    for no, line in enumerate(open(path), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.fullmatch(r"cset name='([^']+)' (-?\d+)", line)
        if not m:
            errors.append(f"{path}:{no}: not a cset line: {line}")
            continue
        name, value = m.group(1), int(m.group(2))
        names.append(name)
        ctl = controls.get(name)
        if ctl is None:
            errors.append(f"{path}:{no}: unknown control '{name}'")
            continue
        kind = ctl.get("type")
        if kind == "INTEGER":
            lo, hi = ctl["min"], ctl["max"]
        elif kind == "BOOLEAN":
            lo, hi = 0, 1
        elif kind == "ENUMERATED":
            lo, hi = 0, ctl["items"] - 1
        else:
            errors.append(f"{path}:{no}: unexpected type {kind} for '{name}'")
            continue
        if not lo <= value <= hi:
            errors.append(f"{path}:{no}: '{name}' = {value} outside {lo}..{hi}")
    dup = sorted({n for n in names if names.count(n) > 1})
    if dup:
        errors.append(f"{path}: controls set twice: {dup}")
    return names, errors


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    controls = parse_dump(sys.argv[1])
    if not controls:
        print(f"no controls found in {sys.argv[1]}")
        return 2
    status = 0
    for path in sys.argv[2:]:
        names, errors = check_batch(controls, path)
        if errors:
            status = 1
            print("\n".join(errors))
        else:
            print(f"OK   {path}: {len(names)} controls, all names and values valid")
    return status


if __name__ == "__main__":
    sys.exit(main())
