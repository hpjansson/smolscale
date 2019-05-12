all: test

test: scale.c png.c test.c
	gcc -Wall -Wextra -O2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0` scale.c png.c test.c -o test

benchmark: scale.c png.c benchmark.c
	gcc -Wall -Wextra -O2 -g `pkg-config --libs --cflags glib-2.0 libpng pixman-1 gdk-pixbuf-2.0` scale.c png.c test.c -o test
