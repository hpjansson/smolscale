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

typedef enum
{
    DIM_HORIZONTAL,
    DIM_VERTICAL
}
Dimension;

static gboolean
check_color_canvas (const guint32 *canvas_in,
                    guint width_in, guint height_in,
                    const guint32 *canvas_out,
                    guint width_out, guint height_out,
                    Dimension dim)
{
    guint32 color = canvas_in [0];
    guint x, y;

    /* Quick check */

    if (!memcmp (canvas_in, canvas_out, width_out * height_out * sizeof (guint32)))
        return TRUE;

    /* Something's wrong: Find the first errant pixel */

    for (y = 0; y < height_out; y++)
    {
        const guint32 *row = canvas_out + y * width_out;

        for (x = 0; x < width_out; x++)
        {
            if (*row != color)
            {
                g_print ("%s %u -> %u: [%5u,%5u] Color is %08x (want %08x).\n",
                         dim == DIM_HORIZONTAL ? "Width" : "Height",
                         dim == DIM_HORIZONTAL ? width_in : height_in,
                         dim == DIM_HORIZONTAL ? width_out : height_out,
                         x, y, *row, color);
                return FALSE;
            }

            row++;
        }
    }

    return TRUE;
}

static void
scale_and_check (const guint32 *canvas_in,
                 guint32 width_in, guint32 height_in,
                 guint32 width_out, guint32 height_out,
                 Dimension dim)
{
    gpointer canvas_out;

    canvas_out = do_scale (canvas_in,
                           width_in, height_in,
                           width_out, height_out);
    check_color_canvas (canvas_in,
                        width_in, height_in,
                        canvas_out,
                        width_out, height_out,
                        dim);
    g_free (canvas_out);
}

static void
check_all_levels (const guint32 * const *canvas_array,
                  guint32 width_in, guint32 height_in,
                  guint32 width_out, guint32 height_out,
                  Dimension dim)
{
    guint c;

    for (c = 0; c < 256; c += 4)
    {
        scale_and_check (canvas_array [c / 4],
                         width_in, height_in,
                         width_out, height_out,
                         dim);
    }
}

static void
check_both (void)
{
    guint32 *canvas_array [256 / 4];
    guint i, j;

    for (i = 0; i < 256; i += 4)
    {
        guint32 pixel = (i << 24) | ((i + 1) << 16) | ((i + 2) << 8) | (i + 3);

        canvas_array [i / 4] = g_malloc (65536 * 2 * sizeof (guint32));

        for (j = 0; j < 65536 * 2; j++)
            canvas_array [i / 4] [j] = pixel;
    }

    i = CORRECTNESS_WIDTH_MIN;
    for (;;)
    {
        for (j = 1; j < MIN (i + 1, 65536); j++)
        {
            g_printerr ("Width %u -> %u:        \r", i, j);
            check_all_levels ((const guint32 * const *) canvas_array,
                              i, 2, j, 2,
                              DIM_HORIZONTAL);
        }

        for (j = 1; j < MIN (i + 1, 65536); j++)
        {
            g_printerr ("Height %u -> %u:        \r", i, j);
            check_all_levels ((const guint32 * const *) canvas_array,
                              2, i, 2, j,
                              DIM_VERTICAL);
        }

        if (i >= CORRECTNESS_WIDTH_MAX)
            break;
        i += CORRECTNESS_WIDTH_STEP_SIZE;
        i = MIN (i, CORRECTNESS_WIDTH_MAX);
    }

    for (i = 0; i < 256 / 4; i++)
    {
        g_free (canvas_array [i]);
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
