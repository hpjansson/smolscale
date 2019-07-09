# Compilers to use for C and C++. E.g. gcc and g++, or clang and clang++.
CC=gcc
CXX=g++

# Set this to 'yes', without the quotes, to build AVX2 optimized
# backend for Smolscale, 'no' otherwise. The backend supplements the
# generic one and will only be used if CPU support is detected
# at runtime.
WITH_AVX2=yes

# Set this to either 'yes' or 'no', without the quotes. You need
# Skia checked out and built (Shared target) in the skia/
# subdirectory.
WITH_SKIA=no

# --- #

GENERAL_CFLAGS=-Wall -Wextra -g

SMOL_CFLAGS=$(GENERAL_CFLAGS) -O3

TEST_CFLAGS=$(GENERAL_CFLAGS) -O3 -flto
TEST_SYSDEPS_FLAGS=`pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx`
TEST_DEBUG_FLAGS=$(GENERAL_CFLAGS) -O2 -g -fno-inline -fno-omit-frame-pointer

SKIA_CFLAGS=$(GENERAL_CFLAGS) -O3 -Iskia -Iskia/include/core

ifeq ($(WITH_AVX2),yes)
  SMOL_CFLAGS+=-DSMOL_WITH_AVX2
  SMOL_OBJ=smolscale.o smolscale-avx2.o
else
  SMOL_OBJ=smolscale.o
endif

SMOL_AVX2_CFLAGS=$(SMOL_CFLAGS) -fverbose-asm -mavx2

ifeq ($(WITH_SKIA),yes)
  TEST_CFLAGS+=-DWITH_SKIA
  TEST_LDFLAGS+=-Lskia/out/Shared -lskia -lstdc++ skia.o
  SKIA_OBJ=skia.o
else
  SKIA_OBJ=
endif

TEST_SRC=png.c test.c

all: test

clean: FORCE
	rm -f test $(SMOL_OBJ) $(SKIA_OBJ)

test: Makefile smolscale.h $(TEST_SRC) $(SMOL_OBJ) $(SKIA_OBJ)
	$(CC) $(TEST_CFLAGS) $(TEST_LDFLAGS) $(TEST_SYSDEPS_FLAGS) $(SMOL_OBJ) $(TEST_SRC) -o test

smolscale.o: Makefile smolscale.c smolscale.h smolscale-private.h
	$(CC) $(SMOL_CFLAGS) -c smolscale.c -o smolscale.o

smolscale-avx2.o: Makefile smolscale-avx2.c smolscale.h smolscale-private.h
	$(CC) $(SMOL_AVX2_CFLAGS) -c smolscale-avx2.c -o smolscale-avx2.o

skia.o: Makefile skia.cpp
	$(CXX) -Wall -Wextra $(SKIA_CFLAGS) -c skia.cpp -o skia.o

FORCE:
