/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019 Hans Petter Jansson. See COPYING for details. */

#include <math.h>  /* stb_image_resize needs pow() */
#include <sys/random.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <pixman.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <SDL/SDL_rotozoom.h> /* zoomSurface() from SDL_gfx */
#include <libswscale/swscale.h>
#include <stdlib.h> /* strtoul, strtod */
#include "smolscale.h"
#include "png.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define PIXEL_TYPE_SMOL SMOL_PIXEL_ARGB8_PREMULTIPLIED
#define PIXEL_TYPE_LIBSWSCALE AV_PIX_FMT_ARGB

#define CORRECTNESS_WIDTH_MIN 1
#define CORRECTNESS_WIDTH_MAX 65535
#define CORRECTNESS_WIDTH_STEPS 100
#define CORRECTNESS_HEIGHT_MIN 1
#define CORRECTNESS_HEIGHT_MAX 65535
#define CORRECTNESS_HEIGHT_STEPS 10

#define CORRECTNESS_WIDTH_STEP_SIZE (((CORRECTNESS_WIDTH_MAX) - (CORRECTNESS_WIDTH_MIN)) / (CORRECTNESS_WIDTH_STEPS))
#define CORRECTNESS_HEIGHT_STEP_SIZE (((CORRECTNESS_HEIGHT_MAX) - (CORRECTNESS_HEIGHT_MIN)) / (CORRECTNESS_HEIGHT_STEPS))

#define BENCHMARK_CONV_WIDTH 3840
#define BENCHMARK_CONV_HEIGHT 2160

/* Defined in png.c */
gboolean smoltest_load_image (const gchar *file_name,
                              guint *width_out,
                              guint *height_out,
                              gpointer *data_out);
void smoltest_save_image (const gchar *prefix,
                          guint32 *data,
                          guint width,
                          guint height);

/* --- Common --- */

static SmolPixelType smol_ptype_in = PIXEL_TYPE_SMOL;
static SmolPixelType smol_ptype_out = PIXEL_TYPE_SMOL;

typedef struct
{
    guint in_width, in_height;
    gpointer in_data, out_data;
    gpointer priv;
}
ScaleParams;

typedef enum
{
    SCALE_OP_BENCHMARK_PROP,
    SCALE_OP_BENCHMARK_CONV,
    SCALE_OP_CHECK,
    SCALE_OP_GENERATE
}
ScaleOperation;

typedef void (*ScaleInitFunc) (ScaleParams *, gconstpointer, guint, guint);
typedef void (*ScaleFiniFunc) (ScaleParams *);
typedef void (*ScaleDoFunc) (ScaleParams *, guint, guint);

static const gchar * const smol_pixel_type_names [SMOL_PIXEL_MAX] =
{
    /* Premultiplied */
    "RGBA8",
    "BGRA8",
    "ARGB8",
    "ABGR8",

    /* Unassociated */
    "rgbA8",
    "bgrA8",
    "Argb8",
    "Abgr8",

    /* No alpha */
    "rgb8",
    "bgr8"
};

static gpointer G_GNUC_UNUSED
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

