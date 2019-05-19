/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <glib.h>
#include <png.h>
#include "scale.h"

#define CORRECTNESS_WIDTH_MIN 1
#define CORRECTNESS_WIDTH_MAX 65535
#define CORRECTNESS_WIDTH_STEPS 100
#define CORRECTNESS_HEIGHT_MIN 1
#define CORRECTNESS_HEIGHT_MAX 65535
#define CORRECTNESS_HEIGHT_STEPS 10

#define CORRECTNESS_WIDTH_STEP_SIZE (((CORRECTNESS_WIDTH_MAX) - (CORRECTNESS_WIDTH_MIN)) / (CORRECTNESS_WIDTH_STEPS))
#define CORRECTNESS_HEIGHT_STEP_SIZE (((CORRECTNESS_HEIGHT_MAX) - (CORRECTNESS_HEIGHT_MIN)) / (CORRECTNESS_HEIGHT_STEPS))

static void
scale_thread_worker (gpointer data, SmolScaleCtx *scale_ctx)
{
    guint32 first_row, n_rows;

    first_row = GPOINTER_TO_UINT (data) >> 16;
    n_rows = GPOINTER_TO_UINT (data) & 0xffff;
    smol_scale_batch (scale_ctx, first_row, n_rows);
}

static guint32 *
do_scale_threaded (const guint32 *pixels_in,
                   guint32 width_in, guint32 height_in,
                   guint32 width_out, guint32 height_out)
{
    SmolScaleCtx scale_ctx;
    guint32 *pixels_out;
    GThreadPool *thread_pool;
    guint32 n_threads;
    guint32 batch_n_rows;
    guint32 i;

    pixels_out = g_malloc (width_out * height_out * sizeof (guint32));

    smol_scale_init (&scale_ctx,
                     pixels_in,
                     width_in, height_in,
                     width_in * sizeof (guint32),
                     pixels_out,
                     width_out, height_out, width_out * sizeof (guint32));

    n_threads = g_get_num_processors ();
    thread_pool = g_thread_pool_new ((GFunc) scale_thread_worker,
                                     &scale_ctx,
                                     n_threads,
                                     FALSE,
                                     NULL);

    batch_n_rows = (height_out + n_threads - 1) / n_threads;

    for (i = 0; i < height_out; )
    {
        uint32_t n = MIN (batch_n_rows, height_out - i);
        g_thread_pool_push (thread_pool, GUINT_TO_POINTER ((i << 16) | n), NULL);
        i += n;
    }

    g_thread_pool_free (thread_pool, FALSE, TRUE);
    smol_scale_finalize (&scale_ctx);

    return pixels_out;
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
        const guint32 *row = canvas + y * width;

        for (x = 0; x < width; x++)
        {
            if (*row != color)
            {
                g_printerr ("%5u,%5u: Color is %08x (want %08x).\n",
                            x, y, *row, color);
                return FALSE;
            }

            row++;
        }
    }

    return TRUE;
}

static void
scale_and_check (const guint32 *pixels_in,
                 guint32 width_in, guint32 height_in,
                 guint32 width_out, guint32 height_out,
                 guint32 color)
{
    gpointer data_scaled;

    data_scaled = do_scale_threaded (pixels_in,
                                     width_in, height_in,
                                     width_out, height_out);
    check_color_canvas (data_scaled, width_out, height_out, color);
    g_free (data_scaled);
}

static void
check_all_levels (guint32 width_in, guint32 height_in,
                  guint32 width_out, guint32 height_out)
{
    guint c;
    guint32 *p_in;
    guint i;

    p_in = alloca (width_in * height_in * sizeof (guint32));

    for (c = 0; c < 256; c += 4)
    {
        guint32 pixel = (c << 24) | ((c + 1) << 16) | ((c + 2) << 8) | (c + 3);

        for (i = 0; i < width_in * height_in; i++)
            p_in [i] = pixel;

        scale_and_check (p_in, width_in, height_in, width_out, height_out, pixel);
    }
}

static void
check_both (void)
{
    guint i, j;

    i = CORRECTNESS_WIDTH_MIN;
    for (;;)
    {
        for (j = 1; j < MIN (i * 4, 65536); j++)
        {
            g_printerr ("\rWidth %u -> %u:        ", i, j);
            check_all_levels (i, 2, j, 2);
        }

        for (j = 1; j < MIN (i * 4, 65536); j++)
        {
            g_printerr ("\rHeight %u -> %u:        ", i, j);
            check_all_levels (2, i, 2, j);
        }

        if (i >= CORRECTNESS_WIDTH_MAX)
            break;
        i += CORRECTNESS_WIDTH_STEP_SIZE;
        i = MIN (i, CORRECTNESS_WIDTH_MAX);
    }
}

static void
run_correctness_test (void)
{
    check_both ();
}

int
main (int argc, char *argv [])
{
    run_correctness_test ();
    return 0;
}
