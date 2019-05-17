/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <glib.h>
#include <png.h>
#include "scale.h"

#define CORRECTNESS_WIDTH_MIN 2
#define CORRECTNESS_WIDTH_MAX 65535
#define CORRECTNESS_WIDTH_STEPS 100
#define CORRECTNESS_HEIGHT_MIN 64
#define CORRECTNESS_HEIGHT_MAX 65535
#define CORRECTNESS_HEIGHT_STEPS 10

#define CORRECTNESS_WIDTH_STEP_SIZE (((CORRECTNESS_WIDTH_MAX) - (CORRECTNESS_WIDTH_MIN)) / (CORRECTNESS_WIDTH_STEPS))
#define CORRECTNESS_HEIGHT_STEP_SIZE (((CORRECTNESS_HEIGHT_MAX) - (CORRECTNESS_HEIGHT_MIN)) / (CORRECTNESS_HEIGHT_STEPS))

static gpointer
gen_color_canvas (guint width, guint height, guint32 color)
{
    guint32 *canvas;
    guint32 *canvas_end;
    guint32 *p;

    canvas = g_malloc (width * height * sizeof (guint32));
    canvas_end = canvas + width * height;

    for (p = canvas; p < canvas_end; p++)
        *p = color;
    
    return canvas;
}

static gboolean
check_color_canvas (const guint32 *canvas, guint width, guint height, guint32 color)
{
    const guint32 *p;
    guint x, y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            p = canvas + x + y * width;

            if (*p != color)
            {
                g_printerr ("%5u,%5u: Color is %08x (want %08x).\n",
                            x, y, *p, color);
                return FALSE;
            }
        }
    }

    return TRUE;
}

static guint32 *
do_scale (const guint32 *pixels_in,
          guint32 width_in, guint32 height_in,
          guint32 width_out, guint32 height_out)
{
    guint32 *scaled;

    scaled = g_new (guint32, width_out * height_out);
    smol_scale_simple (pixels_in, width_in, height_in, width_in * sizeof (guint32),
                       scaled, width_out, height_out, width_out * sizeof (guint32));
    return scaled;
}

static void
check_horizontal (void)
{
    guint i, j;

    i = CORRECTNESS_WIDTH_MIN;
    for (;;)
    {
        gpointer canvas [2];

        canvas [0] = gen_color_canvas (i, 2, 0xffffffff);
        canvas [1] = gen_color_canvas (i, 2, 0x01010101);

        for (j = 2; j < MIN (i * 4, 65536); j++)
        {
            gpointer data_scaled;

            g_printerr ("\rWidth %u -> %u:        ", i, j);

            data_scaled = do_scale (canvas [0], i, 2, j, 2);
            check_color_canvas (data_scaled, j, 2, 0xffffffff);
            g_free (data_scaled);

            data_scaled = do_scale (canvas [1], i, 2, j, 2);
            check_color_canvas (data_scaled, j, 2, 0x01010101);
            g_free (data_scaled);
        }

        g_free (canvas [0]);
        g_free (canvas [1]);

        if (i >= CORRECTNESS_WIDTH_MAX)
            break;
        i += CORRECTNESS_WIDTH_STEP_SIZE;
        i = MIN (i, CORRECTNESS_WIDTH_MAX);
    }
}

static void
check_vertical (void)
{
    guint i, j;

    i = CORRECTNESS_HEIGHT_MIN;
    for (;;)
    {
        gpointer canvas [2];

        canvas [0] = gen_color_canvas (2, i, 0xffffffff);
        canvas [1] = gen_color_canvas (2, i, 0x01010101);

        for (j = 2; j < MIN (i * 4, 65536); j++)
        {
            gpointer data_scaled;

            g_printerr ("\rHeight %u -> %u:        ", i, j);

            data_scaled = do_scale (canvas [0], 2, i, 2, j);
            check_color_canvas (data_scaled, 2, j, 0xffffffff);
            g_free (data_scaled);

            data_scaled = do_scale (canvas [1], 2, i, 2, j);
            check_color_canvas (data_scaled, 2, j, 0x01010101);
            g_free (data_scaled);
        }

        g_free (canvas [0]);
        g_free (canvas [1]);

        if (i >= CORRECTNESS_HEIGHT_MAX)
            break;
        i += CORRECTNESS_HEIGHT_STEP_SIZE;
        i = MIN (i, CORRECTNESS_HEIGHT_MAX);
    }
}

static void
check_both (void)
{
    guint i, j;

    i = CORRECTNESS_WIDTH_MIN;
    for (;;)
    {
        gpointer canvas [2];

        canvas [0] = gen_color_canvas (2, i, 0xffffffff);
        canvas [1] = gen_color_canvas (2, i, 0x01010101);

        for (j = 2; j < MIN (i * 4, 65536); j++)
        {
            gpointer data_scaled;

            g_printerr ("\rWidth %u -> %u:        ", i, j);

            data_scaled = do_scale (canvas [0], i, 2, j, 2);
            check_color_canvas (data_scaled, j, 2, 0xffffffff);
            g_free (data_scaled);

            data_scaled = do_scale (canvas [1], i, 2, j, 2);
            check_color_canvas (data_scaled, j, 2, 0x01010101);
            g_free (data_scaled);
        }

        for (j = 2; j < MIN (i * 4, 65536); j++)
        {
            gpointer data_scaled;

            g_printerr ("\rHeight %u -> %u:        ", i, j);

            data_scaled = do_scale (canvas [0], 2, i, 2, j);
            check_color_canvas (data_scaled, 2, j, 0xffffffff);
            g_free (data_scaled);

            data_scaled = do_scale (canvas [1], 2, i, 2, j);
            check_color_canvas (data_scaled, 2, j, 0x01010101);
            g_free (data_scaled);
        }

        g_free (canvas [0]);
        g_free (canvas [1]);

        if (i >= CORRECTNESS_WIDTH_MAX)
            break;
        i += CORRECTNESS_WIDTH_STEP_SIZE;
        i = MIN (i, CORRECTNESS_WIDTH_MAX);
    }
}

static void
run_correctness_test (void)
{
    guint width, height;
    guint in_width, in_height;
    gpointer canvas;

    check_both ();

    in_width = 2500;
    in_height = 2500;
    canvas = gen_color_canvas (in_width, in_height, 0xffffffff);

    for (height = CORRECTNESS_HEIGHT_MIN; height <= CORRECTNESS_HEIGHT_MAX; height += CORRECTNESS_HEIGHT_STEP_SIZE)
    {
        for (width = CORRECTNESS_WIDTH_MIN; width <= CORRECTNESS_WIDTH_MAX; width += CORRECTNESS_WIDTH_STEP_SIZE)
        {
            gpointer data_scaled;

            data_scaled = do_scale (canvas,
                                    in_width, in_height,
                                    width, height);
#if 0
            save_pixdata ("chafa", data_scaled, width, height);
#endif

            g_printerr ("%ux%u -> %ux%u: ", in_width, in_height, width, height);
            if (check_color_canvas (data_scaled, width, height, 0xffffffff))
                g_printerr ("ok\n");
            g_free (data_scaled);
        }

        fflush (stderr);
    }

    g_free (canvas);
}

int
main (int argc, char *argv [])
{
    run_correctness_test ();
    return 0;
}
