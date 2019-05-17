/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include "scale.h"

#define SMALL_MUL 256U
#define BIG_MUL 65536U
#define BOXES_MULTIPLIER ((guint64) BIG_MUL * SMALL_MUL)
#define FUDGE_FACTOR (SMALL_MUL + SMALL_MUL / 2 + SMALL_MUL / 4)

typedef enum
{
    ALGORITHM_BILINEAR,
    ALGORITHM_BOX_256,
    ALGORITHM_BOX_65536
}
Algorithm;

/* For reusing rows that have already undergone horizontal scaling */
typedef struct
{
    guint in_ofs;
    guint64 *parts_top_row;
    guint64 *parts_bottom_row;
}
VerticalCtx;

typedef struct SmolScaleCtx SmolScaleCtx;

struct SmolScaleCtx
{
    const guint32 *pixels_in;
    guint32 *pixels_out;
    guint width_in, height_in, rowstride_in;
    guint width_out, height_out, rowstride_out;

    Algorithm algo_h, algo_v;

    /* Each offset is split in two guint16s: { pixel index, fraction }. These
     * are relative to the image after halvings have taken place. */
    guint16 *offsets_x, *offsets_y;
    guint span_mul_x, span_mul_y;  /* For box filter */
};

/* --- Pixel and parts manipulation --- */

static const guint32 *
inrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx, guint inrow_ofs)
{
    return scale_ctx->pixels_in + scale_ctx->rowstride_in * inrow_ofs;
}

static guint32 *
outrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx, guint outrow_ofs)
{
    return scale_ctx->pixels_out + scale_ctx->rowstride_out * outrow_ofs;
}

static inline guint32
pack_pixel_256 (guint64 in)
{
    return in | (in >> 24);
}

static inline guint64
unpack_pixel_256 (guint32 p)
{
    return (((guint64) p & 0xff00ff00) << 24) | (p & 0x00ff00ff);
}

static inline guint64
weight_pixel_256 (guint64 p, guint16 w)
{
    return ((p * w) >> 1) & 0x7fff7fff7fff7fff;
}

static inline void
sum_pixels_256 (const guint32 **pp, guint64 *accum, guint n)
{
    const guint32 *pp_end;

    for (pp_end = *pp + n; *pp < pp_end; (*pp)++)
    {
        *accum += unpack_pixel_256 (**pp);
    }
}

static inline void
scale_and_store_256 (guint64 accum, guint64 multiplier, guint64 **row_parts_out)
{
    guint64 a, b;

    /* Average the inputs */
    a = ((accum & 0x0000ffff0000ffffULL) * multiplier
         + (BOXES_MULTIPLIER / 2) + ((BOXES_MULTIPLIER / 2) << 32)) / BOXES_MULTIPLIER;
    b = (((accum & 0xffff0000ffff0000ULL) >> 16) * multiplier
         + (BOXES_MULTIPLIER / 2) + ((BOXES_MULTIPLIER / 2) << 32)) / BOXES_MULTIPLIER;

    /* Store pixel */
    *(*row_parts_out)++ = (a & 0x000000ff000000ffULL) | ((b & 0x000000ff000000ffULL) << 16);
}

static void
convert_parts_256_to_65536 (guint64 *row, guint n)
{
    guint64 *temp;
    guint i, j;

    temp = alloca (n * sizeof (guint64) * 2);

    for (i = 0, j = 0; i < n; )
    {
        temp [j++] = (row [i] >> 16) & 0x000000ff000000ff;
        temp [j++] = row [i++] & 0x000000ff000000ff;
    }

    memcpy (row, temp, n * sizeof (guint64) * 2);
}

static void
convert_parts_65536_to_256 (guint64 *row, guint n)
{
    guint i, j;

    for (i = 0, j = 0; j < n; )
    {
        row [j] = row [i++] << 16;
        row [j++] |= row [i++];
    }
}

/* --- Precalculation --- */

static void
calc_size_steps (guint dim_in, guint dim_out, Algorithm *algo)
{
    guint n_halvings;

    n_halvings = 0;

    while (dim_in >= dim_out * 2)
    {
        dim_out *= 2;
        n_halvings++;
    }

    if (n_halvings == 0)
        *algo = ALGORITHM_BILINEAR;
    else if (n_halvings < 8)
        *algo = ALGORITHM_BOX_256;
    else
        *algo = ALGORITHM_BOX_65536;
}

static void
precalc_bilinear_array (guint16 *array, guint dim_in, guint dim_out, gboolean absolute_offsets)
{
    guint32 ofs_stepF, fracF, frac_stepF;
    guint16 *pu16 = array;
    guint16 last_ofs = 0;

    /* Works when dim_in >= dim_out, 1=1 is perfect */
    frac_stepF = ofs_stepF = ((dim_in - 1) * BIG_MUL + FUDGE_FACTOR) / (dim_out - 1);
    fracF = 0;

    do
    {
        guint16 ofs = fracF / BIG_MUL;

        /* We sample ofs and its neighbor -- prevent out of bounds access
         * for the latter. */
        if (ofs >= dim_in - 1)
            break;

        *(pu16++) = absolute_offsets ? ofs : ofs - last_ofs;
        *(pu16++) = SMALL_MUL - ((fracF / (BIG_MUL / SMALL_MUL)) % SMALL_MUL);
        fracF += frac_stepF;

        last_ofs = ofs;
    }
    while (--dim_out);

    /* Instead of going out of bounds, sample the final pair of pixels with a 100%
     * bias towards the last pixel */
    while (dim_out)
    {
        *(pu16++) = absolute_offsets ? dim_in - 2 : (dim_in - 2) - last_ofs;
        *(pu16++) = 0;
        dim_out--;

        last_ofs = dim_in - 2;
    }
}

static void
precalc_boxes_array (guint16 *array, guint *span_mul, guint dim_in, guint dim_out,
                     gboolean absolute_offsets)
{
    guint64 fracF, frac_stepF;
    guint16 *pu16 = array;
    guint16 ofs, next_ofs;
    guint span_mul_orig;
    guint64 f;
    guint64 stride;

    frac_stepF = ((guint64) dim_in * BIG_MUL) / (guint64) dim_out;
    fracF = 0;
    ofs = 0;

    *span_mul = span_mul_orig = (BOXES_MULTIPLIER * BIG_MUL * SMALL_MUL) / (frac_stepF * SMALL_MUL - BIG_MUL);

    stride = frac_stepF / (guint64) BIG_MUL;
    f = (frac_stepF / SMALL_MUL) % SMALL_MUL;
    while (((((stride * 255) + ((f * 255) / 2) / 128) - 1)
            * (guint64) *span_mul + (BOXES_MULTIPLIER / 4)) / (BOXES_MULTIPLIER) < 255)
        (*span_mul)++;

    do
    {
        fracF += frac_stepF;
        next_ofs = (guint64) fracF / ((guint64) BIG_MUL);

        /* Prevent out of bounds access */
        if (ofs >= dim_in - 1)
            break;

        if (next_ofs > dim_in)
        {
            next_ofs = dim_in;
            if (next_ofs <= ofs)
                break;
        }

        stride = next_ofs - ofs - 1;
        f = (fracF / SMALL_MUL) % SMALL_MUL;

        /* Fraction is the other way around, since left pixel of each span
         * comes first, and it's on the right side of the fractional sample. */
        *(pu16++) = absolute_offsets ? ofs : stride;
        *(pu16++) = f;

        ofs = next_ofs;
    }
    while (--dim_out);

    /* Instead of going out of bounds, sample the final pair of pixels with a 100%
     * bias towards the last pixel */
    while (dim_out)
    {
        *(pu16++) = absolute_offsets ? ofs : 0;
        *(pu16++) = 0;
        dim_out--;
    }

    *(pu16++) = absolute_offsets ? ofs : 0;
    *(pu16++) = 0;
}

static void
interp_horizontal_bilinear (const SmolScaleCtx *scale_ctx, const guint32 *row_in, guint64 *row_parts_out)
{
    const guint32 *pp;
    guint64 p, q;
    const guint16 *ofs_x = scale_ctx->offsets_x;
    guint64 F;
    guint64 *row_parts_out_max = row_parts_out + (scale_ctx->width_out & ~(1ULL));

    pp = row_in;

    /* Unrolling makes us ~2% faster or so */

    do
    {
        pp += *(ofs_x++);
        F = *(ofs_x++);

        p = *pp;
        q = *(pp + 1);
        p = (((p & 0xff00ff00) << 24) | (p & 0x00ff00ff));
        q = (((q & 0xff00ff00) << 24) | (q & 0x00ff00ff));

        p = (((p - q) * F) >> 8) + q;

        *(row_parts_out++) = p & 0x00ff00ff00ff00ff;

        pp += *(ofs_x++);
        F = *(ofs_x++);

        p = *pp;
        q = *(pp + 1);
        p = (((p & 0xff00ff00) << 24) | (p & 0x00ff00ff));
        q = (((q & 0xff00ff00) << 24) | (q & 0x00ff00ff));

        p = (((p - q) * F) >> 8) + q;

        *(row_parts_out++) = p & 0x00ff00ff00ff00ff;
    }
    while (row_parts_out != row_parts_out_max);

    if (scale_ctx->width_out & 1)
    {
        pp += *(ofs_x++);
        F = *(ofs_x++);

        p = *pp;
        q = *(pp + 1);
        p = (((p & 0xff00ff00) << 24) | (p & 0x00ff00ff));
        q = (((q & 0xff00ff00) << 24) | (q & 0x00ff00ff));

        p = (((p - q) * F) >> 8) + q;

        *(row_parts_out++) = p & 0x00ff00ff00ff00ff;
    }
}

static void
interp_horizontal_boxes_256 (const SmolScaleCtx *scale_ctx, const guint32 *row_in, guint64 *row_parts_out)
{
    const guint32 *pp;
    const guint16 *ofs_x = scale_ctx->offsets_x;
    guint64 *row_parts_out_max = row_parts_out + scale_ctx->width_out - 1;
    guint64 accum = 0;
    guint64 p, q, r, s;
    guint n;
    guint64 F;

    pp = row_in;
    p = weight_pixel_256 (unpack_pixel_256 (*(pp++)), 256);
    n = *(ofs_x++);

    while (row_parts_out != row_parts_out_max)
    {
        sum_pixels_256 (&pp, &accum, n);

        F = *(ofs_x++);
        n = *(ofs_x++);

        r = unpack_pixel_256 (*(pp++));
        s = r * F;

        q = (s >> 1) & 0x7fff7fff7fff7fffULL;

        accum += ((p + q) >> 7) & 0x01ff01ff01ff01ffULL;

        p = (((r << 8) - r - s) >> 1) & 0x7fff7fff7fff7fffULL;

        scale_and_store_256 (accum, scale_ctx->span_mul_x, &row_parts_out);
        accum = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_pixels_256 (&pp, &accum, n);

    q = 0;
    F = *(ofs_x);
    if (F > 0)
        q = weight_pixel_256 (unpack_pixel_256 (*(pp)), F);

    accum += ((p + q) >> 7) & 0x01ff01ff01ff01ffULL;
    scale_and_store_256 (accum, scale_ctx->span_mul_x, &row_parts_out);
}

static inline void
unpack_pixel_65536 (guint32 p, guint64 *out)
{
    out [0] = (((guint64) p & 0xff000000) << 8) | (((guint64) p & 0x00ff0000) >> 16);
    out [1] = (((guint64) p & 0x0000ff00) << 24) | (p & 0x000000ff);
}

static inline guint32
pack_pixel_65536 (guint64 *in)
{
    /* FIXME: Are masks needed? */
    return ((in [0] >> 8) & 0xff000000)
           | ((in [0] << 16) & 0x00ff0000)
           | ((in [1] >> 24) & 0x0000ff00)
           | (in [1] & 0x000000ff);
}

static inline void
weight_pixel_65536 (guint64 *p, guint64 *out, guint16 w)
{
    out [0] = ((p [0] * w) >> 1) & 0x7fffffff7fffffffULL;
    out [1] = ((p [1] * w) >> 1) & 0x7fffffff7fffffffULL;
}

static inline void
sum_pixels_65536 (const guint32 **pp, guint64 *accum, guint n)
{
    const guint32 *pp_end;

    for (pp_end = *pp + n; *pp < pp_end; (*pp)++)
    {
        guint64 p [2];
        unpack_pixel_65536 (**pp, p);
        accum [0] += p [0];
        accum [1] += p [1];
    }
}

static inline guint64
scale_65536_half (guint64 accum, guint64 multiplier)
{
    guint64 a, b;

    a = accum & 0x00000000ffffffffULL;
    a = (a * multiplier + BOXES_MULTIPLIER / 2) / BOXES_MULTIPLIER;

    b = (accum & 0xffffffff00000000ULL) >> 32;
    b = (b * multiplier + BOXES_MULTIPLIER / 2) / BOXES_MULTIPLIER;

    return (a & 0x00000000000000ffULL)
           | ((b & 0x00000000000000ffULL) << 32);
}

static inline void
scale_and_store_65536 (guint64 *accum, guint64 multiplier, guint64 **row_parts_out)
{
    *(*row_parts_out)++ = scale_65536_half (accum [0], multiplier);
    *(*row_parts_out)++ = scale_65536_half (accum [1], multiplier);
}

static void
interp_horizontal_boxes_65536 (const SmolScaleCtx *scale_ctx, const guint32 *row_in, guint64 *row_parts_out)
{
    const guint32 *pp;
    const guint16 *ofs_x = scale_ctx->offsets_x;
    guint64 *row_parts_out_max = row_parts_out + (scale_ctx->width_out - /* 2 */ 1) * 2;
    guint64 accum [2] = { 0, 0 };
    guint64 p [2], q [2], r [2], s [2];
    guint n;
    guint64 F;

    pp = row_in;
    unpack_pixel_65536 (*(pp++), p);
    weight_pixel_65536 (p, p, 256);
    n = *(ofs_x++);

    while (row_parts_out != row_parts_out_max)
    {
        sum_pixels_65536 (&pp, accum, n);

        F = *(ofs_x++);
        n = *(ofs_x++);

        unpack_pixel_65536 (*(pp++), r);

        s [0] = r [0] * F;
        s [1] = r [1] * F;

        q [0] = (s [0] >> 1) & 0x7fffffff7fffffff;
        q [1] = (s [1] >> 1) & 0x7fffffff7fffffff;

        accum [0] += ((p [0] + q [0]) >> 7) & 0x01ffffff01ffffff;
        accum [1] += ((p [1] + q [1]) >> 7) & 0x01ffffff01ffffff;

        p [0] = (((r [0] << 8) - r [0] - s [0]) >> 1) & 0x7fffffff7fffffff;
        p [1] = (((r [1] << 8) - r [1] - s [1]) >> 1) & 0x7fffffff7fffffff;

        scale_and_store_65536 (accum, scale_ctx->span_mul_x, &row_parts_out);

        accum [0] = 0;
        accum [1] = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_pixels_65536 (&pp, accum, n);

    q [0] = 0;
    q [1] = 0;

    F = *(ofs_x);
    if (F > 0)
    {
        unpack_pixel_65536 (*(pp), q);
        weight_pixel_65536 (q, q, F);
    }

    accum [0] += ((p [0] + q [0]) >> 7) & 0x01ffffff01ffffff;
    accum [1] += ((p [1] + q [1]) >> 7) & 0x01ffffff01ffffff;

    scale_and_store_65536 (accum, scale_ctx->span_mul_x, &row_parts_out);
}

static void
scale_horizontal (const SmolScaleCtx *scale_ctx, const guint32 *row_in, guint64 *row_parts_out)
{
    if (scale_ctx->algo_h == ALGORITHM_BILINEAR)
        interp_horizontal_bilinear (scale_ctx, row_in, row_parts_out);
    else if (scale_ctx->algo_h == ALGORITHM_BOX_256)
        interp_horizontal_boxes_256 (scale_ctx, row_in, row_parts_out);
    else
        interp_horizontal_boxes_65536 (scale_ctx, row_in, row_parts_out);
}

static void
scale_horizontal_for_vertical_256 (const SmolScaleCtx *scale_ctx, 
                                   const guint32 *row_in, guint64 *row_parts_out)
{
    scale_horizontal (scale_ctx, row_in, row_parts_out);
    if (scale_ctx->algo_h == ALGORITHM_BOX_65536)
    {
        convert_parts_65536_to_256 (row_parts_out, scale_ctx->width_out);
    }
}

static void
scale_horizontal_for_vertical_65536 (const SmolScaleCtx *scale_ctx, 
                                     const guint32 *row_in, guint64 *row_parts_out)
{
    scale_horizontal (scale_ctx, row_in, row_parts_out);
    if (scale_ctx->algo_h != ALGORITHM_BOX_65536)
    {
        convert_parts_256_to_65536 (row_parts_out, scale_ctx->width_out);
    }
}

static void
add_parts (const guint64 *parts_in, guint64 *parts_acc_out, guint n)
{
    const guint64 *parts_in_max = parts_in + n;

    while (parts_in < parts_in_max)
        *(parts_acc_out++) += *(parts_in++);
}

static void
interp_vertical_bilinear_256 (guint64 F, const guint64 *top_row_parts_in,
                              const guint64 *bottom_row_parts_in, guint32 *row_out, guint width)
{
    guint32 *row_out_last = row_out + (width & ~(1ULL));

    do
    {
        guint64 p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        p = (((p - q) * F) >> 8) + q;
        p &= 0x00ff00ff00ff00ff;

        *(row_out++) = (guint32) (p | p >> 24);

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        p = (((p - q) * F) >> 8) + q;
        p &= 0x00ff00ff00ff00ff;

        *(row_out++) = (guint32) (p | p >> 24);
    }
    while (row_out != row_out_last);

    if (width & 1)
    {
        guint64 p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        p = (((p - q) * F) >> 8) + q;
        p &= 0x00ff00ff00ff00ff;

        *(row_out++) = (guint32) (p | p >> 24);
    }
}

static inline guint64
scale_256 (guint64 accum, guint64 multiplier)
{
    guint64 a, b;

    /* Average the inputs */
    a = ((accum & 0x0000ffff0000ffffULL) * multiplier
         + (BOXES_MULTIPLIER / 2) + ((BOXES_MULTIPLIER / 2) << 32)) / BOXES_MULTIPLIER;
    b = (((accum & 0xffff0000ffff0000ULL) >> 16) * multiplier
         + (BOXES_MULTIPLIER / 2) + ((BOXES_MULTIPLIER / 2) << 32)) / BOXES_MULTIPLIER;

    /* Return pixel */
    return (a & 0x000000ff000000ffULL) | ((b & 0x000000ff000000ffULL) << 16);
}

static void
finalize_vertical_256 (const guint64 *accums, guint64 multiplier, guint32 *row_out, guint n)
{
    guint32 *row_out_max = row_out + n;

    while (row_out != row_out_max)
    {
        guint64 p;

        p = scale_256 (*(accums++), multiplier);
        *(row_out++) = pack_pixel_256 (p);
    }
}

static void
scale_and_weight_edge_rows_box_256 (const guint64 *first_row, const guint64 *last_row, guint64 *accum, guint16 w1, guint16 w2, guint n)
{
    const guint64 *first_row_max = first_row + n;

    while (first_row != first_row_max)
    {
        guint64 r, s, p, q;

        r = *(first_row++);
        s = r * w1;
        p = (s >> 1) & 0x7fff7fff7fff7fffULL;

        r = *(last_row++);
        s = r * w2;
        q = (s >> 1) & 0x7fff7fff7fff7fffULL;

        *(accum++) = ((p + q) >> 7) & 0x1ff01ff01ff01ffULL;
    }
}

static void
scale_outrow_box_vertical_256 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                               guint outrow_index, guint32 *row_out)
{
    guint ofs_y, ofs_y_max;
    guint w1, w2;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first and last rows, weight them and store in accumulator */

    w1 = (outrow_index == 0) ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1];
    w2 = scale_ctx->offsets_y [outrow_index * 2 + 1];

    scale_horizontal_for_vertical_256 (scale_ctx,
                                       inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                       vertical_ctx->parts_top_row);
    if (w2)
    {
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, ofs_y_max),
                                           vertical_ctx->parts_bottom_row);
    }
    else
    {
        /* When w2 == 0, the final inrow may be out of bounds. Don't try to access it. */
        memset (vertical_ctx->parts_bottom_row, 0, scale_ctx->width_out * sizeof (guint64));
    }

    scale_and_weight_edge_rows_box_256 (vertical_ctx->parts_top_row,
                                        vertical_ctx->parts_bottom_row,
                                        vertical_ctx->parts_top_row,
                                        w1,
                                        w2,
                                        scale_ctx->width_out);

    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal_for_vertical_256 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                           vertical_ctx->parts_bottom_row);
        add_parts (vertical_ctx->parts_bottom_row,
                   vertical_ctx->parts_top_row,
                   scale_ctx->width_out);

        ofs_y++;
    }

    finalize_vertical_256 (vertical_ctx->parts_top_row,
                           scale_ctx->span_mul_y,
                           row_out,
                           scale_ctx->width_out);
}

