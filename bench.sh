#!/bin/bash

DESC_gdkpixbuf="GDK-Pixbuf"
DESC_libswscale="libswscale"
DESC_pixman="Pixman"
DESC_sdl="SDL_gfx"
DESC_skia="Skia"
DESC_smolscale="Smolscale"
DESC_smolscale_mt="Smolscale MT"
DESC_stb="stb_resize"

TESTNAME_gdkpixbuf="gdk_pixbuf"
TESTNAME_libswscale="libswscale"
TESTNAME_pixman="pixman"
TESTNAME_sdl="sdl"
TESTNAME_skia="skia"
TESTNAME_smolscale="smol"
TESTNAME_smolscale_mt="smol-mt"
TESTNAME_stb="stb"

UNITS=" \
gdkpixbuf
libswscale
pixman
stb
smolscale
smolscale_mt
"

UNITS_AVAILABLE=" \
gdkpixbuf
libswscale
pixman
sdl
skia
smolscale
smolscale_mt
stb
"

TESTS=" \
5-1920-1080-0.01-2-200
5-3840-2160-0.01-2-200
5-7680-4320-0.01-1-200
"

TESTS_AVAILABLE="\
20-2000-2000-0.001-1.5-50
50-2000-2000-0.001-1.5-500
10-3840-2160-0.001-2-100
10-16383-16383-0.0001-0.1-100
"

export LD_LIBRARY_PATH="$PWD/skia/out/Shared"

rm -Rf results
mkdir -p results

for UNIT in $UNITS; do
  for TEST in $TESTS; do
    echo $UNIT: $TEST

    eval DESC=\${DESC_${UNIT}}
    eval TESTNAME=\${TESTNAME_${UNIT}}

    echo \"${DESC}\" >>results/resize-$TEST-samples.txt
    echo -n "\"${DESC}\" " >>results/resize-$TEST-average.txt
    ./test $TESTNAME benchmark $(echo $TEST | sed 's/-/ /g') \
      results/resize-$TEST-average.txt \
      results/resize-$TEST-samples.txt
    echo >>results/resize-$TEST-samples.txt
    echo >>results/resize-$TEST-samples.txt
  done
done
