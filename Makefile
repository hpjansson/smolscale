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

TEST_SYSDEPS_FLAGS=`pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx`
TEST_SRC=smolscale.c png.c test.c
TEST_FLAGS=-Wall -Wextra -O2 -g
TEST_DEBUG_FLAGS=-Wall -Wextra -O2 -g -fno-inline -fno-omit-frame-pointer
TEST_SIMD_FLAGS=-Wall -Wextra -Ofast -g -fverbose-asm -mfma -flto -fstrict-aliasing -ftree-vectorize -mcpu=native -march=native -mtune=native -mmmx -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mavx -mavx2

all: test

clean: FORCE
	rm -f test test-simd test-debug skia.o

test: Makefile smolscale.h $(TEST_SRC) $(SKIA_OBJ)
	gcc $(TEST_FLAGS) $(TEST_SYSDEPS_FLAGS) $(SKIA_CFLAGS) $(SKIA_LDFLAGS) $(TEST_SRC) -o test

test-debug: Makefile smolscale.h $(TEST_SRC) $(SKIA_OBJ)
	gcc $(TEST_DEBUG_FLAGS) $(TEST_SYSDEPS_FLAGS) $(SKIA_CFLAGS) $(SKIA_LDFLAGS) $(TEST_SRC) -o test-debug

test-simd: Makefile smolscale.h $(TEST_SRC) $(SKIA_OBJ)
	gcc $(TEST_SIMD_FLAGS) $(TEST_SYSDEPS_FLAGS) $(SKIA_CFLAGS) $(SKIA_LDFLAGS) $(TEST_SRC) -o test-simd
# -fopt-info-vec-optimized -fopt-info-vec-missed

skia.o: Makefile skia.cpp
	g++ -Wall -Wextra $(SKIA_CFLAGS) -c skia.cpp -o skia.o

FORCE:
