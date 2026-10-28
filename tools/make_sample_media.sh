#!/bin/sh
# make_sample_media.sh - Makes the MP3 and Ogg Vorbis samples (tests/samples_media) from WAVs that
# Mapsmith generates itself (sme-make-samples --media-sources). Mapsmith has no MP3/Vorbis encoder,
# so these two small files are kept in the repository; run this only to change them.
#   tools/make_sample_media.sh build/tests/sme-make-samples      (needs ffmpeg with lame and vorbis)
set -eu
tool=${1:?path to sme-make-samples}
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
"$tool" --media-sources "$work"
ffmpeg -v error -y -i "$work/click.wav" -c:a libmp3lame -b:a 96k -map_metadata -1 -id3v2_version 0 -write_xing 0 "$here/tests/samples_media/click.mp3"
ffmpeg -v error -y -i "$work/wind_loop.wav" -c:a libvorbis -q:a 3 -map_metadata -1 -fflags +bitexact "$here/tests/samples_media/wind_loop.ogg"
rm -r "$work"
echo "written: tests/samples_media/click.mp3, wind_loop.ogg"