static gpointer
gen_random_canvas (guint width, guint height)
{
    guint8 *canvas;
    guint8 *canvas_end;
    guint8 *p;

    canvas = g_malloc (width * height * sizeof (guint32));
    canvas_end = canvas + width * height;

    for (p = canvas; p < canvas_end; )
    {
        ssize_t r = getrandom (p, canvas_end - p, 0);
        if (r < 0)
            continue;
        p += r;
    }

    return canvas;
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

#if 0

static void
premultiply_alpha (guint32 *pixels, guint width, guint height)
{
    guint i;

    for (i = 0; i < width * height; i++)
    {
        guint32 p = pixels [i];
        guint32 ch [4];

        ch [0] = (p >> 24) & 0xff;
        ch [1] = (p >> 16) & 0xff;
        ch [2] = (p >> 8) & 0xff;
        ch [3] = p & 0xff;

        ch [1] = (ch [1] * ch [0] * ((65536 * 256 + 255) / 255)) >> 24; /* * 65794 */
        ch [2] = (ch [2] * ch [0] * ((65536 * 256 + 255) / 255)) >> 24; /* * 65794 */
        ch [3] = (ch [3] * ch [0] * ((65536 * 256 + 255) / 255)) >> 24; /* * 65794 */

        pixels [i] = (ch [0] << 24) | (ch [1] << 16) | (ch [2] << 8) | (ch [3]);
    }
}

static guint8
unpremultiply_channel (guint8 x, guint8 a)
{
    if (a == 0)
        return 0;

    if (x > a)
        x = a;

    return ((guint32) x * 255 + (a / 2)) / a;
}

static void
unpremultiply_alpha (guint32 *pixels, guint width, guint height)
{
    guint i;

    for (i = 0; i < width * height; i++)
    {
        guint32 p = pixels [i];
        guint32 ch [4];

        ch [0] = (p >> 24) & 0xff;
        ch [1] = (p >> 16) & 0xff;
        ch [2] = (p >> 8) & 0xff;
        ch [3] = p & 0xff;

        ch [1] = unpremultiply_channel (ch [1], ch [0]);
        ch [2] = unpremultiply_channel (ch [2], ch [0]);
        ch [3] = unpremultiply_channel (ch [3], ch [0]);

        pixels [i] = (ch [0] << 24) | (ch [1] << 16) | (ch [2] << 8) | (ch [3]);
    }
}

#endif

/* --- Benchmarking support --- */

typedef struct
{
    gint width_in, height_in;
    gint width_out, height_out;
    gdouble elapsed_s;
}
Sample;

typedef struct
{
    GArray *samples;
}
Benchmark;

static gdouble
sample_get_pps (const Sample *sample)
{
    return (sample->width_out * (gdouble) sample->height_out
            + sample->width_in * (gdouble) sample->height_in) / sample->elapsed_s;
}

static Benchmark *
benchmark_new (void)
{
    Benchmark *benchmark;

    benchmark = g_new0 (Benchmark, 1);
    benchmark->samples = g_array_new (FALSE, FALSE, sizeof (Sample));

    return benchmark;
}

static void
benchmark_destroy (Benchmark *benchmark)
{
    g_array_free (benchmark->samples, TRUE);
}

static void
benchmark_add_sample (Benchmark *benchmark,
                      gint width_in, gint height_in,
                      gint width_out, gint height_out,
                      gdouble elapsed_s)
{
    Sample m;

    m.width_in = width_in;
    m.height_in = height_in;
    m.width_out = width_out;
    m.height_out = height_out;
    m.elapsed_s = elapsed_s;

    g_array_append_val (benchmark->samples, m);
}

static gint
samples_cmp (gconstpointer a, gconstpointer b)
{
    const Sample *ba = a, *bb = b;
    gint64 diff_i;

    diff_i = ba->width_in * ba->height_in
        - (gint64) bb->width_in * bb->height_in;
    if (diff_i != 0)
        return diff_i;

    diff_i = ba->width_out * ba->height_out
        - (gint64) bb->width_out * bb->height_out;
    if (diff_i != 0)
        return diff_i;

    if (ba->elapsed_s < bb->elapsed_s)
        return -1;
    else if (ba->elapsed_s > bb->elapsed_s)
        return 1;

    return 0;
}

static gint
samples_cmp_params (gconstpointer a, gconstpointer b)
{
    const Sample *ba = a, *bb = b;
    gint64 diff_i;

    diff_i = ba->width_in * ba->height_in
        - (gint64) bb->width_in * bb->height_in;
    if (diff_i != 0)
        return diff_i;

    diff_i = ba->width_out * ba->height_out
        - (gint64) bb->width_out * bb->height_out;
    if (diff_i != 0)
        return diff_i;

    return 0;
}

static gint
samples_cmp_pps (gconstpointer a, gconstpointer b)
{
    gdouble pps_a, pps_b;

    pps_a = sample_get_pps (a);
    pps_b = sample_get_pps (b);

    if (pps_a > pps_b)
        return -1;
    else if (pps_a < pps_b)
        return 1;

    return 0;
}

static void
benchmark_postprocess (Benchmark *benchmark)
{
    GArray *samples_new;
    guint i;

    g_array_sort (benchmark->samples, samples_cmp);
    samples_new = g_array_new (FALSE, FALSE, sizeof (Sample));

    for (i = 0; i < benchmark->samples->len; i++)
    {
        const Sample *m = &g_array_index (benchmark->samples, Sample, i);

        if (i == 0 || samples_cmp_params (m, &g_array_index (benchmark->samples, Sample, i - 1)) != 0)
        {
            g_array_append_val (samples_new, *m);
        }
    }

    g_array_free (benchmark->samples, TRUE);
    benchmark->samples = samples_new;
}

static void
benchmark_print_samples (Benchmark *benchmark, FILE *f)
{
    guint i;

    for (i = 0; i < benchmark->samples->len; i++)
    {
        const Sample *m = &g_array_index (benchmark->samples, Sample, i);

        g_fprintf (f, "%u %u %.6lf %.1lf\n",
                   m->width_out, m->height_out, m->elapsed_s,
                   sample_get_pps (m));
    }
}

static void
benchmark_print_average (Benchmark *benchmark, FILE *f)
{
    GArray *samples_by_pps;
    gdouble sum = 0.0;
    gdouble ntiles [2];
    guint i;

    samples_by_pps = g_array_copy (benchmark->samples);
    g_array_sort (samples_by_pps, samples_cmp_pps);

    for (i = 0; i < samples_by_pps->len; i++)
    {
        const Sample *m = &g_array_index (samples_by_pps, Sample, i);

        sum += sample_get_pps (m);
    }

    ntiles [0] = sample_get_pps (&g_array_index (samples_by_pps, const Sample,
                                                 (gint) (samples_by_pps->len * 0.05)));
    ntiles [1] = sample_get_pps (&g_array_index (samples_by_pps, const Sample,
                                                 (gint) (samples_by_pps->len * 0.95)));

    g_fprintf (f, "%.1lf %.1lf %.1lf\n", sum / (gdouble) samples_by_pps->len, ntiles [0], ntiles [1]);
    g_array_free (samples_by_pps, TRUE);
}

/* --- Conversion benchmarks --- */

typedef struct
{
    Benchmark *bm [SMOL_PIXEL_MAX] [SMOL_PIXEL_MAX];
}
ConvBenchmark;

static ConvBenchmark *
conv_benchmark_new (void)
{
    ConvBenchmark *conv_bm;
    gint i, j;

    conv_bm = g_new0 (ConvBenchmark, 1);

    for (i = 0; i < SMOL_PIXEL_MAX; i++)
    {
        for (j = 0; j < SMOL_PIXEL_MAX; j++)
        {
            conv_bm->bm [i] [j] = benchmark_new ();
        }
    }

    return conv_bm;
}

static void
conv_benchmark_destroy (ConvBenchmark *conv_bm)
{
    gint i, j;

    for (i = 0; i < SMOL_PIXEL_MAX; i++)
    {
        for (j = 0; j < SMOL_PIXEL_MAX; j++)
        {
            benchmark_destroy (conv_bm->bm [i] [j]);
        }
    }

    g_free (conv_bm);
}

static void
conv_benchmark_add_sample (ConvBenchmark *conv_bm,
                           SmolPixelType ptype_in, SmolPixelType ptype_out,
                           gint width_in, gint height_in,
                           gint width_out, gint height_out,
                           gdouble elapsed_s)
{
    benchmark_add_sample (conv_bm->bm [ptype_in] [ptype_out],
                          width_in, height_in,
                          width_out, height_out,
                          elapsed_s);
}

static void
conv_benchmark_postprocess (ConvBenchmark *conv_bm)
{
    gint i, j;

    for (i = 0; i < SMOL_PIXEL_MAX; i++)
    {
        for (j = 0; j < SMOL_PIXEL_MAX; j++)
            benchmark_postprocess (conv_bm->bm [i] [j]);
    }
}

static void
conv_benchmark_print (ConvBenchmark *conv_bm, FILE *f)
{
    gint i, j;

    fprintf (f, "Repack  ");

    for (j = 0; j < SMOL_PIXEL_MAX; j++)
    {
        fprintf (f, "%7.7s ", smol_pixel_type_names [j]);
    }

    fputc ('\n', f);

    for (i = 0; i < SMOL_PIXEL_MAX; i++)
    {
        fprintf (f, "%7.7s ", smol_pixel_type_names [i]);

        for (j = 0; j < SMOL_PIXEL_MAX; j++)
        {
            const Sample *sample;

            sample = &g_array_index (conv_bm->bm [i] [j]->samples, Sample, 0);
            fprintf (f, "%1.5lf ", sample->elapsed_s);
        }

        fputc ('\n', f);
    }
}

/* --- Pixman --- */

static void
scale_init_pixman (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = pixman_image_create_bits (PIXMAN_a8r8g8b8,
                                                in_width, in_height,
                                                (void *) in_raw,
                                                in_width * sizeof (guint32));
}

static void
scale_fini_pixman (ScaleParams *params)
{
    pixman_image_unref (params->in_data);
    free (params->out_data);
    pixman_image_unref (params->priv);
}

static void
scale_do_pixman (ScaleParams *params, guint out_width, guint out_height)
{
    pixman_image_t *pixman_image = params->in_data;
    pixman_f_transform_t ftransform;
    pixman_transform_t transform;
    double fscale_x, fscale_y;
    pixman_fixed_t *pixman_params;
    int n_params;
    guint32 *pixels;
    pixman_image_t *tmp;

    if (params->out_data)
        free (params->out_data);

    if (params->priv)
        pixman_image_unref (params->priv);

    /* Setup */

    pixman_f_transform_init_identity (&ftransform);

    fscale_x = (double) params->in_width / (double) out_width;
    fscale_y = (double) params->in_height / (double) out_height;

    pixman_f_transform_scale (&ftransform, NULL, fscale_x, fscale_y);
    pixman_transform_from_pixman_f_transform (&transform, &ftransform);

    pixman_image_set_transform (pixman_image, &transform);

    pixman_params = pixman_filter_create_separable_convolution (
        &n_params,
        pixman_double_to_fixed (fscale_x),
        pixman_double_to_fixed (fscale_y),
        PIXMAN_KERNEL_IMPULSE,
        PIXMAN_KERNEL_IMPULSE,
        PIXMAN_KERNEL_BOX,
        PIXMAN_KERNEL_BOX,
        1,
        1);

    if (fscale_x > 2.0 || fscale_y > 2.0)
    {
        pixman_image_set_filter (pixman_image, PIXMAN_FILTER_SEPARABLE_CONVOLUTION, pixman_params, n_params);
    }
    else
    {
        pixman_image_set_filter (pixman_image, PIXMAN_FILTER_BILINEAR, NULL, 0);
    }

    pixman_image_set_repeat (pixman_image, PIXMAN_REPEAT_NONE);
    
    free (pixman_params);

    /* Scale */

    pixels = calloc (1, out_width * out_height * sizeof (guint32));
    tmp = pixman_image_create_bits (PIXMAN_a8r8g8b8,
                                    out_width, out_height,
                                    pixels,
                                    out_width * 4);

    pixman_image_composite (PIXMAN_OP_SRC,
                            pixman_image, NULL, tmp,
                            0, 0, 0, 0, 0, 0,
                            out_width, out_height);

    params->priv = tmp;
    params->out_data = pixels;
}

/* --- GDK-Pixbuf --- */

static void
scale_init_gdk_pixbuf (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_data = gdk_pixbuf_new_from_data (in_raw, GDK_COLORSPACE_RGB, TRUE,
                                                8, in_width, in_height, in_width * sizeof (guint32),
                                                NULL, NULL);
}

static void
scale_fini_gdk_pixbuf (ScaleParams *params)
{
    g_object_unref (params->in_data);

    if (params->priv)
    {
        g_object_unref (params->priv);
        params->priv = NULL;
    }
}

static void
scale_do_gdk_pixbuf (ScaleParams *params, guint out_width, guint out_height)
{
    if (params->priv)
    {
        g_object_unref (params->priv);
        params->priv = NULL;
    }

    params->priv = gdk_pixbuf_scale_simple (params->in_data, out_width, out_height,
                                            GDK_INTERP_BILINEAR);
    params->out_data = gdk_pixbuf_get_pixels (params->priv);
}

/* --- SDL --- */

static void
scale_init_sdl (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    SDL_Surface *surface;

#if 0
    /* No need to initialize SDL subsystems when we're just scaling images
     * on the CPU */
    if (!sdl_is_initialized)
    {
        SDL_Init (SDL_INIT_VIDEO);
        sdl_is_initialized = TRUE;
    }
#endif

    params->in_width = in_width;
    params->in_height = in_height;

    surface = SDL_CreateRGBSurfaceFrom ((void *) in_raw,
                                        in_width,
                                        in_height,
                                        32,
                                        in_width * sizeof (guint32),
                                        0x000000ff,
                                        0x0000ff00,
                                        0x00ff0000,
                                        0xff000000);
    params->in_data = surface;
}

static void
scale_fini_sdl (ScaleParams *params)
{
    if (params->in_data)
    {
        SDL_FreeSurface (params->in_data);
        params->in_data = NULL;
    }

    if (params->priv)
    {
        SDL_FreeSurface (params->priv);
        params->priv = NULL;
    }
}

static void
scale_do_sdl (ScaleParams *params, guint out_width, guint out_height)
{
    SDL_Surface *scaled_surface [2];
    guint x_shrink_factor, y_shrink_factor;

    if (params->priv)
    {
        SDL_FreeSurface (params->priv);
        params->priv = NULL;
    }

    /* Find intermediate size and integer divisors for shrinkSurface */

    x_shrink_factor = params->in_width / out_width;
    y_shrink_factor = params->in_height / out_height;

    if (x_shrink_factor < 2)
        x_shrink_factor = 1;
    else
        out_width *= x_shrink_factor;

    if (y_shrink_factor < 2)
        y_shrink_factor = 1;
    else
        out_height *= y_shrink_factor;

    /* NOTE: zoomSurface() seems to fail with dimensions greater than 16384 */

    scaled_surface [0] = scaled_surface [1] =
        zoomSurface (params->in_data,
                     out_width / (gdouble) params->in_width,
                     out_height / (gdouble) params->in_height,
                     SMOOTHING_ON);

    if (!scaled_surface [0])
    {
        g_printerr ("\n  zoomSurface failed: %ux%u -> %ux%u\n",
                    params->in_width, params->in_height,
                    out_width, out_height);
        return;
    }

    if (x_shrink_factor > 1 || y_shrink_factor > 1)
    {
        scaled_surface [1] = shrinkSurface (scaled_surface [0], x_shrink_factor, y_shrink_factor);
        SDL_FreeSurface (scaled_surface [0]);

        if (!scaled_surface [1])
        {
            g_printerr ("\n  shrinkSurface failed: %ux%u -> %ux%u, shrink factors %u/%u\n",
                        params->in_width, params->in_height,
                        out_width, out_height,
                        x_shrink_factor, y_shrink_factor);
            return;
        }
    }

    params->priv = scaled_surface [1];
    params->out_data = scaled_surface [1]->pixels;
}

/* --- Skia --- */

#ifdef WITH_SKIA

/* Defined in skia.c */
void skia_scale_raw (const uint32_t *in_raw, int in_width, int in_height,
                     uint32_t *out_raw, int out_width, int out_height);

static void
scale_init_skia (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = (gpointer) in_raw;
}

static void
scale_fini_skia (G_GNUC_UNUSED ScaleParams *params)
{
    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);
}

