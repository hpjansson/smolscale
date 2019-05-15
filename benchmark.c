/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <glib.h>
#include <png.h>
#include <pixman.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "scale.h"

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

#if 0

static void
run_benchmark_chafa (guint width, guint height, gpointer data)
{
    guint target_width, target_height;

    for (target_height = TARGET_HEIGHT_MIN; target_height <= TARGET_HEIGHT_MAX; target_height += TARGET_HEIGHT_STEP_SIZE)
    {
        for (target_width = TARGET_WIDTH_MIN; target_width <= TARGET_WIDTH_MAX; target_width += TARGET_WIDTH_STEP_SIZE)
        {
            gpointer data_scaled;

            data_scaled = chafa_scale_bilinear_simple (data, width, height,
                                                       target_width, target_height);
            save_pixdata ("chafa", data_scaled, target_width, target_height);
            g_free (data_scaled);
        }

        g_printerr ("*");
        fflush (stderr);
    }
}

static double
min4 (double a, double b, double c, double d)
{
    double m1, m2;

    m1 = MIN (a, b);
    m2 = MIN (c, d);
    return MIN (m1, m2);
}

static double
max4 (double a, double b, double c, double d)
{
    double m1, m2;

    m1 = MAX (a, b);
    m2 = MAX (c, d);
    return MAX (m1, m2);
}

static void
compute_extents (pixman_f_transform_t *trans, double *sx, double *sy)
{
    double min_x, max_x, min_y, max_y;
    pixman_f_vector_t v[4] =
    {
	{ { 1, 1, 1 } },
	{ { -1, 1, 1 } },
	{ { -1, -1, 1 } },
	{ { 1, -1, 1 } },
    };

    pixman_f_transform_point (trans, &v[0]);
    pixman_f_transform_point (trans, &v[1]);
    pixman_f_transform_point (trans, &v[2]);
    pixman_f_transform_point (trans, &v[3]);

    min_x = min4 (v[0].v[0], v[1].v[0], v[2].v[0], v[3].v[0]);
    max_x = max4 (v[0].v[0], v[1].v[0], v[2].v[0], v[3].v[0]);
    min_y = min4 (v[0].v[1], v[1].v[1], v[2].v[1], v[3].v[1]);
    max_y = max4 (v[0].v[1], v[1].v[1], v[2].v[1], v[3].v[1]);

    *sx = (max_x - min_x) / 2.0;
    *sy = (max_y - min_y) / 2.0;
}

static void
scale_pixman (guint width, guint height, guint target_width, guint target_height, pixman_image_t *pixman_image)
{
    pixman_f_transform_t ftransform;
    pixman_transform_t transform;
    double fscale_x, fscale_y;
    pixman_fixed_t *params;
    int n_params;
    guint32 *pixels;
    pixman_image_t *tmp;

    /* Setup */

    pixman_f_transform_init_identity (&ftransform);

    fscale_x = (double) width / (double) target_width;
    fscale_y = (double) height / (double) target_height;

    pixman_f_transform_scale (&ftransform, NULL, fscale_x, fscale_y);
    pixman_transform_from_pixman_f_transform (&transform, &ftransform);

    pixman_image_set_transform (pixman_image, &transform);

    params = pixman_filter_create_separable_convolution (
        &n_params,
        pixman_double_to_fixed (fscale_x),
        pixman_double_to_fixed (fscale_y),
        PIXMAN_KERNEL_IMPULSE,
        PIXMAN_KERNEL_IMPULSE,
        PIXMAN_KERNEL_LINEAR,
        PIXMAN_KERNEL_LINEAR,
        1,
        1);

#if 1
    pixman_image_set_filter (pixman_image, PIXMAN_FILTER_SEPARABLE_CONVOLUTION, params, n_params);
#else
    pixman_image_set_filter (pixman_image, PIXMAN_FILTER_BILINEAR, NULL, 0);
#endif
    pixman_image_set_repeat (pixman_image, PIXMAN_REPEAT_NONE);
    
    free (params);

    /* Scale */

    pixels = calloc (1, target_width * target_height * sizeof (guint32));
    tmp = pixman_image_create_bits (
        PIXMAN_r8g8b8a8, target_width, target_height, pixels, target_width * 4);

    pixman_image_composite (
        PIXMAN_OP_SRC,
        pixman_image, NULL, tmp,
        0, 0, 0, 0, 0, 0,
        target_width, target_height);

    save_pixdata ("pixman", pixels, target_width, target_height);
    free (pixels);
    pixman_image_unref (tmp);
}

static void
run_benchmark_pixman (guint width, guint height, gpointer data)
{
    guint target_width, target_height;
    pixman_image_t *pixman_image;

    pixman_image = pixman_image_create_bits (PIXMAN_r8g8b8a8, width, height, data, width * sizeof (guint32));

    for (target_height = TARGET_HEIGHT_MIN; target_height <= TARGET_HEIGHT_MAX; target_height += TARGET_HEIGHT_STEP_SIZE)
    {
        for (target_width = TARGET_WIDTH_MIN; target_width <= TARGET_WIDTH_MAX; target_width += TARGET_WIDTH_STEP_SIZE)
        {
            scale_pixman (width, height, target_width, target_height, pixman_image);
        }

        g_printerr ("*");
        fflush (stderr);
    }
}

#endif

typedef struct
{
    guint in_width, in_height;
    gpointer in_data, out_data;
}
ScaleParams;

typedef void (*ScaleInitFunc) (ScaleParams *, gpointer, guint, guint);
typedef void (*ScaleFiniFunc) (ScaleParams *);
typedef void (*ScaleDoFunc) (ScaleParams *, guint, guint);

/* --- GDK-Pixbuf --- */

static void
scale_init_gdk_pixbuf (ScaleParams *params, gpointer in_raw, guint in_width, guint in_height)
{
    params->in_data = gdk_pixbuf_new_from_data (in_raw, GDK_COLORSPACE_RGB, TRUE,
                                                8, in_width, in_height, in_width * sizeof (guint32),
                                                NULL, NULL);
}

static void
scale_fini_gdk_pixbuf (ScaleParams *params)
{
    g_object_unref (params->in_data);
}

static void
scale_do_gdk_pixbuf (ScaleParams *params, guint out_width, guint out_height)
{
    GdkPixbuf *scaled;

    scaled = gdk_pixbuf_scale_simple (params->in_data, out_width, out_height,
                                      GDK_INTERP_BILINEAR);
    g_object_unref (scaled);
}

/* --- Smolscale --- */

static void
scale_init_smol (ScaleParams *params, gpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = in_raw;
}

static void
scale_fini_smol (ScaleParams *params)
{
}

static void
scale_do_smol (ScaleParams *params, guint out_width, guint out_height)
{
    gpointer scaled;

    scaled = smol_scale_simple (params->in_data,
                                params->in_width, params->in_height,
                                out_width, out_height);
    g_free (scaled);
}

static gdouble
compute_elapsed (struct timespec *before, struct timespec *after)
{
    gint64 before_usec = before->tv_nsec / 1000;
    gint64 after_usec = after->tv_nsec / 1000;
    guint64 diff;

    diff = (after->tv_sec * 1000000) - (before->tv_sec * 1000000);
    diff += (after_usec - before_usec);

    return diff / (gdouble) 1000000.0;
}

static void
run_benchmark (gpointer raw_data,
               guint n_repetitions,
               guint in_width, guint in_height,
               guint out_width_min, guint out_width_max,
               guint out_height_min, guint out_height_max,
               guint n_width_steps, guint n_height_steps,
               ScaleInitFunc init_func,
               ScaleFiniFunc fini_func,
               ScaleDoFunc do_func)
{
    gfloat width_step_size, height_step_size;
    guint width_step, height_step;
    guint rep;
    ScaleParams params;
    gdouble *results;

    if (n_width_steps > 1)
        width_step_size = (out_width_max - out_width_min) / ((gfloat) n_width_steps - 1.0);
    else
        width_step_size = 99999.0;

    if (n_height_steps > 1)
        height_step_size = (out_height_max - out_height_min) / ((gfloat) n_height_steps - 1.0);
    else
        height_step_size = 99999.0;

    results = alloca (n_width_steps * n_height_steps * n_repetitions * sizeof (gdouble));

    (*init_func) (&params, raw_data, in_width, in_height);

    for (rep = 0; rep < n_repetitions; rep++)
    {
        for (height_step = 0; height_step < n_height_steps; height_step++)
        {
            for (width_step = 0; width_step < n_width_steps; width_step++)
            {
                struct timespec before, after;

                clock_gettime (CLOCK_MONOTONIC_RAW, &before);
                (*do_func) (&params,
                            out_width_min + width_step * width_step_size,
                            out_height_min + height_step * height_step_size);
                clock_gettime (CLOCK_MONOTONIC_RAW, &after);
                results [width_step * n_height_steps * n_repetitions
                         + height_step * n_repetitions
                         + rep] = compute_elapsed (&before, &after);
            }

            g_printerr ("*");
            fflush (stderr);
        }
    }

    g_printerr ("\n");
    fflush (stderr);

    (*fini_func) (&params);

    for (width_step = 0; width_step < n_width_steps; width_step++)
    {
        for (height_step = 0; height_step < n_height_steps; height_step++)
        {
            gdouble best_time = 999999.9;

            for (rep = 0; rep < n_repetitions; rep++)
            {
                gdouble t = results [width_step * n_height_steps * n_repetitions
                                     + height_step * n_repetitions
                                     + rep];

                if (t < best_time)
                    best_time = t;
            }

            g_print ("%u %u %lf\n",
                     (guint) (out_width_min + width_step * width_step_size),
                     (guint) (out_height_min + height_step * height_step_size),
                     best_time);
        }
    }
}

static void
print_usage (void)
{
    g_printerr ("Usage: benchmark <smol|pixman|gdk_pixbuf>\n"
                "                 [ <n_repetitions>\n"
                "                   <in_width> <in_height>\n"
                "                   <min_width> <max_width> <width_steps>\n"
                "                   <min_height> <max_height> <height_steps> ]\n");
}

#define DEFAULT_N_REPETITIONS 3
#define DEFAULT_IN_WIDTH 1024
#define DEFAULT_IN_HEIGHT 1024
#define DEFAULT_OUT_WIDTH_MIN 2
#define DEFAULT_OUT_WIDTH_MAX 2048
#define DEFAULT_OUT_WIDTH_STEPS 4
#define DEFAULT_OUT_HEIGHT_MIN 2
#define DEFAULT_OUT_HEIGHT_MAX 2048
#define DEFAULT_OUT_HEIGHT_STEPS 4

int
main (int argc, char *argv [])
{
    guint n_repetitions;
    guint in_width, in_height;
    guint out_width_min, out_width_max, out_width_steps;
    guint out_height_min, out_height_max, out_height_steps;
    gpointer raw_data;
    ScaleInitFunc init_func;
    ScaleFiniFunc fini_func;
    ScaleDoFunc do_func;
    gint i;

    if (argc < 2)
    {
        print_usage ();
        return 1;
    }
    else if (argc > 2 && argc != 11)
    {
        g_printerr ("Error: All or no optional arguments must be present.\n");
        print_usage ();
        return 1;
    }

    n_repetitions = DEFAULT_N_REPETITIONS;
    in_width = DEFAULT_IN_WIDTH;
    in_height = DEFAULT_IN_HEIGHT;
    out_width_min = DEFAULT_OUT_WIDTH_MIN;
    out_width_max = DEFAULT_OUT_WIDTH_MAX;
    out_width_steps = DEFAULT_OUT_WIDTH_STEPS;
    out_height_min = DEFAULT_OUT_HEIGHT_MIN;
    out_height_max = DEFAULT_OUT_HEIGHT_MAX;
    out_height_steps = DEFAULT_OUT_HEIGHT_STEPS;

    if (!strcasecmp (argv [1], "gdk_pixbuf"))
    {
        init_func = scale_init_gdk_pixbuf;
        fini_func = scale_fini_gdk_pixbuf;
        do_func = scale_do_gdk_pixbuf;
    }
    else if (!strcasecmp (argv [1], "smol"))
    {
        init_func = scale_init_smol;
        fini_func = scale_fini_smol;
        do_func = scale_do_smol;
    }
    else
    {
        print_usage ();
        return 1;
    }

    if (argc == 11)
    {
        i = 2;
        n_repetitions = strtoul (argv [i++], NULL, 10);
        in_width = strtoul (argv [i++], NULL, 10);
        in_height = strtoul (argv [i++], NULL, 10);
        out_width_min = strtoul (argv [i++], NULL, 10);
        out_width_max = strtoul (argv [i++], NULL, 10);
        out_width_steps = strtoul (argv [i++], NULL, 10);
        out_height_min = strtoul (argv [i++], NULL, 10);
        out_height_max = strtoul (argv [i++], NULL, 10);
        out_height_steps = strtoul (argv [i++], NULL, 10);
    }

    raw_data = gen_color_canvas (in_width, in_height, 0x55555555);

    run_benchmark (raw_data,
                   n_repetitions,
                   in_width, in_height,
                   out_width_min, out_width_max,
                   out_height_min, out_height_max,
                   out_width_steps, out_height_steps,
                   init_func, fini_func, do_func);

    g_free (raw_data);
    return 0;
}