static void
finalize_vertical_65536 (const guint64 *accums, guint64 multiplier, guint32 *row_out, guint n)
{
    guint32 *row_out_max = row_out + n;

    while (row_out != row_out_max)
    {
        guint64 p [2];

        p [0] = scale_65536_half (*(accums++), multiplier);
        p [1] = scale_65536_half (*(accums++), multiplier);

        *(row_out++) = pack_pixel_65536 (p);
    }
}

static void
weight_row_65536 (guint64 *row, guint16 w, guint n)
{
    guint64 *row_max = row + (n * 2);

    while (row != row_max)
    {
        row [0] = ((row [0] * w) >> 8) & 0x00ffffff00ffffffULL;
        row [1] = ((row [1] * w) >> 8) & 0x00ffffff00ffffffULL;
        row += 2;
    }
}

static void
scale_outrow_box_vertical_65536 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                                 guint outrow_index, guint32 *row_out)
{
    guint ofs_y, ofs_y_max;
    guint w;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first inrow and store it */

    scale_horizontal_for_vertical_65536 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                         vertical_ctx->parts_top_row);
    weight_row_65536 (vertical_ctx->parts_top_row,
                      outrow_index == 0 ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1],
                      scale_ctx->width_out);
    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal_for_vertical_65536 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                             vertical_ctx->parts_bottom_row);
        add_parts (vertical_ctx->parts_bottom_row,
                   vertical_ctx->parts_top_row,
                   scale_ctx->width_out * 2);

        ofs_y++;
    }

    /* Final row is optional; if this is the bottommost outrow it could be out of bounds */

    w = scale_ctx->offsets_y [outrow_index * 2 + 1];
    if (w > 0)
    {
        scale_horizontal_for_vertical_65536 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                             vertical_ctx->parts_bottom_row);
        weight_row_65536 (vertical_ctx->parts_bottom_row,
                          w - 1,  /* Subtract 1 to avoid overflow */
                          scale_ctx->width_out);
        add_parts (vertical_ctx->parts_bottom_row,
                   vertical_ctx->parts_top_row,
                   scale_ctx->width_out * 2);
    }

    finalize_vertical_65536 (vertical_ctx->parts_top_row, scale_ctx->span_mul_y,
                             row_out, scale_ctx->width_out);
}

