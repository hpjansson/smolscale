/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include "scale.h"
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memset */
#include <alloca.h> /* alloca */
#include <limits.h>

#if 0
/* To be used in separate compilation unit */
#include <immintrin.h>  /* AVX2 */
#endif

#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#define SMOL_RESTRICT __restrict
#define SMOL_INLINE __attribute__((always_inline)) inline
#define SMOL_CONST __attribute__((const))
#define SMOL_PURE __attribute__((pure))
#define SMOL_ALIGNED_4 __attribute__((aligned(4)))
#define SMOL_ALIGNED_8 __attribute__((aligned(8)))
#define SMOL_ALIGNED_16 __attribute__((aligned(16)))
#define SMOL_ALIGNED_32 __attribute__((aligned(32)))

#define SMALL_MUL 256U
#define BIG_MUL 65536U
#define BOXES_MULTIPLIER ((uint64_t) BIG_MUL * SMALL_MUL)
#define FUDGE_FACTOR (SMALL_MUL + SMALL_MUL / 2 + SMALL_MUL / 4)

#define aligned_alloca(s, a) \
  ({ void *p = alloca ((s) + (a)); p = (void *) (((uintptr_t) (p) + (a)) & ~((a) - 1)); (p); })

/* For reusing rows that have already undergone horizontal scaling */
typedef struct
{
    uint32_t in_ofs;
    uint64_t *parts_row [3];
}
VerticalCtx;

/* --- Pixel and parts manipulation --- */

static SMOL_PURE SMOL_INLINE const uint32_t *
inrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx, uint32_t inrow_ofs)
{
    return scale_ctx->pixels_in + scale_ctx->rowstride_in * inrow_ofs;
}

static SMOL_PURE SMOL_INLINE uint32_t *
outrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx, uint32_t outrow_ofs)
{
    return scale_ctx->pixels_out + scale_ctx->rowstride_out * outrow_ofs;
}

static SMOL_CONST SMOL_INLINE uint32_t
pack_pixel_256 (uint64_t in)
{
    return in | (in >> 24);
}

static SMOL_CONST SMOL_INLINE uint64_t
unpack_pixel_256 (uint32_t p)
{
    return (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff00ff);
}

static inline uint32_t
pack_pixel_65536 (uint64_t *in)
{
    /* FIXME: Are masks needed? */
    return ((in [0] >> 8) & 0xff000000)
           | ((in [0] << 16) & 0x00ff0000)
           | ((in [1] >> 24) & 0x0000ff00)
           | (in [1] & 0x000000ff);
}

static inline void
unpack_pixel_65536 (uint32_t p, uint64_t *out)
{
    out [0] = (((uint64_t) p & 0xff000000) << 8) | (((uint64_t) p & 0x00ff0000) >> 16);
    out [1] = (((uint64_t) p & 0x0000ff00) << 24) | (p & 0x000000ff);
}

static inline uint64_t
weight_pixel_256 (uint64_t p, uint16_t w)
{
    return ((p * w) >> 1) & 0x7fff7fff7fff7fff;
}

static inline void
weight_pixel_65536 (uint64_t *p, uint64_t *out, uint16_t w)
{
    out [0] = ((p [0] * w) >> 1) & 0x7fffffff7fffffffULL;
    out [1] = ((p [1] * w) >> 1) & 0x7fffffff7fffffffULL;
}

static void
pack_row_256 (const uint64_t *row_in, uint32_t *row_out, uint32_t n)
{
    uint32_t *row_out_max = row_out + n;

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_256 (*(row_in++));
    }
}

/* AVX2 has a useful instruction for this: __m256i _mm256_cvtepu8_epi16 (__m128i a);
 * It results in a different channel ordering, so it'd be important to match with
 * the right kind of re-pack. */
static void
unpack_row_256 (const uint32_t *row_in, uint64_t *row_out, uint32_t n)
{
    uint64_t *row_out_max = row_out + n;

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_256 (*(row_in++));
    }
}

static inline void
sum_pixels_256 (const uint32_t **pp, uint64_t *accum, uint32_t n)
{
    const uint32_t *pp_end;

    for (pp_end = *pp + n; *pp < pp_end; (*pp)++)
    {
        *accum += unpack_pixel_256 (**pp);
    }
}

static inline void
sum_pixels_65536 (const uint32_t **pp, uint64_t *accum, uint32_t n)
{
    const uint32_t *pp_end;

    for (pp_end = *pp + n; *pp < pp_end; (*pp)++)
    {
        uint64_t p [2];
        unpack_pixel_65536 (**pp, p);
        accum [0] += p [0];
        accum [1] += p [1];
    }
}

static inline uint64_t
scale_256 (uint64_t accum, uint64_t multiplier)
{
    uint64_t a, b;

    /* Average the inputs */
    a = ((accum & 0x0000ffff0000ffffULL) * multiplier
         + (BOXES_MULTIPLIER / 2) + ((BOXES_MULTIPLIER / 2) << 32)) / BOXES_MULTIPLIER;
    b = (((accum & 0xffff0000ffff0000ULL) >> 16) * multiplier
         + (BOXES_MULTIPLIER / 2) + ((BOXES_MULTIPLIER / 2) << 32)) / BOXES_MULTIPLIER;

    /* Return pixel */
    return (a & 0x000000ff000000ffULL) | ((b & 0x000000ff000000ffULL) << 16);
}

static inline uint64_t
scale_65536_half (uint64_t accum, uint64_t multiplier)
{
    uint64_t a, b;

    a = accum & 0x00000000ffffffffULL;
    a = (a * multiplier + BOXES_MULTIPLIER / 2) / BOXES_MULTIPLIER;

    b = (accum & 0xffffffff00000000ULL) >> 32;
    b = (b * multiplier + BOXES_MULTIPLIER / 2) / BOXES_MULTIPLIER;

    return (a & 0x00000000000000ffULL)
           | ((b & 0x00000000000000ffULL) << 32);
}

static inline void
scale_and_store_65536 (uint64_t *accum, uint64_t multiplier, uint64_t **row_parts_out)
{
    *(*row_parts_out)++ = scale_65536_half (accum [0], multiplier);
    *(*row_parts_out)++ = scale_65536_half (accum [1], multiplier);
}

static void
convert_parts_256_to_65536 (uint64_t *row, uint32_t n)
{
    uint64_t *temp;
    uint32_t i, j;

    temp = alloca (n * sizeof (uint64_t) * 2);

    for (i = 0, j = 0; i < n; )
    {
        temp [j++] = (row [i] >> 16) & 0x000000ff000000ff;
        temp [j++] = row [i++] & 0x000000ff000000ff;
    }

    memcpy (row, temp, n * sizeof (uint64_t) * 2);
}

static void
convert_parts_65536_to_256 (uint64_t *row, uint32_t n)
{
    uint32_t i, j;

    for (i = 0, j = 0; j < n; )
    {
        row [j] = row [i++] << 16;
        row [j++] |= row [i++];
    }
}

static void
add_parts (const uint64_t *parts_in, uint64_t *parts_acc_out, uint32_t n)
{
    const uint64_t *parts_in_max = parts_in + n;

    while (parts_in < parts_in_max)
        *(parts_acc_out++) += *(parts_in++);
}

/* --- Precalculation --- */

static void
calc_size_steps (uint32_t dim_in, uint32_t dim_out,
                 unsigned int *n_halvings,
                 uint32_t *dim_bilin_out,
                 SmolAlgorithm *algo)
{
    *n_halvings = 0;
    *dim_bilin_out = dim_out;

    if (dim_in > dim_out * 127)
        *algo = ALGORITHM_BOX_65536;
    else if (dim_in > dim_out * 2)
    {
#if 0
        *algo = ALGORITHM_BOX_256;
#else
        uint32_t d = dim_out;

        *algo = ALGORITHM_BILINEAR;

        for (;;)
        {
            d *= 2;
            if (d >= dim_in)
                break;
            (*n_halvings)++;
        }
        dim_out <<= *n_halvings;
        *dim_bilin_out = dim_out;
#endif
    }
    else if (dim_in == 1)
        *algo = ALGORITHM_ONE;
    else
        *algo = ALGORITHM_BILINEAR;
}

static void
precalc_bilinear_array (uint16_t *array,
                        uint32_t dim_in, uint32_t dim_out,
                        unsigned int make_absolute_offsets)
{
    uint32_t ofs_stepF, fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t last_ofs = 0;

    /* Works when dim_in >= dim_out, 1=1 is perfect */
    frac_stepF = ofs_stepF = ((dim_in - 1) * BIG_MUL + FUDGE_FACTOR) / (dim_out > 1 ? (dim_out - 1) : 1);
    fracF = 0;

    do
    {
        uint16_t ofs = fracF / BIG_MUL;

        /* We sample ofs and its neighbor -- prevent out of bounds access
         * for the latter. */
        if (ofs >= dim_in - 1)
            break;

        *(pu16++) = make_absolute_offsets ? ofs : ofs - last_ofs;
        *(pu16++) = SMALL_MUL - ((fracF / (BIG_MUL / SMALL_MUL)) % SMALL_MUL);
        fracF += frac_stepF;

        last_ofs = ofs;
    }
    while (--dim_out);

    /* Instead of going out of bounds, sample the final pair of pixels with a 100%
     * bias towards the last pixel */
    while (dim_out)
    {
        *(pu16++) = make_absolute_offsets ? dim_in - 2 : (dim_in - 2) - last_ofs;
        *(pu16++) = 0;
        dim_out--;

        last_ofs = dim_in - 2;
    }
}

static void
precalc_boxes_array (uint16_t *array, uint32_t *span_mul, uint32_t dim_in, uint32_t dim_out,
                     unsigned int make_absolute_offsets)
{
    uint64_t fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t ofs, next_ofs;
    uint32_t span_mul_orig;
    uint64_t f;
    uint64_t stride;

    frac_stepF = ((uint64_t) dim_in * BIG_MUL) / (uint64_t) dim_out;
    fracF = 0;
    ofs = 0;

    *span_mul = span_mul_orig = (BOXES_MULTIPLIER * BIG_MUL * SMALL_MUL) / (frac_stepF * SMALL_MUL - BIG_MUL);

    stride = frac_stepF / (uint64_t) BIG_MUL;
    f = (frac_stepF / SMALL_MUL) % SMALL_MUL;
    while (((((stride * 255) + ((f * 255) / 2) / 128) - 1)
            * (uint64_t) *span_mul + (BOXES_MULTIPLIER / 4)) / (BOXES_MULTIPLIER) < 255)
        (*span_mul)++;

    do
    {
        fracF += frac_stepF;
        next_ofs = (uint64_t) fracF / ((uint64_t) BIG_MUL);

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
        *(pu16++) = make_absolute_offsets ? ofs : stride;
        *(pu16++) = f;

        ofs = next_ofs;
    }
    while (--dim_out);

    /* Instead of going out of bounds, sample the final pair of pixels with a 100%
     * bias towards the last pixel */
    while (dim_out)
    {
        *(pu16++) = make_absolute_offsets ? ofs : 0;
        *(pu16++) = 0;
        dim_out--;
    }

    *(pu16++) = make_absolute_offsets ? ofs : 0;
    *(pu16++) = 0;
}

/* --- Horizontal scaling --- */

#define DEF_INTERP_HORIZONTAL_BILINEAR(n_halvings)                      \
static void                                                             \
interp_horizontal_bilinear_##n_halvings (const SmolScaleCtx *scale_ctx, \
                                         const uint32_t *row_in,        \
                                         uint64_t * SMOL_RESTRICT row_parts_out) \
{                                                                       \
    uint64_t p, q;                                                      \
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t F;                                                         \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out; \
    uint64_t * SMOL_RESTRICT unpacked_in;                               \
    int i;                                                              \
                                                                        \
    unpacked_in = alloca (scale_ctx->width_in * sizeof (uint64_t));     \
    unpack_row_256 (row_in, unpacked_in, scale_ctx->width_in);          \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t accum = 0;                                             \
                                                                        \
        for (i = 0; i < (1 << (n_halvings)); i++)                       \
        {                                                               \
            unpacked_in += *(ofs_x++);                                  \
            F = *(ofs_x++);                                             \
                                                                        \
            p = *unpacked_in;                                           \
            q = *(unpacked_in + 1);                                     \
                                                                        \
            accum += ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL; \
        }                                                               \
        *(row_parts_out++) = ((accum) >> (n_halvings)) & 0x00ff00ff00ff00ffULL; \
    }                                                                   \
    while (row_parts_out != row_parts_out_max);                         \
}

