# Set this to either 'yes' or 'no', without the quotes
WITH_SKIA=yes

ifeq ($(WITH_SKIA),yes)
  SKIA_OBJ=skia.o
  SKIA_CFLAGS=-DWITH_SKIA -Iskia -Iskia/include/core
  SKIA_LDFLAGS=-Lskia/out/Shared -lskia -lstdc++ skia.o
else
  SKIA_OBJ=
  SKIA_CFLAGS=
  SKIA_LDFLAGS=
endif

TEST_DEPS=`pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx`

all: test

clean: FORCE
	rm -f test test-simd test-debug skia.o

test: Makefile smolscale.c smolscale.h png.c test.c $(SKIA_OBJ)
	gcc -Wall -Wextra -O2 -g $(TEST_DEPS) $(SKIA_CFLAGS) $(SKIA_LDFLAGS) smolscale.c png.c test.c -o test

test-debug: Makefile smolscale.c smolscale.h png.c test.c $(SKIA_OBJ)
	gcc -Wall -Wextra -O2 -fno-inline -fno-omit-frame-pointer -g $(TEST_DEPS) $(SKIA_CFLAGS) $(SKIA_LDFLAGS) smolscale.c png.c test.c -o test-debug

test-simd: Makefile smolscale.c smolscale.h png.c test.c $(SKIA_OBJ)
	gcc -Wall -Wextra -fverbose-asm -Ofast -mfma -flto -fstrict-aliasing -ftree-vectorize -mcpu=native -march=native -mtune=native -mmmx -msse4 -msse4.2 -msse4.1 -msse2 -msse3 -msse -mavx -mavx2 -g $(TEST_DEPS) $(SKIA_CFLAGS) $(SKIA_LDFLAGS) smolscale.c png.c test.c -o test-simd
# -fopt-info-vec-optimized -fopt-info-vec-missed

skia.o: Makefile skia.cpp
	g++ -Wall -Wextra $(SKIA_CFLAGS) -c skia.cpp -o skia.o

FORCE:
