#!/bin/sh
# Dry run of the audio mixer without a goggle.
#
#   utilities/audio_mixer/dryrun.sh [output dir]
#
# Builds the routing code (src/core/audio_mixer_routing.c) for the host, writes
# the amixer batch of several mixer states, checks every control name and value
# against the device's `amixer contents` dump, and finally feeds one batch to a
# stub amixer the same way the firmware does (amixer -q -s < /tmp/audio_mixer.cmd).
#
# The stub can also be used with the emulator: start it with
#   PATH=<repo>/utilities/audio_mixer/stub:$PATH AMIXER_LOG=$PWD/amixer.log ./HDZGOGGLE
# and every batch the UI sends ends up in amixer.log.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$(mktemp -d)}
DUMP=$ROOT/mkapp/app/app/record/audio/audiocodec/alsa-util/amixer-record.txt
mkdir -p "$OUT"

${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -I"$ROOT/src" -o "$OUT/dryrun" \
    "$HERE/dryrun.c" "$ROOT/src/core/audio_mixer_routing.c"

scenario() {
    name=$1
    shift
    "$OUT/dryrun" "$@" > "$OUT/$name.cmd"
    echo "$name: $*"
}

echo "== scenarios (fields as in setting.ini [mixer]; source = old record/audio_source)"
scenario 01_migrated_mic source=0
scenario 02_migrated_line_in source=1
scenario 03_migrated_av_in source=2
scenario 04_as_heard_mic_av dvr_mode=0 mic=4 av=3
scenario 05_as_heard_extmic_av dvr_mode=0 line_in=6 line_in_boost=2 av=4
scenario 06_custom_mic_av_hear_av dvr_mode=1 dvr_mic=1 dvr_av=1 mic=0 av=8
scenario 07_all_max dvr_mode=0 mic=8 line_in=8 av=8 mic_boost=7 line_in_boost=7 headphones=31 dvr_level=7
scenario 08_all_off dvr_mode=1 mic=0 line_in=0 av=0 dvr_mic=0 headphones=0 dvr_level=0
scenario 09_hdmi_in line_out=0 dvr_mode=0 mic=4
scenario 10_playback playing=1 playback=20 mic=4 av=8
scenario 11_playback_after_hdmi playing=1 line_out=0 playback=27
scenario 12_out_of_range mic=99 headphones=200 dvr_level=9 mic_boost=8 dvr_mode=5

echo "== control names and ranges vs $(basename "$DUMP")"
python3 "$HERE/check_controls.py" "$DUMP" "$OUT"/*.cmd

echo "== stub amixer, as issued by the firmware"
cp "$OUT/04_as_heard_mic_av.cmd" "$OUT/audio_mixer.cmd"
rm -f "$OUT/amixer.log"
PATH="$HERE/stub:$PATH" AMIXER_LOG="$OUT/amixer.log" sh -c "amixer -q -s < '$OUT/audio_mixer.cmd'"
cat "$OUT/amixer.log"
echo "== batches in $OUT"