static void
interp_horizontal_bilinear_0 (const SmolScaleCtx *scale_ctx, const uint32_t *row_in, uint64_t *row_parts_out)
{
    uint64_t p, q;
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t F;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out;
    uint64_t * SMOL_RESTRICT unpacked_in;

    unpacked_in = alloca (scale_ctx->width_in * sizeof (uint64_t));
    unpack_row_256 (row_in, unpacked_in, scale_ctx->width_in);

    do
    {
        unpacked_in += *(ofs_x++);
        F = *(ofs_x++);

        p = *unpacked_in;
        q = *(unpacked_in + 1);

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (row_parts_out != row_parts_out_max);
}

DEF_INTERP_HORIZONTAL_BILINEAR(1)
DEF_INTERP_HORIZONTAL_BILINEAR(2)
DEF_INTERP_HORIZONTAL_BILINEAR(3)
DEF_INTERP_HORIZONTAL_BILINEAR(4)
DEF_INTERP_HORIZONTAL_BILINEAR(5)
DEF_INTERP_HORIZONTAL_BILINEAR(6)
DEF_INTERP_HORIZONTAL_BILINEAR(7)

static void
interp_horizontal_bilinear (const SmolScaleCtx *scale_ctx, const uint32_t *row_in, uint64_t *row_parts_out)
{
    switch (scale_ctx->width_halvings)
    {
        case 0:
            interp_horizontal_bilinear_0 (scale_ctx, row_in, row_parts_out);
            break;
        case 1:
            interp_horizontal_bilinear_1 (scale_ctx, row_in, row_parts_out);
            break;
        case 2:
            interp_horizontal_bilinear_2 (scale_ctx, row_in, row_parts_out);
            break;
        case 3:
            interp_horizontal_bilinear_3 (scale_ctx, row_in, row_parts_out);
            break;
        case 4:
            interp_horizontal_bilinear_4 (scale_ctx, row_in, row_parts_out);
            break;
        case 5:
            interp_horizontal_bilinear_5 (scale_ctx, row_in, row_parts_out);
            break;
        case 6:
            interp_horizontal_bilinear_6 (scale_ctx, row_in, row_parts_out);
            break;
        case 7:
            interp_horizontal_bilinear_7 (scale_ctx, row_in, row_parts_out);
            break;
    }
}

static void
interp_horizontal_boxes_256 (const SmolScaleCtx *scale_ctx, const uint32_t *row_in, uint64_t *row_parts_out)
{
    const uint32_t *pp;
    const uint16_t *ofs_x = scale_ctx->offsets_x;
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out - 1;
    uint64_t accum = 0;
    uint64_t p, q, r, s;
    uint32_t n;
    uint64_t F;

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

        /* (255 * r) - (F * r) */
        p = (((r << 8) - r - s) >> 1) & 0x7fff7fff7fff7fffULL;

        *(row_parts_out++) = scale_256 (accum, scale_ctx->span_mul_x);
        accum = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_pixels_256 (&pp, &accum, n);

    q = 0;
    F = *(ofs_x);
    if (F > 0)
        q = weight_pixel_256 (unpack_pixel_256 (*(pp)), F);

    accum += ((p + q) >> 7) & 0x01ff01ff01ff01ffULL;
    *(row_parts_out++) = scale_256 (accum, scale_ctx->span_mul_x);
}

static void
interp_horizontal_boxes_65536 (const SmolScaleCtx *scale_ctx, const uint32_t *row_in, uint64_t *row_parts_out)
{
    const uint32_t *pp;
    const uint16_t *ofs_x = scale_ctx->offsets_x;
    uint64_t *row_parts_out_max = row_parts_out + (scale_ctx->width_out - /* 2 */ 1) * 2;
    uint64_t accum [2] = { 0, 0 };
    uint64_t p [2], q [2], r [2], s [2];
    uint32_t n;
    uint64_t F;

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
interp_horizontal_one (const SmolScaleCtx *scale_ctx, const uint32_t * SMOL_RESTRICT row_in, uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out;
    uint64_t part;

    part = unpack_pixel_256 (*row_in);
    while (row_parts_out != row_parts_out_max)
        *(row_parts_out++) = part;
}

static void
scale_horizontal (const SmolScaleCtx *scale_ctx, const uint32_t *row_in, uint64_t *row_parts_out)
{
    if (scale_ctx->algo_h == ALGORITHM_BILINEAR)
        interp_horizontal_bilinear (scale_ctx, row_in, row_parts_out);
    else if (scale_ctx->algo_h == ALGORITHM_BOX_256)
        interp_horizontal_boxes_256 (scale_ctx, row_in, row_parts_out);
    else if (scale_ctx->algo_h == ALGORITHM_BOX_65536)
        interp_horizontal_boxes_65536 (scale_ctx, row_in, row_parts_out);
    else /* scale_ctx->algo_h == ALGORITHM_ONE */
        interp_horizontal_one (scale_ctx, row_in, row_parts_out);
}

static void
scale_horizontal_for_vertical_256 (const SmolScaleCtx *scale_ctx, 
                                   const uint32_t *row_in, uint64_t *row_parts_out)
{
    scale_horizontal (scale_ctx, row_in, row_parts_out);
    if (scale_ctx->algo_h == ALGORITHM_BOX_65536)
    {
        convert_parts_65536_to_256 (row_parts_out, scale_ctx->width_out);
    }
}

static void
scale_horizontal_for_vertical_65536 (const SmolScaleCtx *scale_ctx, 
                                     const uint32_t *row_in, uint64_t *row_parts_out)
{
    scale_horizontal (scale_ctx, row_in, row_parts_out);
    if (scale_ctx->algo_h != ALGORITHM_BOX_65536)
    {
        convert_parts_256_to_65536 (row_parts_out, scale_ctx->width_out);
    }
}

/* --- Vertical scaling --- */

static void
update_vertical_ctx_bilinear (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                              uint32_t outrow_index)
{
    uint32_t new_in_ofs = scale_ctx->offsets_y [outrow_index * 2];

    if (new_in_ofs == vertical_ctx->in_ofs)
        return;

    if (new_in_ofs == vertical_ctx->in_ofs + 1)
    {
        uint64_t *t = vertical_ctx->parts_row [0];
        vertical_ctx->parts_row [0] = vertical_ctx->parts_row [1];
        vertical_ctx->parts_row [1] = t;

        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                                           vertical_ctx->parts_row [1]);
    }
    else
    {
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, new_in_ofs),
                                           vertical_ctx->parts_row [0]);
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                                           vertical_ctx->parts_row [1]);
    }

    vertical_ctx->in_ofs = new_in_ofs;
}