static void
update_vertical_ctx_bilinear (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx, guint new_out_ofs)
{
    guint new_in_ofs = scale_ctx->offsets_y [new_out_ofs * 2];

    if (new_in_ofs == vertical_ctx->in_ofs)
        return;

    if (new_in_ofs == vertical_ctx->in_ofs + 1)
    {
        guint64 *t = vertical_ctx->parts_top_row;
        vertical_ctx->parts_top_row = vertical_ctx->parts_bottom_row;
        vertical_ctx->parts_bottom_row = t;

        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                                           vertical_ctx->parts_bottom_row);
    }
    else
    {
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, new_in_ofs),
                                           vertical_ctx->parts_top_row);
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                                           vertical_ctx->parts_bottom_row);
    }

    vertical_ctx->in_ofs = new_in_ofs;
}

static void
scale_outrow_bilinear_vertical_256 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                                    guint outrow_index, guint32 *row_out)
{
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, outrow_index);
    interp_vertical_bilinear_256 (scale_ctx->offsets_y [outrow_index * 2 + 1],
                                  vertical_ctx->parts_top_row,
                                  vertical_ctx->parts_bottom_row,
                                  row_out,
                                  scale_ctx->width_out);
}

static void
scale_outrow (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
              guint outrow_index, guint32 *row_out)
{
    if (scale_ctx->algo_v == ALGORITHM_BILINEAR)
        scale_outrow_bilinear_vertical_256 (scale_ctx, vertical_ctx, outrow_index, row_out);
    else if (scale_ctx->algo_v == ALGORITHM_BOX_256)
        scale_outrow_box_vertical_256 (scale_ctx, vertical_ctx, outrow_index, row_out);
    else
        scale_outrow_box_vertical_65536 (scale_ctx, vertical_ctx, outrow_index, row_out);
}

