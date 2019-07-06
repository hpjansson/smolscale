/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson. See COPYING for details. */

/* read_png_file() and write_png_file() are based on an example from
 * the libpng documentation. */

#include <stdlib.h> /* malloc, abort */
#include <string.h> /* memcpy */
#include <glib.h>
#include <png.h>

typedef struct
{
  png_bytep *rows;
  int width, height;
  png_byte color_type;
  png_byte bit_depth;
}
PngImage;

static void
abort_ (const char *s, ...)
{
  va_list args;

  va_start (args, s);
  vfprintf (stderr, s, args);
  fprintf (stderr, "\n");
  va_end (args);
  abort ();
}

static void
read_png_file (const char *file_name, PngImage *png_image)
{
  unsigned char header [8];
  png_structp png_ptr;
  png_infop info_ptr;
  int y;
  FILE *fp;

  /* Open file and check file type */

  fp = fopen (file_name, "rb");
  if (!fp)
    abort_ ("File %s could not be opened for reading", file_name);

  fread (header, 1, 8, fp);

  if (png_sig_cmp (header, 0, 8))
    abort_ ("File %s is not a PNG file", file_name);

  /* Initialize */

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr)
    abort_ ("png_create_read_struct failed");

  info_ptr = png_create_info_struct (png_ptr);
  if (!info_ptr)
    abort_ ("png_create_info_struct failed");

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error during init_io");

  png_init_io (png_ptr, fp);
  png_set_sig_bytes (png_ptr, 8);

  png_read_info (png_ptr, info_ptr);

  png_image->width = png_get_image_width (png_ptr, info_ptr);
  png_image->height = png_get_image_height (png_ptr, info_ptr);
  png_image->color_type = png_get_color_type (png_ptr, info_ptr);
  png_image->bit_depth = png_get_bit_depth (png_ptr, info_ptr);

  png_read_update_info (png_ptr, info_ptr);

  /* Read file */

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error during read_image");

  png_image->rows = (png_bytep*) malloc (sizeof (png_bytep) * png_image->height);
  for (y = 0; y < png_image->height; y++)
    png_image->rows [y] = (png_byte*) malloc (png_get_rowbytes (png_ptr, info_ptr));

  png_read_image (png_ptr, png_image->rows);
  fclose (fp);

  if (png_get_color_type (png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB)
    abort_ ("Input file is PNG_COLOR_TYPE_RGB but must be PNG_COLOR_TYPE_RGBA "
            "(missing alpha channel)");

  if (png_get_color_type (png_ptr, info_ptr) != PNG_COLOR_TYPE_RGBA)
    abort_ ("Color_type of input file must be PNG_COLOR_TYPE_RGBA (%d) (is %d)",
            PNG_COLOR_TYPE_RGBA, png_get_color_type (png_ptr, info_ptr));
}

static void
write_png_file (PngImage *png_image, char *file_name)
{
  FILE *fp = fopen (file_name, "wb");
  png_structp png_ptr;
  png_infop info_ptr;

  if (!fp)
    abort_ ("File %s could not be opened for writing", file_name);

  /* Initialize */

  png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (!png_ptr)
    abort_ ("png_create_write_struct failed");

  info_ptr = png_create_info_struct (png_ptr);
  if (!info_ptr)
    abort_ ("png_create_info_struct failed");

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error during init_io");

#if 1
  png_set_compression_level (png_ptr, 5);
#endif

  png_init_io (png_ptr, fp);
        
  /* Write header */

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error writing PNG header");

  png_set_IHDR (png_ptr, info_ptr, png_image->width, png_image->height,
                png_image->bit_depth, png_image->color_type, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_write_info (png_ptr, info_ptr);

  /* Write data */

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error writing PNG data");

  png_write_image (png_ptr, png_image->rows);

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error writing PNG data (end of write)");

  png_write_end (png_ptr, NULL);

  /* Cleanup */

  fclose (fp);
}

gboolean
smoltest_load_image (const gchar *file_name,
                     guint *width_out, guint *height_out,
                     gpointer *data_out)
{
    PngImage png_image;
    guint32 *data_u32, *p;
    guint row;
    guint col_size;

    read_png_file (file_name, &png_image);
    p = data_u32 = g_new (guint32, png_image.width * png_image.height);
    col_size = png_image.width * sizeof (guint32);

    for (row = 0; row < (guint) png_image.height; row++)
    {
        memcpy (p, png_image.rows [row], col_size);
        p += png_image.width;
    }

    *width_out = png_image.width;
    *height_out = png_image.height;
    *data_out = data_u32;

    return TRUE;
}

void
smoltest_save_image (const gchar *prefix, guint32 *data, guint width, guint height)
{
    PngImage png_image = { 0 };
    guint32 **rows;
    gchar *filename;
    guint i;

    png_image.width = width;
    png_image.height = height;
    png_image.bit_depth = 8;
    png_image.color_type = PNG_COLOR_TYPE_RGB_ALPHA;

    rows = alloca (height * sizeof (gpointer));
    png_image.rows = (png_byte **) rows;

    for (i = 0; i < height; i++)
    {
        rows [i] = data + i * width;
    }

    filename = g_strdup_printf ("%s-%04d-%04d.png", prefix, width, height);
    write_png_file (&png_image, filename);
    g_free (filename);
}