static void
interp_vertical_bilinear_once (uint64_t F, const uint64_t * SMOL_RESTRICT SMOL_ALIGNED_8 top_row_parts_in,
                               const uint64_t * SMOL_RESTRICT SMOL_ALIGNED_8 bottom_row_parts_in,
                               uint32_t * SMOL_RESTRICT SMOL_ALIGNED_4 row_out, int width)
{
    int i, j;

    for (i = 0; i + 16 <= width; i += 16)
    {
        for (j = 0; j < 16; j++)
        {
            uint64_t p, q;

            p = *(top_row_parts_in++);
            q = *(bottom_row_parts_in++);

            p = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ff;

            *(row_out++) = (uint32_t) (p | p >> 24);
        }
    }

    for ( ; i < width; i++)
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        p = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ff;

        *(row_out++) = (uint32_t) (p | p >> 24);
    }
}

static void
interp_vertical_bilinear_store (uint64_t F, const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                uint64_t * SMOL_RESTRICT parts_out, uint32_t width)
{
    uint64_t *parts_out_last = parts_out + width;

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (parts_out != parts_out_last);
}

static void
interp_vertical_bilinear_add (uint64_t F, const uint64_t * SMOL_RESTRICT top_row_parts_in,
                              const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                              uint64_t * SMOL_RESTRICT accum_out, uint32_t width)
{
    uint64_t *accum_out_last = accum_out + width;

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(accum_out++) += ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (accum_out != accum_out_last);
}

#define DEF_INTERP_VERTICAL_BILINEAR_FINAL(n_halvings)                  \
static void                                                             \
interp_vertical_bilinear_final_##n_halvings (uint64_t F, const uint64_t * SMOL_RESTRICT top_row_parts_in, \
                                             const uint64_t * SMOL_RESTRICT bottom_row_parts_in, \
                                             const uint64_t * SMOL_RESTRICT accum_in, uint32_t * SMOL_RESTRICT row_out, uint32_t width) \
{                                                                       \
    uint32_t *row_out_last = row_out + width;                           \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t p, q;                                                  \
                                                                        \
        p = *(top_row_parts_in++);                                      \
        q = *(bottom_row_parts_in++);                                   \
                                                                        \
        p = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;         \
        p = ((p + *(accum_in++)) >> n_halvings) & 0x00ff00ff00ff00ffULL; \
                                                                        \
        *(row_out++) = pack_pixel_256 (p);                              \
    }                                                                   \
    while (row_out != row_out_last);                                    \
}

#define DEF_SCALE_OUTROW_BILINEAR(n_halvings)                           \
static void                                                             \
scale_outrow_bilinear_##n_halvings (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx, \
                                    uint32_t outrow_index, uint32_t *row_out) \
{                                                                       \
    uint32_t bilin_index = outrow_index << (n_halvings);                \
    unsigned int i;                                                     \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_store (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                    vertical_ctx->parts_row [0],        \
                                    vertical_ctx->parts_row [1],        \
                                    vertical_ctx->parts_row [2],        \
                                    scale_ctx->width_out);              \
    bilin_index++;                                                      \
                                                                        \
    for (i = 0; i < (1 << (n_halvings)) - 2; i++)                       \
    {                                                                   \
        update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
        interp_vertical_bilinear_add (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                      vertical_ctx->parts_row [0],      \
                                      vertical_ctx->parts_row [1],      \
                                      vertical_ctx->parts_row [2],      \
                                      scale_ctx->width_out);            \
        bilin_index++;                                                  \
    }                                                                   \
                                                                        \
    interp_vertical_bilinear_final_##n_halvings (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                                 vertical_ctx->parts_row [0], \
                                                 vertical_ctx->parts_row [1], \
                                                 vertical_ctx->parts_row [2], \
                                                 row_out,               \
                                                 scale_ctx->width_out); \
}

static void
scale_outrow_bilinear_0 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                         uint32_t outrow_index, uint32_t *row_out)
{
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, outrow_index);
    interp_vertical_bilinear_once (scale_ctx->offsets_y [outrow_index * 2 + 1],
                                   vertical_ctx->parts_row [0],
                                   vertical_ctx->parts_row [1],
                                   row_out,
                                   scale_ctx->width_out);
}