static void
do_rows (const SmolScaleCtx *scale_ctx, guint row_out_index, guint n_rows)
{
    VerticalCtx vertical_ctx;
    guint64 *parts_storage;
    guint i;

    parts_storage = alloca (scale_ctx->width_out * sizeof (guint64) * 4);

    /* Must be one less, or this test in update_vertical_ctx() will wrap around:
     * if (new_in_ofs == vertical_ctx->in_ofs + 1) { ... } */
    vertical_ctx.in_ofs = G_MAXUINT - 1;
    vertical_ctx.parts_top_row = parts_storage;
    vertical_ctx.parts_bottom_row = vertical_ctx.parts_top_row + scale_ctx->width_out * 2;

    for (i = row_out_index; i < row_out_index + n_rows; i++)
    {
        scale_outrow (scale_ctx, &vertical_ctx, i, outrow_ofs_to_pointer (scale_ctx, i));
    }
}

/* --- API --- */

void
smol_scale_init (SmolScaleCtx *scale_ctx,
                 const guint32 *pixels_in, guint width_in, guint height_in, guint rowstride_in,
                 guint32 *pixels_out, guint width_out, guint height_out, guint rowstride_out)
{
    /* FIXME: Special handling for images that are a single pixel wide or tall */

    scale_ctx->pixels_in = pixels_in;
    scale_ctx->width_in = width_in;
    scale_ctx->height_in = height_in;
    scale_ctx->rowstride_in = rowstride_in / sizeof (guint32);
    scale_ctx->pixels_out = pixels_out;
    scale_ctx->width_out = width_out;
    scale_ctx->height_out = height_out;
    scale_ctx->rowstride_out = rowstride_out / sizeof (guint32);

    calc_size_steps (width_in, width_out, &scale_ctx->algo_h);
    calc_size_steps (height_in, height_out, &scale_ctx->algo_v);

    scale_ctx->offsets_x = g_new (guint16, (scale_ctx->width_out + 1) * 2 + (scale_ctx->height_out + 1) * 2);
    scale_ctx->offsets_y = scale_ctx->offsets_x + (scale_ctx->width_out + 1) * 2;

    if (scale_ctx->algo_h == ALGORITHM_BILINEAR)
    {
        precalc_bilinear_array (scale_ctx->offsets_x,
                                width_in, scale_ctx->width_out, FALSE);
    }
    else
    {
        precalc_boxes_array (scale_ctx->offsets_x, &scale_ctx->span_mul_x,
                             width_in, scale_ctx->width_out, FALSE);
    }

    if (scale_ctx->algo_v == ALGORITHM_BILINEAR)
    {
        precalc_bilinear_array (scale_ctx->offsets_y,
                                height_in, scale_ctx->height_out, TRUE);
    }
    else
    {
        precalc_boxes_array (scale_ctx->offsets_y, &scale_ctx->span_mul_y,
                             height_in, scale_ctx->height_out, TRUE);
    }
}