static void
scale_do_skia (ScaleParams *params, guint out_width, guint out_height)
{
    gpointer scaled;

    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);

    scaled = g_new (guint32, out_width * out_height);
    skia_scale_raw (params->in_data,
                    params->in_width, params->in_height,
                    scaled,
                    out_width, out_height);

    params->out_data = scaled;
}

#endif

/* --- Smolscale --- */

static void
scale_init_smol (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = (gpointer) in_raw;
}

static void
scale_fini_smol (G_GNUC_UNUSED ScaleParams *params)
{
    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);
}

static void
scale_do_smol (ScaleParams *params, guint out_width, guint out_height)
{
    gpointer scaled;

    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);

    scaled = g_new (guint32, out_width * out_height);
    smol_scale_simple (params->in_data,
                       smol_ptype_in,
                       params->in_width, params->in_height,
                       params->in_width * sizeof (guint32),
                       scaled,
                       smol_ptype_out,
                       out_width, out_height,
                       out_width * sizeof (guint32),
                       FALSE);

    params->out_data = scaled;
}

/* --- Smolscale, threaded --- */

static void
scale_init_smol_threaded (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = (gpointer) in_raw;
}

static void
scale_fini_smol_threaded (G_GNUC_UNUSED ScaleParams *params)
{
    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);
}

static void
scale_smol_thread_worker (gpointer data, SmolScaleCtx *scale_ctx)
{
    guint32 first_row, n_rows;

    first_row = GPOINTER_TO_UINT (data) >> 16;
    n_rows = GPOINTER_TO_UINT (data) & 0xffff;
    smol_scale_batch (scale_ctx, first_row, n_rows);
}

static void
scale_do_smol_threaded (ScaleParams *params, guint out_width, guint out_height)
{
    SmolScaleCtx *scale_ctx;
    GThreadPool *thread_pool;
    gpointer scaled;
    guint32 n_threads;
    guint32 batch_n_rows;
    guint32 i;

    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);

    scaled = g_new (guint32, out_width * out_height);

    scale_ctx = smol_scale_new (params->in_data,
                                smol_ptype_in,
                                params->in_width, params->in_height, params->in_width * sizeof (guint32),
                                scaled,
                                smol_ptype_out,
                                out_width, out_height, out_width * sizeof (guint32),
                                FALSE);

    n_threads = g_get_num_processors ();
    thread_pool = g_thread_pool_new ((GFunc) scale_smol_thread_worker,
                                     scale_ctx,
                                     n_threads,
                                     FALSE,
                                     NULL);

    batch_n_rows = (out_height + n_threads - 1) / n_threads;

    for (i = 0; i < out_height; )
    {
        uint32_t n = MIN (batch_n_rows, out_height - i);
        g_thread_pool_push (thread_pool, GUINT_TO_POINTER ((i << 16) | n), NULL);
        i += n;
    }

    g_thread_pool_free (thread_pool, FALSE, TRUE);
    smol_scale_destroy (scale_ctx);

    params->out_data = scaled;
}