DEF_INTERP_VERTICAL_BILINEAR_FINAL(1)

static void
scale_outrow_bilinear_1 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                         uint32_t outrow_index, uint32_t *row_out)
{
    uint32_t bilin_index = outrow_index << 1;

    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_store (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                    vertical_ctx->parts_row [0],
                                    vertical_ctx->parts_row [1],
                                    vertical_ctx->parts_row [2],
                                    scale_ctx->width_out);
    bilin_index++;
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_final_1 (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                      vertical_ctx->parts_row [0],
                                      vertical_ctx->parts_row [1],
                                      vertical_ctx->parts_row [2],
                                      row_out,
                                      scale_ctx->width_out);
}

DEF_INTERP_VERTICAL_BILINEAR_FINAL(2)
DEF_SCALE_OUTROW_BILINEAR(2)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(3)
DEF_SCALE_OUTROW_BILINEAR(3)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(4)
DEF_SCALE_OUTROW_BILINEAR(4)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(5)
DEF_SCALE_OUTROW_BILINEAR(5)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(6)
DEF_SCALE_OUTROW_BILINEAR(6)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(7)
DEF_SCALE_OUTROW_BILINEAR(7)

static void
scale_outrow_bilinear (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                       uint32_t outrow_index, uint32_t *row_out)
{
    switch (scale_ctx->height_halvings)
    {
        case 0:
            scale_outrow_bilinear_0 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 1:
            scale_outrow_bilinear_1 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 2:
            scale_outrow_bilinear_2 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 3:
            scale_outrow_bilinear_3 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 4:
            scale_outrow_bilinear_4 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 5:
            scale_outrow_bilinear_5 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 6:
            scale_outrow_bilinear_6 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
        case 7:
            scale_outrow_bilinear_7 (scale_ctx, vertical_ctx, outrow_index, row_out);
            break;
    }
}

static void
finalize_vertical_256 (const uint64_t *accums, uint64_t multiplier, uint32_t *row_out, uint32_t n)
{
    uint32_t *row_out_max = row_out + n;

    while (row_out != row_out_max)
    {
        uint64_t p;

        p = scale_256 (*(accums++), multiplier);
        *(row_out++) = pack_pixel_256 (p);
    }
}

static void
weight_edge_row_256 (uint64_t *row, uint16_t w, uint32_t n)
{
    uint64_t *row_max = row + n;

    while (row != row_max)
    {
        *row = ((*row * w) >> 1) & 0x7fff7fff7fff7fffULL;
        row++;
    }
}

static void
scale_and_weight_edge_rows_box_256 (const uint64_t *first_row, uint64_t *last_row, uint64_t *accum, uint16_t w2, uint32_t n)
{
    const uint64_t *first_row_max = first_row + n;

    while (first_row != first_row_max)
    {
        uint64_t r, s, p, q;

        p = *(first_row++);

        r = *(last_row);
        s = r * w2;
        q = (s >> 1) & 0x7fff7fff7fff7fffULL;
        /* (255 * r) - (F * r) */
        *(last_row++) = (((r << 8) - r - s) >> 1) & 0x7fff7fff7fff7fffULL;

        *(accum++) = ((p + q) >> 7) & 0x1ff01ff01ff01ffULL;
    }
}

static void
update_vertical_ctx_box_256 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx, uint32_t ofs_y, uint32_t ofs_y_max, uint16_t w1, uint16_t w2)
{
    /* Old in_ofs is the previous max */
    if (ofs_y == vertical_ctx->in_ofs)
    {
        uint64_t *t = vertical_ctx->parts_row [0];
        vertical_ctx->parts_row [0] = vertical_ctx->parts_row [1];
        vertical_ctx->parts_row [1] = t;
    }
    else
    {
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                           vertical_ctx->parts_row [0]);
        weight_edge_row_256 (vertical_ctx->parts_row [0], w1, scale_ctx->width_out);
    }

    /* When w2 == 0, the final inrow may be out of bounds. Don't try to access it in
     * that case. */
    if (w2 || ofs_y_max < scale_ctx->height_in)
    {
        scale_horizontal_for_vertical_256 (scale_ctx,
                                           inrow_ofs_to_pointer (scale_ctx, ofs_y_max),
                                           vertical_ctx->parts_row [1]);
    }
    else
    {
        memset (vertical_ctx->parts_row [1], 0, scale_ctx->width_out * sizeof (uint64_t));
    }

    vertical_ctx->in_ofs = ofs_y_max;
}