void
smol_scale_finalize (SmolScaleCtx *scale_ctx)
{
    g_free (scale_ctx->offsets_x);
}

void
smol_scale_bilinear_row (SmolScaleCtx *scale_ctx, guint row_out_index)
{
    do_rows (scale_ctx, row_out_index, 1);
}

#define BATCH_N_LINES 64

void
smol_scale (SmolScaleCtx *scale_ctx)
{
    guint i;

#if 0
    for (i = 0; i < scale_ctx->height_out; i++)
    {
        do_rows (scale_ctx, i, 1);
    }
#elif 0
    for (i = 0; i < scale_ctx->height_out && scale_ctx->height_out - i > BATCH_N_LINES; i += BATCH_N_LINES)
    {
        do_rows (scale_ctx, i, BATCH_N_LINES);
    }

    do_rows (scale_ctx, i, scale_ctx->height_out - i);

#else
    do_rows (scale_ctx, 0, scale_ctx->height_out);
#endif
}

#if 1

/* Single thread */

guint32 *
smol_scale_simple (const guint32 *pixels_in, guint width_in, guint height_in,
                   guint width_out, guint height_out)
{
    SmolScaleCtx scale_ctx;
    guint32 *pixels_out;

    pixels_out = g_malloc (width_out * height_out * sizeof (guint32));

    smol_scale_init (&scale_ctx, pixels_in, width_in, height_in, width_in * 4,
                     pixels_out, width_out, height_out, width_out * 4);
    smol_scale (&scale_ctx);
    smol_scale_finalize (&scale_ctx);

    return pixels_out;
}

#else

static void
smol_scale_worker (gpointer data, SmolScaleCtx *scale_ctx)
{
    guint first_row, n_rows;

    first_row = GPOINTER_TO_UINT (data) >> 16;
    n_rows = GPOINTER_TO_UINT (data) & 0xffff;
    do_rows (scale_ctx, first_row, n_rows);
}

/* Multithreaded */

guint32 *
smol_scale_simple (const guint32 *pixels_in, guint width_in, guint height_in,
                   guint width_out, guint height_out)
{
    SmolScaleCtx scale_ctx;
    guint32 *pixels_out;
    guint i;
    GThreadPool *thread_pool;
    guint n_threads;
    guint batch_n_rows;

    pixels_out = g_malloc (width_out * height_out * 4);

    smol_scale_init (&scale_ctx, pixels_in, width_in, height_in, width_in * 4,
                     pixels_out, width_out, height_out, width_out * 4);

    n_threads = g_get_num_processors ();
    thread_pool = g_thread_pool_new ((GFunc) smol_scale_worker,
                                     &scale_ctx,
                                     n_threads,
                                     FALSE,
                                     NULL);

    batch_n_rows = (scale_ctx.height_out + n_threads - 1) / n_threads;

    for (i = 0; i < scale_ctx.height_out; )
    {
        guint n = MIN (batch_n_rows, scale_ctx.height_out - i);
        g_thread_pool_push (thread_pool, GUINT_TO_POINTER ((i << 16) | n), NULL);
        i += n;
    }

    g_thread_pool_free (thread_pool, FALSE, TRUE);
    smol_scale_finalize (&scale_ctx);

    return pixels_out;
}

#endif
