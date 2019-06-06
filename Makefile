all: test

clean:
	rm -f test test-simd

test: Makefile smolscale.c smolscale.h png.c test.c
	gcc -Wall -Wextra -O2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx` smolscale.c png.c test.c -o test

test-debug: Makefile smolscale.c smolscale.h png.c test.c
	gcc -Wall -Wextra -O2 -fno-inline -fno-omit-frame-pointer -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx` smolscale.c png.c test.c -o test-debug

test-simd: Makefile smolscale.c smolscale.h test.c
	gcc -Wall -Wextra -fverbose-asm -Ofast -mfma -flto -fstrict-aliasing -ftree-vectorize -mcpu=native -march=native -mtune=native -mmmx -msse4 -msse4.2 -msse4.1 -msse2 -msse3 -msse -mavx -mavx2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx` smolscale.c png.c test.c -o test-simd
#	gcc -Wall -Wextra -Ofast -mfma -flto -ftracer -fstrict-aliasing -fsimd-cost-model=unlimited -funroll-all-loops -fpeel-loops -ftree-vectorize -mcpu=haswell -mmmx -msse4 -msse4.2 -msse4.1 -msse2 -msse3 -msse -mavx -mavx2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0 SDL_gfx` scale.c png.c test.c -o test-simd -fopt-info-vec-optimized -fopt-info-vec-missed -save-temps -fno-inline

# -fopt-info-vec-optimized -fopt-info-vec-missed