static void
scale_outrow_box_vertical_256 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                               uint32_t outrow_index, uint32_t *row_out)
{
    uint32_t ofs_y, ofs_y_max;
    uint16_t w1, w2;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first and last rows, weight them and store in accumulator */

    w1 = (outrow_index == 0) ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1];
    w2 = scale_ctx->offsets_y [outrow_index * 2 + 1];

    update_vertical_ctx_box_256 (scale_ctx, vertical_ctx, ofs_y, ofs_y_max, w1, w2);

    scale_and_weight_edge_rows_box_256 (vertical_ctx->parts_row [0],
                                        vertical_ctx->parts_row [1],
                                        vertical_ctx->parts_row [2],
                                        w2,
                                        scale_ctx->width_out);

    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal_for_vertical_256 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                           vertical_ctx->parts_row [0]);
        add_parts (vertical_ctx->parts_row [0],
                   vertical_ctx->parts_row [2],
                   scale_ctx->width_out);

        ofs_y++;
    }

    finalize_vertical_256 (vertical_ctx->parts_row [2],
                           scale_ctx->span_mul_y,
                           row_out,
                           scale_ctx->width_out);
}

static void
finalize_vertical_65536 (const uint64_t *accums, uint64_t multiplier, uint32_t *row_out, uint32_t n)
{
    uint32_t *row_out_max = row_out + n;

    while (row_out != row_out_max)
    {
        uint64_t p [2];

        p [0] = scale_65536_half (*(accums++), multiplier);
        p [1] = scale_65536_half (*(accums++), multiplier);

        *(row_out++) = pack_pixel_65536 (p);
    }
}

static void
weight_row_65536 (uint64_t *row, uint16_t w, uint32_t n)
{
    uint64_t *row_max = row + (n * 2);

    while (row != row_max)
    {
        row [0] = ((row [0] * w) >> 8) & 0x00ffffff00ffffffULL;
        row [1] = ((row [1] * w) >> 8) & 0x00ffffff00ffffffULL;
        row += 2;
    }
}

static void
scale_outrow_box_vertical_65536 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                                 uint32_t outrow_index, uint32_t *row_out)
{
    uint32_t ofs_y, ofs_y_max;
    uint16_t w;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first inrow and store it */

    scale_horizontal_for_vertical_65536 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                         vertical_ctx->parts_row [0]);
    weight_row_65536 (vertical_ctx->parts_row [0],
                      outrow_index == 0 ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1],
                      scale_ctx->width_out);
    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal_for_vertical_65536 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                             vertical_ctx->parts_row [1]);
        add_parts (vertical_ctx->parts_row [1],
                   vertical_ctx->parts_row [0],
                   scale_ctx->width_out * 2);

        ofs_y++;
    }

    /* Final row is optional; if this is the bottommost outrow it could be out of bounds */

    w = scale_ctx->offsets_y [outrow_index * 2 + 1];
    if (w > 0)
    {
        scale_horizontal_for_vertical_65536 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, ofs_y),
                                             vertical_ctx->parts_row [1]);
        weight_row_65536 (vertical_ctx->parts_row [1],
                          w - 1,  /* Subtract 1 to avoid overflow */
                          scale_ctx->width_out);
        add_parts (vertical_ctx->parts_row [1],
                   vertical_ctx->parts_row [0],
                   scale_ctx->width_out * 2);
    }

    finalize_vertical_65536 (vertical_ctx->parts_row [0], scale_ctx->span_mul_y,
                             row_out, scale_ctx->width_out);
}

static void
scale_outrow_one_vertical_256 (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
                               uint32_t *row_out)
{
    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal_for_vertical_256 (scale_ctx, inrow_ofs_to_pointer (scale_ctx, 0),
                                           vertical_ctx->parts_row [0]);
        vertical_ctx->in_ofs = 0;
    }

    pack_row_256 (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow (const SmolScaleCtx *scale_ctx, VerticalCtx *vertical_ctx,
              uint32_t outrow_index, uint32_t *row_out)
{
    if (scale_ctx->algo_v == ALGORITHM_BILINEAR)
        scale_outrow_bilinear (scale_ctx, vertical_ctx, outrow_index, row_out);
    else if (scale_ctx->algo_v == ALGORITHM_BOX_256)
        scale_outrow_box_vertical_256 (scale_ctx, vertical_ctx, outrow_index, row_out);
    else if (scale_ctx->algo_v == ALGORITHM_BOX_65536)
        scale_outrow_box_vertical_65536 (scale_ctx, vertical_ctx, outrow_index, row_out);
    else /* if (scale_ctx->algo_v == ALGORITHM_ONE) */
        scale_outrow_one_vertical_256 (scale_ctx, vertical_ctx, row_out);
}

static void
do_rows (const SmolScaleCtx *scale_ctx, uint32_t row_out_index, uint32_t n_rows)
{
    VerticalCtx vertical_ctx;
    uint64_t *parts_storage;
    uint32_t n_parts_per_pixel = 1;
    uint32_t n_stored_rows = 3;
    uint32_t i;

    if (scale_ctx->algo_h == ALGORITHM_BOX_65536 || scale_ctx->algo_v == ALGORITHM_BOX_65536)
        n_parts_per_pixel = 2;

    if (scale_ctx->algo_v == ALGORITHM_ONE)
        n_stored_rows = 1;

    parts_storage = alloca (scale_ctx->width_out * sizeof (uint64_t)
                            * n_parts_per_pixel * n_stored_rows);

    /* Must be one less, or this test in update_vertical_ctx() will wrap around:
     * if (new_in_ofs == vertical_ctx->in_ofs + 1) { ... } */
    vertical_ctx.in_ofs = UINT_MAX - 1;
    vertical_ctx.parts_row [0] = parts_storage;
    vertical_ctx.parts_row [1] = vertical_ctx.parts_row [0] + scale_ctx->width_out * n_parts_per_pixel;
    if (n_stored_rows == 3)
        vertical_ctx.parts_row [2] = vertical_ctx.parts_row [1] + scale_ctx->width_out * n_parts_per_pixel;
    else
        vertical_ctx.parts_row [2] = NULL;

    for (i = row_out_index; i < row_out_index + n_rows; i++)
    {
        scale_outrow (scale_ctx, &vertical_ctx, i, outrow_ofs_to_pointer (scale_ctx, i));
    }
}

/* --- API --- */

void
smol_scale_init (SmolScaleCtx *scale_ctx,
                 const uint32_t *pixels_in, uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                 uint32_t *pixels_out, uint32_t width_out, uint32_t height_out, uint32_t rowstride_out)
{
    scale_ctx->pixels_in = pixels_in;
    scale_ctx->width_in = width_in;
    scale_ctx->height_in = height_in;
    scale_ctx->rowstride_in = rowstride_in / sizeof (uint32_t);
    scale_ctx->pixels_out = pixels_out;
    scale_ctx->width_out = width_out;
    scale_ctx->height_out = height_out;
    scale_ctx->rowstride_out = rowstride_out / sizeof (uint32_t);

    calc_size_steps (width_in, width_out,
                     &scale_ctx->width_halvings,
                     &scale_ctx->width_bilin_out,
                     &scale_ctx->algo_h);
    calc_size_steps (height_in, height_out,
                     &scale_ctx->height_halvings,
                     &scale_ctx->height_bilin_out,
                     &scale_ctx->algo_v);

    scale_ctx->offsets_x = malloc (((scale_ctx->width_bilin_out + 1) * 2
                                    + (scale_ctx->height_bilin_out + 1) * 2) * sizeof (uint16_t));
    scale_ctx->offsets_y = scale_ctx->offsets_x + (scale_ctx->width_bilin_out + 1) * 2;

    if (scale_ctx->algo_h == ALGORITHM_BILINEAR)
    {
        precalc_bilinear_array (scale_ctx->offsets_x,
                                width_in, scale_ctx->width_bilin_out, FALSE);
    }
    else if (scale_ctx->algo_h != ALGORITHM_ONE)
    {
        precalc_boxes_array (scale_ctx->offsets_x, &scale_ctx->span_mul_x,
                             width_in, scale_ctx->width_out, FALSE);
    }

    if (scale_ctx->algo_v == ALGORITHM_BILINEAR)
    {
        precalc_bilinear_array (scale_ctx->offsets_y,
                                height_in, scale_ctx->height_bilin_out, TRUE);
    }
    else if (scale_ctx->algo_v != ALGORITHM_ONE)
    {
        precalc_boxes_array (scale_ctx->offsets_y, &scale_ctx->span_mul_y,
                             height_in, scale_ctx->height_out, TRUE);
    }
}

void
smol_scale_finalize (SmolScaleCtx *scale_ctx)
{
    free (scale_ctx->offsets_x);
}

void
smol_scale_simple (const uint32_t *pixels_in,
                   uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                   uint32_t *pixels_out,
                   uint32_t width_out, uint32_t height_out, uint32_t rowstride_out)
{
    SmolScaleCtx scale_ctx;

    smol_scale_init (&scale_ctx,
                     pixels_in, width_in, height_in, rowstride_in,
                     pixels_out, width_out, height_out, rowstride_out);
    do_rows (&scale_ctx, 0, scale_ctx.height_out);
    smol_scale_finalize (&scale_ctx);
}

void
smol_scale_batch (const SmolScaleCtx *scale_ctx, uint32_t first_out_row, uint32_t n_out_rows)
{
    do_rows (scale_ctx, first_out_row, n_out_rows);
}