/* --- libswscale --- */

static void
scale_init_libswscale (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = (gpointer) in_raw;
}

static void
scale_fini_libswscale (G_GNUC_UNUSED ScaleParams *params)
{
    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);
}

static void
scale_do_libswscale (ScaleParams *params, guint out_width, guint out_height)
{
    struct SwsContext *ctx;
    const uint8_t *src_planes;
    uint8_t *dest_planes;
    int src_stride, dest_stride;
    gdouble fscale_x, fscale_y;
    int flags;
    gpointer scaled;

    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);

    scaled = g_new (guint32, out_width * out_height);

    src_planes = params->in_data;
    src_stride = params->in_width * sizeof (guint32);
    dest_planes = scaled;
    dest_stride = out_width * sizeof (guint32);

    fscale_x = (double) params->in_width / (double) out_width;
    fscale_y = (double) params->in_height / (double) out_height;
    flags = (fscale_x <= 2.0 || fscale_y <= 2.0) ? SWS_FAST_BILINEAR : SWS_AREA;

    ctx = sws_getContext (params->in_width, params->in_height, PIXEL_TYPE_LIBSWSCALE,
                          out_width, out_height, PIXEL_TYPE_LIBSWSCALE,
                          flags,
                          NULL, NULL, NULL);

    sws_scale (ctx, &src_planes, &src_stride, 0, params->in_height,
               &dest_planes, &dest_stride);

    sws_freeContext (ctx);

    params->out_data = scaled;
}

/* --- stb --- */

static void
scale_init_stb (ScaleParams *params, gconstpointer in_raw, guint in_width, guint in_height)
{
    params->in_width = in_width;
    params->in_height = in_height;
    params->in_data = (gpointer) in_raw;
}

static void
scale_fini_stb (G_GNUC_UNUSED ScaleParams *params)
{
    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);
}

static void
scale_do_stb (ScaleParams *params, guint out_width, guint out_height)
{
    gpointer scaled;

    if (params->priv)
        g_free (params->priv);
    if (params->out_data)
        g_free (params->out_data);

    scaled = g_new (guint32, out_width * out_height);

#if 0
    stbir_resize_uint8 (params->in_data, params->in_width, params->in_height, 0,
                        scaled, out_width, out_height, 0, 4);
#else
    stbir_resize_uint8_generic (params->in_data, params->in_width, params->in_height, 0,
                                scaled, out_width, out_height, 0, 4, 0,
                                STBIR_FLAG_ALPHA_PREMULTIPLIED,
                                STBIR_EDGE_ZERO,
                                STBIR_FILTER_BOX,
                                STBIR_COLORSPACE_LINEAR,
                                NULL);
#endif

    params->out_data = scaled;
}

/* --- Benchmarks --- */

static void
run_benchmark_proportional (gpointer raw_data,
                            guint n_repetitions,
                            guint in_width, guint in_height,
                            guint out_width_min, guint out_width_max,
                            guint out_height_min, guint out_height_max,
                            guint n_steps,
                            ScaleInitFunc init_func,
                            ScaleFiniFunc fini_func,
                            ScaleDoFunc do_func,
                            Benchmark *benchmark)
{
    gfloat width_step_size, height_step_size;
    guint step;
    guint rep;
    ScaleParams params = { 0 };

    if (n_steps > 1)
    {
        width_step_size = (out_width_max - out_width_min) / ((gfloat) n_steps - 1.0);
        height_step_size = (out_height_max - out_height_min) / ((gfloat) n_steps - 1.0);
    }
    else
    {
        width_step_size = 99999.0;
        height_step_size = 99999.0;
    }

    (*init_func) (&params, raw_data, in_width, in_height);

    for (rep = 0; rep < n_repetitions; rep++)
    {
        for (step = 0; step < n_steps; step++)
        {
            struct timespec before, after;
            guint out_width, out_height;

            out_width = CLAMP (out_width_min + step * width_step_size, 1, 65535);
            out_height = CLAMP (out_height_min + step * height_step_size, 1, 65535);

            clock_gettime (CLOCK_MONOTONIC_RAW, &before);
            (*do_func) (&params, out_width, out_height);
            clock_gettime (CLOCK_MONOTONIC_RAW, &after);

            benchmark_add_sample (benchmark,
                                  in_width, in_height,
                                  out_width, out_height,
                                  compute_elapsed (&before, &after));
        }

        g_printerr ("*");
        fflush (stderr);
    }

    g_printerr ("\n");
    fflush (stderr);

    (*fini_func) (&params);

    benchmark_postprocess (benchmark);
}

static void
run_benchmark_conv (gpointer raw_data,
                    guint in_width, guint in_height,
                    ScaleInitFunc init_func,
                    ScaleFiniFunc fini_func,
                    ScaleDoFunc do_func,
                    ConvBenchmark *conv_bm)
{
    ScaleParams params = { 0 };
    gint n_repetitions = 20;
    gint rep;
    SmolPixelType ptype_in, ptype_out;

    (*init_func) (&params, raw_data, in_width, in_height);

    for (rep = 0; rep < n_repetitions; rep++)
    {
        for (ptype_in = 0; ptype_in < SMOL_PIXEL_MAX; ptype_in++)
        {
            for (ptype_out = 0; ptype_out < SMOL_PIXEL_MAX; ptype_out++)
            {
                struct timespec before, after;
                guint out_width, out_height;

                out_width = in_width - 1;
                out_height = in_height - 1;

                smol_ptype_in = ptype_in;
                smol_ptype_out = ptype_out;

                clock_gettime (CLOCK_MONOTONIC_RAW, &before);
                (*do_func) (&params, out_width, out_height);
                clock_gettime (CLOCK_MONOTONIC_RAW, &after);

                conv_benchmark_add_sample (conv_bm,
                                           ptype_in, ptype_out,
                                           in_width, in_height,
                                           out_width, out_height,
                                           compute_elapsed (&before, &after));
            }
        }

        g_printerr ("*");
        fflush (stderr);
    }

    g_printerr ("\n");
    fflush (stderr);

    (*fini_func) (&params);

    conv_benchmark_postprocess (conv_bm);
}

static void
remove_extension (gchar *filename)
{
    gchar *p0 = strrchr (filename, '.');
    if (!p0)
        return;

    *p0 = '\0';
}

/* --- Correctness check --- */

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
check_all_levels (const guint32 * const *canvas_array,
                  ScaleInitFunc init_func,
                  ScaleFiniFunc fini_func,
                  ScaleDoFunc do_func,
                  guint32 width_in, guint32 height_in,
                  guint32 width_out, guint32 height_out,
                  Dimension dim)
{
    ScaleParams params = { 0 };
    guint c;

    for (c = 0; c < 256 / 4; c++)
    {
        (*init_func) (&params, canvas_array [c], width_in, height_in);
        (*do_func) (&params, width_out, height_out);
        check_color_canvas (canvas_array [c],
                            width_in, height_in,
                            params.out_data,
                            width_out, height_out,
                            dim);
        (*fini_func) (&params);
        memset (&params, 0, sizeof (params));
    }
}

static void
run_check (ScaleInitFunc init_func,
           ScaleFiniFunc fini_func,
           ScaleDoFunc do_func)
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

    /* Downscale to minimum width and height; we do this first as it's
     * more likely to reveal issues early on */

    for (i = 1; i < 65536; i++)
    {
        g_printerr ("Width %u -> %u:        \r", i, 1);
        check_all_levels ((const guint32 * const *) canvas_array,
                          init_func,
                          fini_func,
                          do_func,
                          i, 1, 1, 1,
                          DIM_HORIZONTAL);
    }

    for (i = 1; i < 65536; i++)
    {
        g_printerr ("Height %u -> %u:        \r", i, 1);
        check_all_levels ((const guint32 * const *) canvas_array,
                          init_func,
                          fini_func,
                          do_func,
                          1, i, 1, 1,
                          DIM_VERTICAL);
    }

    /* Downscale from maximum width and height */

    for (i = 1; i < 65536; i++)
    {
        g_printerr ("Width %u -> %u:        \r", 65535, i);
        check_all_levels ((const guint32 * const *) canvas_array,
                          init_func,
                          fini_func,
                          do_func,
                          65535, 1, i, 1,
                          DIM_HORIZONTAL);
    }

    for (i = 1; i < 65536; i++)
    {
        g_printerr ("Height %u -> %u:        \r", 65535, i);
        check_all_levels ((const guint32 * const *) canvas_array,
                          init_func,
                          fini_func,
                          do_func,
                          1, 65535, 1, i,
                          DIM_VERTICAL);
    }

    /* Long test */

    i = CORRECTNESS_WIDTH_MIN;
    for (;;)
    {
        for (j = 1; j < MIN (i + 1, 65536); j++)
        {
            g_printerr ("Width %u -> %u:        \r", i, j);
            check_all_levels ((const guint32 * const *) canvas_array,
                              init_func,
                              fini_func,
                              do_func,
                              i, 1, j, 1,
                              DIM_HORIZONTAL);
        }

        for (j = 1; j < MIN (i + 1, 65536); j++)
        {
            g_printerr ("Height %u -> %u:        \r", i, j);
            check_all_levels ((const guint32 * const *) canvas_array,
                              init_func,
                              fini_func,
                              do_func,
                              1, i, 1, j,
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

/* --- Image generation --- */

static void
run_generate (const gchar *filename,
              gdouble scale_min,
              gdouble scale_max,
              guint n_steps,
              ScaleInitFunc init_func,
              ScaleFiniFunc fini_func,
              ScaleDoFunc do_func)
{
    gpointer raw_data;
    guint in_width, in_height;
    guint out_width_min, out_width_max;
    guint out_height_min, out_height_max;
    gfloat width_step_size, height_step_size;
    ScaleParams params = { 0 };
    gchar *fname_out_prefix;
    guint step;

    if (!smoltest_load_image (filename, &in_width, &in_height, &raw_data))
    {
        g_printerr ("Failed to read image '%s'.\n", filename);
        return;
    }

#if 0
    premultiply_alpha (raw_data, in_width, in_height);
#endif

    out_width_min = CLAMP (in_width * scale_min, 1, 65535);
    out_width_max = CLAMP (in_width * scale_max, 1, 65535);
    out_height_min = CLAMP (in_height * scale_min, 1, 65535);
    out_height_max = CLAMP (in_height * scale_max, 1, 65535);

    if (n_steps > 1)
    {
        width_step_size = (out_width_max - out_width_min) / ((gfloat) n_steps - 1.0);
        height_step_size = (out_height_max - out_height_min) / ((gfloat) n_steps - 1.0);
    }
    else
    {
        width_step_size = 99999.0;
        height_step_size = 99999.0;
    }

    fname_out_prefix = g_strdup (filename);
    remove_extension (fname_out_prefix);

    (*init_func) (&params, raw_data, in_width, in_height);

    for (step = 0; step < n_steps; step++)
    {
        guint out_width, out_height;

        out_width = CLAMP (out_width_min + step * width_step_size, 1, 65535);
        out_height = CLAMP (out_height_min + step * height_step_size, 1, 65535);

        (*do_func) (&params, out_width, out_height);

#if 0
        unpremultiply_alpha (params.out_data, out_width, out_height);
#endif

        smoltest_save_image (fname_out_prefix, params.out_data, out_width, out_height);

        g_printerr ("*");
        fflush (stderr);
    }

    (*fini_func) (&params);
}

/* --- Main --- */

static void
print_usage (void)
{
    g_printerr ("Usage: test <smol|libswscale|pixman|gdk_pixbuf|sdl|skia|stb>\n"
                "            [ check ] |\n"
                "            [ generate\n"
                "              <min_scale> <max_scale> <n_steps>\n"
                "              <filename> ] |\n"
                "            [ benchmark\n"
                "              <n_repetitions>\n"
                "              <in_width> <in_height>\n"
                "              <min_scale> <max_scale> <n_steps>\n"
                "              [ <averages log file>\n"
                "                [ <samples log file> ] ] ] |\n"
                "            [ benchmark-conv ]\n");
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

#define OUTPUT_FILE_MAX 16

int
main (int argc, char *argv [])
{
    guint n_repetitions;
    guint in_width, in_height;
    gdouble scale_min = 1.0, scale_max = 1.0;
    guint scale_steps = 1;
    ScaleInitFunc init_func;
    ScaleFiniFunc fini_func;
    ScaleDoFunc do_func;
    ScaleOperation scale_op = SCALE_OP_CHECK;
    gchar *filename = NULL;
    const gchar *output_fname [OUTPUT_FILE_MAX] = { NULL };
    FILE *output_file [OUTPUT_FILE_MAX] = { NULL };
    gint i;

    if (argc < 2)
    {
        print_usage ();
        return 1;
    }

    n_repetitions = DEFAULT_N_REPETITIONS;
    in_width = DEFAULT_IN_WIDTH;
    in_height = DEFAULT_IN_HEIGHT;

    if (!strcasecmp (argv [1], "smol"))
    {
        init_func = scale_init_smol;
        fini_func = scale_fini_smol;
        do_func = scale_do_smol;
    }
    else if (!strcasecmp (argv [1], "smol-mt"))
    {
        init_func = scale_init_smol_threaded;
        fini_func = scale_fini_smol_threaded;
        do_func = scale_do_smol_threaded;
    }
    else if (!strcasecmp (argv [1], "pixman"))
    {
        init_func = scale_init_pixman;
        fini_func = scale_fini_pixman;
        do_func = scale_do_pixman;
    }
    else if (!strcasecmp (argv [1], "gdk_pixbuf"))
    {
        init_func = scale_init_gdk_pixbuf;
        fini_func = scale_fini_gdk_pixbuf;
        do_func = scale_do_gdk_pixbuf;
    }
    else if (!strcasecmp (argv [1], "sdl"))
    {
        init_func = scale_init_sdl;
        fini_func = scale_fini_sdl;
        do_func = scale_do_sdl;
    }
    else if (!strcasecmp (argv [1], "libswscale"))
    {
        init_func = scale_init_libswscale;
        fini_func = scale_fini_libswscale;
        do_func = scale_do_libswscale;
    }
    else if (!strcasecmp (argv [1], "skia"))
    {
#ifdef WITH_SKIA
        init_func = scale_init_skia;
        fini_func = scale_fini_skia;
        do_func = scale_do_skia;
#else
        g_printerr ("No Skia support built in; see Makefile.\n");
        return 1;
#endif
    }
    else if (!strcasecmp (argv [1], "stb"))
    {
        init_func = scale_init_stb;
        fini_func = scale_fini_stb;
        do_func = scale_do_stb;
    }
    else
    {
        g_printerr ("Unknown scaler module: %s\n", argv [1]);
        print_usage ();
        return 1;
    }

    if (argc > 2)
    {
        if (!strcasecmp (argv [2], "benchmark"))
            scale_op = SCALE_OP_BENCHMARK_PROP;
        else if (!strcasecmp (argv [2], "benchmark-conv"))
            scale_op = SCALE_OP_BENCHMARK_CONV;
        else if (!strcasecmp (argv [2], "generate"))
            scale_op = SCALE_OP_GENERATE;
        else if (!strcasecmp (argv [2], "check"))
            scale_op = SCALE_OP_CHECK;
    }

    if (scale_op == SCALE_OP_BENCHMARK_PROP)
    {
        if (argc < 9)
        {
            g_printerr ("Missing arguments for benchmarking.");
            print_usage ();
            return 1;
        }

        i = 3;

        n_repetitions = strtoul (argv [i++], NULL, 10);
        in_width = strtoul (argv [i++], NULL, 10);
        in_height = strtoul (argv [i++], NULL, 10);
        scale_min = strtod (argv [i++], NULL);
        scale_max = strtod (argv [i++], NULL);
        scale_steps = strtoul (argv [i++], NULL, 10);
        if (argc >= 10)
            output_fname [0] = argv [i++];
        if (argc >= 11)
            output_fname [1] = argv [i++];
    }
    if (scale_op == SCALE_OP_BENCHMARK_CONV)
    {
        if (strcasecmp (argv [1], "smol")
            && strcasecmp (argv [1], "smol-mt"))
        {
            g_printerr ("Operation benchmark-conv is only defined for modules 'smol' and 'smol-mt'.");
            print_usage ();
            return 1;
        }
    }
    else if (scale_op == SCALE_OP_GENERATE)
    {
        if (argc < 7)
        {
            g_printerr ("Missing arguments for generate.");
            print_usage ();
            return 1;
        }

        i = 3;

        scale_min = strtod (argv [i++], NULL);
        scale_max = strtod (argv [i++], NULL);
        scale_steps = strtoul (argv [i++], NULL, 10);
        filename = g_strdup (argv [i++]);
    }

    for (i = 0; i < OUTPUT_FILE_MAX && output_fname [i]; i++)
    {
        output_file [i] = fopen (output_fname [i], "a");
        if (!output_file [i])
        {
            g_printerr ("Failed to open output file '%s'.\n", output_fname [i]);
            return 1;
        }
    }

    if (scale_op == SCALE_OP_BENCHMARK_PROP)
    {
        gpointer raw_data = gen_random_canvas (in_width, in_height);
        Benchmark *benchmark = benchmark_new ();
        run_benchmark_proportional (raw_data,
                                    n_repetitions,
                                    in_width, in_height,
                                    in_width * scale_min, in_width * scale_max,
                                    in_height * scale_min, in_height * scale_max,
                                    scale_steps,
                                    init_func, fini_func, do_func,
                                    benchmark);
        if (output_file [0])
            benchmark_print_average (benchmark, output_file [0]);
        if (output_file [1])
            benchmark_print_samples (benchmark, output_file [1]);
        benchmark_destroy (benchmark);
        g_free (raw_data);
    }
    else if (scale_op == SCALE_OP_BENCHMARK_CONV)
    {
        gpointer raw_data = gen_random_canvas (BENCHMARK_CONV_WIDTH, BENCHMARK_CONV_HEIGHT);
        ConvBenchmark *conv_bm = conv_benchmark_new ();
        run_benchmark_conv (raw_data,
                            BENCHMARK_CONV_WIDTH, BENCHMARK_CONV_HEIGHT,
                            init_func, fini_func, do_func,
                            conv_bm);
        conv_benchmark_print (conv_bm, stdout);
        conv_benchmark_destroy (conv_bm);
        g_free (raw_data);
    }
    else if (scale_op == SCALE_OP_GENERATE)
    {
        run_generate (filename,
                      scale_min,
                      scale_max,
                      scale_steps,
                      init_func, fini_func, do_func);
    }
    else if (scale_op == SCALE_OP_CHECK)
    {
        run_check (init_func, fini_func, do_func);
    }

    for (i = 0; i < OUTPUT_FILE_MAX && output_file [i]; i++)
    {
        fclose (output_file [i]);
    }

    return 0;
}
