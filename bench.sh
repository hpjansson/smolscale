#!/bin/bash

DESC_gdkpixbuf="GDK-Pixbuf"
DESC_libswscale="libswscale"
DESC_pixman="Pixman"
DESC_sdl="SDL_gfx"
DESC_skia="Skia"
DESC_smolscale="Smolscale"
DESC_smolscale_mt="Smolscale MT"

TESTNAME_gdkpixbuf="gdk_pixbuf"
TESTNAME_libswscale="libswscale"
TESTNAME_pixman="pixman"
TESTNAME_sdl="sdl"
TESTNAME_skia="skia"
TESTNAME_smolscale="smol"
TESTNAME_smolscale_mt="smol-mt"

UNITS=" \
libswscale
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
"

TESTS=" \
50-2000-2000-0.001-1.5-500
10-16383-16383-0.0001-0.1-100
"

TESTS_AVAILABLE="\
20-2000-2000-0.001-1.5-50
50-2000-2000-0.001-1.5-500
10-16383-16383-0.0001-0.1-100
"

LD_LIBRARY_PATH="skia/out/Shared"

rm -Rf results
mkdir -p results

for UNIT in $UNITS; do
  for TEST in $TESTS; do
    echo $UNIT: $TEST

    eval DESC=\${DESC_${UNIT}}
    eval TESTNAME=\${TESTNAME_${UNIT}}

    echo \"${DESC}\" >>results/resize-$TEST.txt
    ./test $TESTNAME proportional $(echo $TEST | sed 's/-/ /g') >>results/resize-$TEST.txt
    echo >>results/resize-$TEST.txt
    echo >>results/resize-$TEST.txt
  done
done
