all: test benchmark

clean:
	rm -f test benchmark benchmark-simd

test: Makefile scale.c scale.h png.c test.c
	gcc -Wall -Wextra -O2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0` scale.c png.c test.c -o test

benchmark: Makefile scale.c scale.h benchmark.c
	gcc -Wall -Wextra -O2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0` scale.c png.c benchmark.c -o benchmark

benchmark-simd: Makefile scale.c scale.h benchmark.c
	gcc -Wall -Wextra -O3 -fstrict-aliasing -fsimd-cost-model=unlimited -funroll-loops -ftree-vectorize -fopt-info-vec-optimized -fopt-info-vec-missed -mcpu=haswell -mmmx -msse4 -msse4.2 -msse4.1 -msse2 -msse3 -msse2 -mavx -mavx2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0` scale.c png.c benchmark.c -o benchmark-simd
