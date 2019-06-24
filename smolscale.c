/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <stdlib.h> /* malloc, free */
#include <string.h> /* memset */
#include <alloca.h> /* alloca */
#include <limits.h>
#include "smolscale.h"

#ifndef FALSE
# define FALSE (0)
#endif
#ifndef TRUE
# define TRUE (!FALSE)
#endif

#define SMOL_RESTRICT __restrict
#define SMOL_INLINE __attribute__((always_inline)) inline
#define SMOL_CONST __attribute__((const))
#define SMOL_PURE __attribute__((pure))
#define SMOL_ALIGNED_4 __attribute__((aligned(4)))
#define SMOL_ALIGNED_8 __attribute__((aligned(8)))
#define SMOL_ALIGNED_16 __attribute__((aligned(16)))
#define SMOL_ALIGNED_32 __attribute__((aligned(32)))
#define SMOL_ALIGNED_64 __attribute__((aligned(64)))

#define SMALL_MUL 256U
#define BIG_MUL 65536U
#define BOXES_MULTIPLIER ((uint64_t) BIG_MUL * SMALL_MUL)
#define BILIN_MULTIPLIER ((uint64_t) BIG_MUL * BIG_MUL)

#define aligned_alloca(s, a) \
  ({ void *p = alloca ((s) + (a)); p = (void *) (((uintptr_t) (p) + (a)) & ~((a) - 1)); (p); })

/* --- Pixel and parts manipulation --- */

#define INVERTED_DIV_SHIFT 18
#define INVERTED_DIV_ROUNDING (1U << (INVERTED_DIV_SHIFT - 1))
#define INVERTED_DIV_ROUNDING_128BPP \
    (((uint64_t) INVERTED_DIV_ROUNDING << 32) | INVERTED_DIV_ROUNDING)

/* This table is used to divide by an integer [1..255] using only a lookup,
 * multiplication and a shift. This is faster than plain division on most
 * architectures.
 *
 * Each entry represents the integer 262144 (1 << 18) divided by the index of
 * the entry. Consequently,
 *
 * (v / i) ~= (v * inverted_div_table [i] + (1 << 17)) >> 18
 *
 * (1 << 17) is added for nearest rounding. It would've been nice to keep
 * this table in uint16_t, but alas, we need the extra bits to get sufficient
 * precision. */
static const uint32_t inverted_div_table [256] =
{
        0, 262144, 131072,  87381,  65536,  52429,  43691,  37449,
    32768,  29127,  26214,  23831,  21845,  20165,  18725,  17476,
    16384,  15420,  14564,  13797,  13107,  12483,  11916,  11398,
    10923,  10486,  10082,   9709,   9362,   9039,   8738,   8456,
     8192,   7944,   7710,   7490,   7282,   7085,   6899,   6722,
     6554,   6394,   6242,   6096,   5958,   5825,   5699,   5578,
     5461,   5350,   5243,   5140,   5041,   4946,   4855,   4766,
     4681,   4599,   4520,   4443,   4369,   4297,   4228,   4161,
     4096,   4033,   3972,   3913,   3855,   3799,   3745,   3692,
     3641,   3591,   3542,   3495,   3449,   3404,   3361,   3318,
     3277,   3236,   3197,   3158,   3121,   3084,   3048,   3013,
     2979,   2945,   2913,   2881,   2849,   2819,   2789,   2759,
     2731,   2703,   2675,   2648,   2621,   2595,   2570,   2545,
     2521,   2497,   2473,   2450,   2427,   2405,   2383,   2362,
     2341,   2320,   2300,   2280,   2260,   2241,   2222,   2203,
     2185,   2166,   2149,   2131,   2114,   2097,   2081,   2064,
     2048,   2032,   2016,   2001,   1986,   1971,   1956,   1942,
     1928,   1913,   1900,   1886,   1872,   1859,   1846,   1833,
     1820,   1808,   1796,   1783,   1771,   1759,   1748,   1736,
     1725,   1713,   1702,   1691,   1680,   1670,   1659,   1649,
     1638,   1628,   1618,   1608,   1598,   1589,   1579,   1570,
     1560,   1551,   1542,   1533,   1524,   1515,   1507,   1498,
     1489,   1481,   1473,   1464,   1456,   1448,   1440,   1432,
     1425,   1417,   1409,   1402,   1394,   1387,   1380,   1372,
     1365,   1358,   1351,   1344,   1337,   1331,   1324,   1317,
     1311,   1304,   1298,   1291,   1285,   1279,   1273,   1266,
     1260,   1254,   1248,   1242,   1237,   1231,   1225,   1219,
     1214,   1208,   1202,   1197,   1192,   1186,   1181,   1176,
     1170,   1165,   1160,   1155,   1150,   1145,   1140,   1135,
     1130,   1125,   1120,   1116,   1111,   1106,   1101,   1097,
     1092,   1088,   1083,   1079,   1074,   1070,   1066,   1061,
     1057,   1053,   1049,   1044,   1040,   1036,   1032,   1028
};

static SMOL_INLINE const uint32_t *
inrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx,
                      uint32_t inrow_ofs)
{
    return scale_ctx->pixels_in + scale_ctx->rowstride_in * inrow_ofs;
}

static SMOL_INLINE uint32_t *
outrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx,
                       uint32_t outrow_ofs)
{
    return scale_ctx->pixels_out + scale_ctx->rowstride_out * outrow_ofs;
}

static SMOL_INLINE uint32_t
pack_pixel_64bpp (uint64_t in)
{
    return in | (in >> 24);
}

static SMOL_INLINE uint64_t
unpack_pixel_64bpp (uint32_t p)
{
    return (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff00ff);
}

static SMOL_INLINE uint32_t
pack_pixel_128bpp (const uint64_t *in)
{
    /* FIXME: Are masks needed? */
    return ((in [0] >> 8) & 0xff000000)
           | ((in [0] << 16) & 0x00ff0000)
           | ((in [1] >> 24) & 0x0000ff00)
           | (in [1] & 0x000000ff);
}

static SMOL_INLINE void
unpack_pixel_128bpp (uint32_t p,
                     uint64_t *out)
{
    uint64_t p64 = p;
    out [0] = ((p64 & 0xff000000) << 8) | ((p64 & 0x00ff0000) >> 16);
    out [1] = ((p64 & 0x0000ff00) << 24) | (p64 & 0x000000ff);
}

/* Masking and shifting out the results is left to the caller. In
 * and out may not overlap. */
static SMOL_INLINE void
unpremul_128bpp (const uint64_t * SMOL_RESTRICT in,
                 uint64_t * SMOL_RESTRICT out,
                 uint8_t alpha)
{
    out [0] = ((in [0] * (uint64_t) inverted_div_table [alpha]
                + INVERTED_DIV_ROUNDING_128BPP) >> INVERTED_DIV_SHIFT);
    out [1] = ((in [1] * (uint64_t) inverted_div_table [alpha]
                + INVERTED_DIV_ROUNDING_128BPP) >> INVERTED_DIV_SHIFT);
}

static SMOL_INLINE uint32_t
pack_pixel_unassoc_xxxa_128bpp (const uint64_t * SMOL_RESTRICT in)
{
    uint8_t alpha = in [1] & 0xff;
    uint64_t t [2];

    unpremul_128bpp (in, t, alpha);

    return ((t [0] >> 8) & 0xff000000)
           | ((t [0] << 16) & 0x00ff0000)
           | ((t [1] >> 24) & 0x0000ff00)
           | alpha;
}

static SMOL_INLINE void
unpack_pixel_unassoc_xxxa_128bpp (uint32_t p,
                                  uint64_t *out)
{
    uint64_t p64 = p;
    uint64_t alpha = p64 & 0xff;

    out [0] = (((((p64 & 0xff000000) << 8) | ((p64 & 0x00ff0000) >> 16)) * alpha));
    out [1] = (((((p64 & 0x0000ff00) << 24) * alpha))) | alpha;
}

static SMOL_INLINE uint64_t
weight_pixel_64bpp (uint64_t p,
                    uint16_t w)
{
    return ((p * w) >> 8) & 0x00ff00ff00ff00ff;
}

/* p and out may be the same address */
static SMOL_INLINE void
weight_pixel_128bpp (uint64_t *p,
                     uint64_t *out,
                     uint16_t w)
{
    out [0] = ((p [0] * w) >> 8) & 0x00ffffff00ffffffULL;
    out [1] = ((p [1] * w) >> 8) & 0x00ffffff00ffffffULL;
}

static SMOL_INLINE void
pack_row_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                uint32_t * SMOL_RESTRICT row_out,
                uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_64bpp (*(row_in++));
    }
}

/* AVX2 has a useful instruction for this: __m256i _mm256_cvtepu8_epi16 (__m128i a);
 * It results in a different channel ordering, so it'd be important to match with
 * the right kind of re-pack. */
static SMOL_INLINE void
unpack_row_64bpp (const uint32_t * SMOL_RESTRICT row_in,
                  uint64_t * SMOL_RESTRICT row_out,
                  uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels;

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_64bpp (*(row_in++));
    }
}

static SMOL_INLINE void
pack_row_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                 uint32_t * SMOL_RESTRICT row_out,
                 uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_128bpp (row_in);
        row_in += 2;
    }
}

static SMOL_INLINE void
unpack_row_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                   uint64_t * SMOL_RESTRICT row_out,
                   uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    while (row_out != row_out_max)
    {
        unpack_pixel_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

static SMOL_INLINE void
pack_row_unassoc_xxxa_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                              uint32_t * SMOL_RESTRICT row_out,
                              uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_unassoc_xxxa_128bpp (row_in);
        row_in += 2;
    }
}

static SMOL_INLINE void
unpack_row_unassoc_xxxa_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                uint64_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    while (row_out != row_out_max)
    {
        unpack_pixel_unassoc_xxxa_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

static SMOL_INLINE void
sum_parts_64bpp (const uint64_t ** SMOL_RESTRICT pp,
                 uint64_t * SMOL_RESTRICT accum,
                 uint32_t n)
{
    const uint64_t *pp_end;

    for (pp_end = *pp + n; *pp < pp_end; (*pp)++)
    {
        *accum += **pp;
    }
}

static SMOL_INLINE void
sum_parts_128bpp (const uint64_t ** SMOL_RESTRICT pp,
                  uint64_t * SMOL_RESTRICT accum,
                  uint32_t n)
{
    const uint64_t *pp_end;

    for (pp_end = *pp + n * 2; *pp < pp_end; )
    {
        accum [0] += *((*pp)++);
        accum [1] += *((*pp)++);
    }
}

static SMOL_INLINE uint64_t
scale_64bpp (uint64_t accum,
             uint64_t multiplier)
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

static SMOL_INLINE uint64_t
scale_128bpp_half (uint64_t accum,
                   uint64_t multiplier)
{
    uint64_t a, b;

    a = accum & 0x00000000ffffffffULL;
    a = (a * multiplier + BOXES_MULTIPLIER / 2) / BOXES_MULTIPLIER;

    b = (accum & 0xffffffff00000000ULL) >> 32;
    b = (b * multiplier + BOXES_MULTIPLIER / 2) / BOXES_MULTIPLIER;

    return (a & 0x000000000000ffffULL)
           | ((b & 0x000000000000ffffULL) << 32);
}

static SMOL_INLINE void
scale_and_store_128bpp (const uint64_t * SMOL_RESTRICT accum,
                        uint64_t multiplier,
                        uint64_t ** SMOL_RESTRICT row_parts_out)
{
    *(*row_parts_out)++ = scale_128bpp_half (accum [0], multiplier);
    *(*row_parts_out)++ = scale_128bpp_half (accum [1], multiplier);
}

static void
add_parts (const uint64_t * SMOL_RESTRICT parts_in,
           uint64_t * SMOL_RESTRICT parts_acc_out,
           uint32_t n)
{
    const uint64_t *parts_in_max = parts_in + n;

    while (parts_in < parts_in_max)
        *(parts_acc_out++) += *(parts_in++);
}

/* --- Precalculation --- */

static void
calc_size_steps (uint32_t dim_in,
                 uint32_t dim_out,
                 unsigned int *n_halvings,
                 uint32_t *dim_bilin_out,
                 SmolAlgorithm *algo)
{
    *n_halvings = 0;
    *dim_bilin_out = dim_out;

    /* The box algorithms are only sufficiently precise when
     * dim_in > dim_out * 5. box_64bpp typically starts outperforming
     * bilinear+halving at dim_in > dim_out * 8. */

    if (dim_in > dim_out * 255)
    {
        *algo = ALGORITHM_BOX_128BPP;
    }
    else if (dim_in > dim_out * 8)
    {
        *algo = ALGORITHM_BOX_64BPP;
    }
    else if (dim_in == 1)
    {
        *algo = ALGORITHM_ONE_64BPP;
    }
    else
    {
        uint32_t d = dim_out;

        *algo = ALGORITHM_BILINEAR_0H_64BPP;

        for (;;)
        {
            d *= 2;
            if (d >= dim_in)
                break;
            (*n_halvings)++;
        }
        dim_out <<= *n_halvings;
        *dim_bilin_out = dim_out;
    }
}

static void
precalc_bilinear_array (uint16_t *array,
                        uint32_t dim_in,
                        uint32_t dim_out,
                        unsigned int make_absolute_offsets)
{
    uint64_t ofs_stepF, fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t last_ofs = 0;

    if (dim_in > dim_out)
    {
        /* Minification */
        frac_stepF = ofs_stepF = (dim_in * BILIN_MULTIPLIER) / dim_out;
        fracF = (frac_stepF - BILIN_MULTIPLIER) / 2;
    }
    else
    {
        /* Magnification */
        frac_stepF = ofs_stepF = ((dim_in - 1) * BILIN_MULTIPLIER) / (dim_out > 1 ? (dim_out - 1) : 1);
        fracF = 0;
    }

    do
    {
        uint16_t ofs = fracF / BILIN_MULTIPLIER;

        /* We sample ofs and its neighbor -- prevent out of bounds access
         * for the latter. */
        if (ofs >= dim_in - 1)
            break;

        *(pu16++) = make_absolute_offsets ? ofs : ofs - last_ofs;
        *(pu16++) = SMALL_MUL - ((fracF / (BILIN_MULTIPLIER / SMALL_MUL)) % SMALL_MUL);
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
precalc_boxes_array (uint16_t *array,
                     uint32_t *span_mul,
                     uint32_t dim_in,
                     uint32_t dim_out,
                     unsigned int make_absolute_offsets)
{
    uint64_t fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t ofs, next_ofs;
    uint64_t f;
    uint64_t stride;
    uint64_t a, b;

    frac_stepF = ((uint64_t) dim_in * BIG_MUL) / (uint64_t) dim_out;
    fracF = 0;
    ofs = 0;

    stride = frac_stepF / (uint64_t) BIG_MUL;
    f = (frac_stepF / SMALL_MUL) % SMALL_MUL;

    a = (BOXES_MULTIPLIER * 255);
    b = ((stride * 255) + ((f * 255) / 256));
    *span_mul = (a + (b / 2)) / b;

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
interp_horizontal_bilinear_##n_halvings##h_64bpp (const SmolScaleCtx *scale_ctx, \
                                                  const uint64_t * SMOL_RESTRICT row_parts_in, \
                                                  uint64_t * SMOL_RESTRICT row_parts_out) \
{                                                                       \
    uint64_t p, q;                                                      \
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t F;                                                         \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out; \
    int i;                                                              \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t accum = 0;                                             \
                                                                        \
        for (i = 0; i < (1 << (n_halvings)); i++)                       \
        {                                                               \
            row_parts_in += *(ofs_x++);                                 \
            F = *(ofs_x++);                                             \
                                                                        \
            p = *row_parts_in;                                          \
            q = *(row_parts_in + 1);                                    \
                                                                        \
            accum += ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL; \
        }                                                               \
        *(row_parts_out++) = ((accum) >> (n_halvings)) & 0x00ff00ff00ff00ffULL; \
    }                                                                   \
    while (row_parts_out != row_parts_out_max);                         \
}                                                                       \
                                                                        \
static void                                                             \
interp_horizontal_bilinear_##n_halvings##h_128bpp (const SmolScaleCtx *scale_ctx, \
                                                   const uint64_t * SMOL_RESTRICT row_parts_in, \
                                                   uint64_t * SMOL_RESTRICT row_parts_out) \
{                                                                       \
    uint64_t p, q;                                                      \
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t F;                                                         \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out * 2; \
    int i;                                                              \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t accum [2] = { 0 };                                     \
                                                                        \
        for (i = 0; i < (1 << (n_halvings)); i++)                       \
        {                                                               \
            row_parts_in += *(ofs_x++) * 2;                             \
            F = *(ofs_x++);                                             \
                                                                        \
            p = row_parts_in [0];                                       \
            q = row_parts_in [2];                                       \
                                                                        \
            accum [0] += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL; \
                                                                        \
            p = row_parts_in [1];                                       \
            q = row_parts_in [3];                                       \
                                                                        \
            accum [1] += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL; \
        }                                                               \
        *(row_parts_out++) = ((accum [0]) >> (n_halvings)) & 0x00ffffff00ffffffULL; \
        *(row_parts_out++) = ((accum [1]) >> (n_halvings)) & 0x00ffffff00ffffffULL; \
    }                                                                   \
    while (row_parts_out != row_parts_out_max);                         \
}

static void
interp_horizontal_bilinear_0h_64bpp (const SmolScaleCtx *scale_ctx,
                                     const uint64_t * SMOL_RESTRICT row_parts_in,
                                     uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t p, q;
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t F;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out;

    do
    {
        row_parts_in += *(ofs_x++);
        F = *(ofs_x++);

        p = *row_parts_in;
        q = *(row_parts_in + 1);

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (row_parts_out != row_parts_out_max);
}

static void
interp_horizontal_bilinear_0h_128bpp (const SmolScaleCtx *scale_ctx,
                                      const uint64_t * SMOL_RESTRICT row_parts_in,
                                      uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t p, q;
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t F;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out * 2;

    do
    {
        row_parts_in += *(ofs_x++) * 2;
        F = *(ofs_x++);

        p = row_parts_in [0];
        q = row_parts_in [2];

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;

        p = row_parts_in [1];
        q = row_parts_in [3];

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
    while (row_parts_out != row_parts_out_max);
}

DEF_INTERP_HORIZONTAL_BILINEAR(1)
DEF_INTERP_HORIZONTAL_BILINEAR(2)
DEF_INTERP_HORIZONTAL_BILINEAR(3)
DEF_INTERP_HORIZONTAL_BILINEAR(4)
DEF_INTERP_HORIZONTAL_BILINEAR(5)
DEF_INTERP_HORIZONTAL_BILINEAR(6)

static void
interp_horizontal_boxes_64bpp (const SmolScaleCtx *scale_ctx,
                               const uint64_t *row_parts_in,
                               uint64_t * SMOL_RESTRICT row_parts_out)
{
    const uint64_t * SMOL_RESTRICT pp;
    const uint16_t *ofs_x = scale_ctx->offsets_x;
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out - 1;
    uint64_t accum = 0;
    uint64_t p, q, r, s;
    uint32_t n;
    uint64_t F;

    pp = row_parts_in;
    p = weight_pixel_64bpp (*(pp++), 256);
    n = *(ofs_x++);

    while (row_parts_out != row_parts_out_max)
    {
        sum_parts_64bpp ((const uint64_t ** SMOL_RESTRICT) &pp, &accum, n);

        F = *(ofs_x++);
        n = *(ofs_x++);

        r = *(pp++);
        s = r * F;

        q = (s >> 8) & 0x00ff00ff00ff00ffULL;

        accum += p + q;

        /* (255 * r) - (F * r) */
        p = (((r << 8) - r - s) >> 8) & 0x00ff00ff00ff00ffULL;

        *(row_parts_out++) = scale_64bpp (accum, scale_ctx->span_mul_x);
        accum = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_parts_64bpp ((const uint64_t ** SMOL_RESTRICT) &pp, &accum, n);

    q = 0;
    F = *(ofs_x);
    if (F > 0)
        q = weight_pixel_64bpp (*(pp), F);

    accum += p + q;
    *(row_parts_out++) = scale_64bpp (accum, scale_ctx->span_mul_x);
}

static void
interp_horizontal_boxes_128bpp (const SmolScaleCtx *scale_ctx,
                                const uint64_t *row_parts_in,
                                uint64_t * SMOL_RESTRICT row_parts_out)
{
    const uint64_t * SMOL_RESTRICT pp;
    const uint16_t *ofs_x = scale_ctx->offsets_x;
    uint64_t *row_parts_out_max = row_parts_out + (scale_ctx->width_out - /* 2 */ 1) * 2;
    uint64_t accum [2] = { 0, 0 };
    uint64_t p [2], q [2], r [2], s [2];
    uint32_t n;
    uint64_t F;

    pp = row_parts_in;

    p [0] = *(pp++);
    p [1] = *(pp++);
    weight_pixel_128bpp (p, p, 256);

    n = *(ofs_x++);

    while (row_parts_out != row_parts_out_max)
    {
        sum_parts_128bpp ((const uint64_t ** SMOL_RESTRICT) &pp, accum, n);

        F = *(ofs_x++);
        n = *(ofs_x++);

        r [0] = *(pp++);
        r [1] = *(pp++);

        s [0] = r [0] * F;
        s [1] = r [1] * F;

        q [0] = (s [0] >> 8) & 0x00ffffff00ffffff;
        q [1] = (s [1] >> 8) & 0x00ffffff00ffffff;

        accum [0] += p [0] + q [0];
        accum [1] += p [1] + q [1];

        p [0] = (((r [0] << 8) - r [0] - s [0]) >> 8) & 0x00ffffff00ffffff;
        p [1] = (((r [1] << 8) - r [1] - s [1]) >> 8) & 0x00ffffff00ffffff;

        scale_and_store_128bpp (accum,
                                scale_ctx->span_mul_x,
                                (uint64_t ** SMOL_RESTRICT) &row_parts_out);

        accum [0] = 0;
        accum [1] = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_parts_128bpp ((const uint64_t ** SMOL_RESTRICT) &pp, accum, n);

    q [0] = 0;
    q [1] = 0;

    F = *(ofs_x);
    if (F > 0)
    {
        q [0] = *(pp++);
        q [1] = *(pp++);
        weight_pixel_128bpp (q, q, F);
    }

    accum [0] += p [0] + q [0];
    accum [1] += p [1] + q [1];

    scale_and_store_128bpp (accum,
                            scale_ctx->span_mul_x,
                            (uint64_t ** SMOL_RESTRICT) &row_parts_out);
}

static void
interp_horizontal_one_64bpp (const SmolScaleCtx *scale_ctx,
                             const uint64_t * SMOL_RESTRICT row_parts_in,
                             uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out;
    uint64_t part;

    part = *row_parts_in;
    while (row_parts_out != row_parts_out_max)
        *(row_parts_out++) = part;
}

static void
interp_horizontal_one_128bpp (const SmolScaleCtx *scale_ctx,
                              const uint64_t * SMOL_RESTRICT row_parts_in,
                              uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out * 2;

    while (row_parts_out != row_parts_out_max)
    {
        *(row_parts_out++) = row_parts_in [0];
        *(row_parts_out++) = row_parts_in [1];
    }
}

static void
scale_horizontal (const SmolScaleCtx *scale_ctx,
                  const uint32_t *row_in,
                  uint64_t *row_parts_out)
{
    uint64_t * SMOL_RESTRICT unpacked_in;

    /* FIXME: Allocate less for 64bpp */
    unpacked_in = aligned_alloca (scale_ctx->width_in * sizeof (uint64_t) * 2, 64);

    scale_ctx->unpack_row_func (row_in,
                                unpacked_in,
                                scale_ctx->width_in);
    scale_ctx->hfilter_func (scale_ctx,
                             unpacked_in,
                             row_parts_out);
}

/* --- Vertical scaling --- */

static void
update_vertical_ctx_bilinear (const SmolScaleCtx *scale_ctx,
                              SmolVerticalCtx *vertical_ctx,
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

        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                          vertical_ctx->parts_row [1]);
    }
    else
    {
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs),
                          vertical_ctx->parts_row [0]);
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                          vertical_ctx->parts_row [1]);
    }

    vertical_ctx->in_ofs = new_in_ofs;
}

static void
interp_vertical_bilinear_store_64bpp (uint64_t F,
                                      const uint64_t * SMOL_RESTRICT SMOL_ALIGNED_64 top_row_parts_in,
                                      const uint64_t * SMOL_RESTRICT SMOL_ALIGNED_64 bottom_row_parts_in,
                                      uint64_t * SMOL_RESTRICT SMOL_ALIGNED_64 parts_out,
                                      uint32_t width)
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
interp_vertical_bilinear_add_64bpp (uint64_t F,
                                    const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                    const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                    uint64_t * SMOL_RESTRICT accum_out,
                                    uint32_t width)
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

static void
interp_vertical_bilinear_store_128bpp (uint64_t F,
                                       const uint64_t * SMOL_RESTRICT SMOL_ALIGNED_64 top_row_parts_in,
                                       const uint64_t * SMOL_RESTRICT SMOL_ALIGNED_64 bottom_row_parts_in,
                                       uint64_t * SMOL_RESTRICT SMOL_ALIGNED_64 parts_out,
                                       uint32_t width)
{
    uint64_t *parts_out_last = parts_out + width;

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
    while (parts_out != parts_out_last);
}

static void
interp_vertical_bilinear_add_128bpp (uint64_t F,
                                     const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                     const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                     uint64_t * SMOL_RESTRICT accum_out,
                                     uint32_t width)
{
    uint64_t *accum_out_last = accum_out + width;

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(accum_out++) += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
    while (accum_out != accum_out_last);
}

#define DEF_INTERP_VERTICAL_BILINEAR_FINAL(n_halvings)                  \
static void                                                             \
interp_vertical_bilinear_final_##n_halvings##h_64bpp (uint64_t F,                \
                                                      const uint64_t * SMOL_RESTRICT top_row_parts_in, \
                                                      const uint64_t * SMOL_RESTRICT bottom_row_parts_in, \
                                                      uint64_t * SMOL_RESTRICT accum_inout, \
                                                      uint32_t width)   \
{                                                                       \
    uint64_t *accum_inout_last = accum_inout + width;                   \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t p, q;                                                  \
                                                                        \
        p = *(top_row_parts_in++);                                      \
        q = *(bottom_row_parts_in++);                                   \
                                                                        \
        p = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;         \
        p = ((p + *accum_inout) >> n_halvings) & 0x00ff00ff00ff00ffULL; \
                                                                        \
        *(accum_inout++) = p;                                           \
    }                                                                   \
    while (accum_inout != accum_inout_last);                            \
}                                                                       \
                                                                        \
static void                                                             \
interp_vertical_bilinear_final_##n_halvings##h_128bpp (uint64_t F,      \
                                                       const uint64_t * SMOL_RESTRICT top_row_parts_in, \
                                                       const uint64_t * SMOL_RESTRICT bottom_row_parts_in, \
                                                       uint64_t * SMOL_RESTRICT accum_inout, \
                                                       uint32_t width)  \
{                                                                       \
    uint64_t *accum_inout_last = accum_inout + width;                   \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t p, q;                                                  \
                                                                        \
        p = *(top_row_parts_in++);                                      \
        q = *(bottom_row_parts_in++);                                   \
                                                                        \
        p = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;         \
        p = ((p + *accum_inout) >> n_halvings) & 0x00ffffff00ffffffULL; \
                                                                        \
        *(accum_inout++) = p;                                           \
    }                                                                   \
    while (accum_inout != accum_inout_last);                            \
}

#define DEF_SCALE_OUTROW_BILINEAR(n_halvings)                           \
static void                                                             \
scale_outrow_bilinear_##n_halvings##h_64bpp (const SmolScaleCtx *scale_ctx, \
                                             SmolVerticalCtx *vertical_ctx, \
                                             uint32_t outrow_index,     \
                                             uint32_t *row_out)         \
{                                                                       \
    uint32_t bilin_index = outrow_index << (n_halvings);                \
    unsigned int i;                                                     \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_store_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                          vertical_ctx->parts_row [0],  \
                                          vertical_ctx->parts_row [1],  \
                                          vertical_ctx->parts_row [2],  \
                                          scale_ctx->width_out);        \
    bilin_index++;                                                      \
                                                                        \
    for (i = 0; i < (1 << (n_halvings)) - 2; i++)                       \
    {                                                                   \
        update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
        interp_vertical_bilinear_add_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                            vertical_ctx->parts_row [0], \
                                            vertical_ctx->parts_row [1], \
                                            vertical_ctx->parts_row [2], \
                                            scale_ctx->width_out);      \
        bilin_index++;                                                  \
    }                                                                   \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_final_##n_halvings##h_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                                          vertical_ctx->parts_row [0], \
                                                          vertical_ctx->parts_row [1], \
                                                          vertical_ctx->parts_row [2], \
                                                          scale_ctx->width_out); \
                                                                        \
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out); \
}                                                                       \
                                                                        \
static void                                                             \
scale_outrow_bilinear_##n_halvings##h_128bpp (const SmolScaleCtx *scale_ctx, \
                                              SmolVerticalCtx *vertical_ctx, \
                                              uint32_t outrow_index,    \
                                              uint32_t *row_out)        \
{                                                                       \
    uint32_t bilin_index = outrow_index << (n_halvings);                \
    unsigned int i;                                                     \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_store_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                           vertical_ctx->parts_row [0], \
                                           vertical_ctx->parts_row [1], \
                                           vertical_ctx->parts_row [2], \
                                           scale_ctx->width_out * 2);   \
    bilin_index++;                                                      \
                                                                        \
    for (i = 0; i < (1 << (n_halvings)) - 2; i++)                       \
    {                                                                   \
        update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
        interp_vertical_bilinear_add_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                             vertical_ctx->parts_row [0], \
                                             vertical_ctx->parts_row [1], \
                                             vertical_ctx->parts_row [2], \
                                             scale_ctx->width_out * 2); \
        bilin_index++;                                                  \
    }                                                                   \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_final_##n_halvings##h_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                                           vertical_ctx->parts_row [0], \
                                                           vertical_ctx->parts_row [1], \
                                                           vertical_ctx->parts_row [2], \
                                                           scale_ctx->width_out * 2); \
                                                                        \
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out); \
}

static void
scale_outrow_bilinear_0h_64bpp (const SmolScaleCtx *scale_ctx,
                                SmolVerticalCtx *vertical_ctx,
                                uint32_t outrow_index,
                                uint32_t *row_out)
{
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, outrow_index);
    interp_vertical_bilinear_store_64bpp (scale_ctx->offsets_y [outrow_index * 2 + 1],
                                          vertical_ctx->parts_row [0],
                                          vertical_ctx->parts_row [1],
                                          vertical_ctx->parts_row [2],
                                          scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

static void
scale_outrow_bilinear_0h_128bpp (const SmolScaleCtx *scale_ctx,
                                 SmolVerticalCtx *vertical_ctx,
                                 uint32_t outrow_index,
                                 uint32_t *row_out)
{
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, outrow_index);
    interp_vertical_bilinear_store_128bpp (scale_ctx->offsets_y [outrow_index * 2 + 1],
                                           vertical_ctx->parts_row [0],
                                           vertical_ctx->parts_row [1],
                                           vertical_ctx->parts_row [2],
                                           scale_ctx->width_out * 2);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

DEF_INTERP_VERTICAL_BILINEAR_FINAL(1)

static void
scale_outrow_bilinear_1h_64bpp (const SmolScaleCtx *scale_ctx,
                                SmolVerticalCtx *vertical_ctx,
                                uint32_t outrow_index,
                                uint32_t *row_out)
{
    uint32_t bilin_index = outrow_index << 1;

    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_store_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                          vertical_ctx->parts_row [0],
                                          vertical_ctx->parts_row [1],
                                          vertical_ctx->parts_row [2],
                                          scale_ctx->width_out);
    bilin_index++;
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_final_1h_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                             vertical_ctx->parts_row [0],
                                             vertical_ctx->parts_row [1],
                                             vertical_ctx->parts_row [2],
                                             scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

static void
scale_outrow_bilinear_1h_128bpp (const SmolScaleCtx *scale_ctx,
                                 SmolVerticalCtx *vertical_ctx,
                                 uint32_t outrow_index,
                                 uint32_t *row_out)
{
    uint32_t bilin_index = outrow_index << 1;

    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_store_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                           vertical_ctx->parts_row [0],
                                           vertical_ctx->parts_row [1],
                                           vertical_ctx->parts_row [2],
                                           scale_ctx->width_out * 2);
    bilin_index++;
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_final_1h_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                              vertical_ctx->parts_row [0],
                                              vertical_ctx->parts_row [1],
                                              vertical_ctx->parts_row [2],
                                              scale_ctx->width_out * 2);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
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

static void
finalize_vertical_64bpp (const uint64_t * SMOL_RESTRICT accums,
                         uint64_t multiplier,
                         uint64_t * SMOL_RESTRICT parts_out,
                         uint32_t n)
{
    uint64_t *parts_out_max = parts_out + n;

    while (parts_out != parts_out_max)
    {
        *(parts_out++) = scale_64bpp (*(accums++), multiplier);
    }
}

static void
weight_edge_row_64bpp (uint64_t *row,
                       uint16_t w,
                       uint32_t n)
{
    uint64_t *row_max = row + n;

    while (row != row_max)
    {
        *row = ((*row * w) >> 8) & 0x00ff00ff00ff00ffULL;
        row++;
    }
}

static void
scale_and_weight_edge_rows_box_64bpp (const uint64_t * SMOL_RESTRICT first_row,
                                    uint64_t * SMOL_RESTRICT last_row,
                                    uint64_t * SMOL_RESTRICT accum,
                                    uint16_t w2,
                                    uint32_t n)
{
    const uint64_t *first_row_max = first_row + n;

    while (first_row != first_row_max)
    {
        uint64_t r, s, p, q;

        p = *(first_row++);

        r = *(last_row);
        s = r * w2;
        q = (s >> 8) & 0x00ff00ff00ff00ffULL;
        /* (255 * r) - (F * r) */
        *(last_row++) = (((r << 8) - r - s) >> 8) & 0x00ff00ff00ff00ffULL;

        *(accum++) = p + q;
    }
}

static void
update_vertical_ctx_box_64bpp (const SmolScaleCtx *scale_ctx,
                             SmolVerticalCtx *vertical_ctx,
                             uint32_t ofs_y,
                             uint32_t ofs_y_max,
                             uint16_t w1,
                             uint16_t w2)
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
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [0]);
        weight_edge_row_64bpp (vertical_ctx->parts_row [0], w1, scale_ctx->width_out);
    }

    /* When w2 == 0, the final inrow may be out of bounds. Don't try to access it in
     * that case. */
    if (w2 || ofs_y_max < scale_ctx->height_in)
    {
        scale_horizontal (scale_ctx,
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
scale_outrow_box_64bpp (const SmolScaleCtx *scale_ctx,
                        SmolVerticalCtx *vertical_ctx,
                        uint32_t outrow_index,
                        uint32_t *row_out)
{
    uint32_t ofs_y, ofs_y_max;
    uint16_t w1, w2;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first and last rows, weight them and store in accumulator */

    w1 = (outrow_index == 0) ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1];
    w2 = scale_ctx->offsets_y [outrow_index * 2 + 1];

    update_vertical_ctx_box_64bpp (scale_ctx, vertical_ctx, ofs_y, ofs_y_max, w1, w2);

    scale_and_weight_edge_rows_box_64bpp (vertical_ctx->parts_row [0],
                                          vertical_ctx->parts_row [1],
                                          vertical_ctx->parts_row [2],
                                          w2,
                                          scale_ctx->width_out);

    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [0]);
        add_parts (vertical_ctx->parts_row [0],
                   vertical_ctx->parts_row [2],
                   scale_ctx->width_out);

        ofs_y++;
    }

    finalize_vertical_64bpp (vertical_ctx->parts_row [2],
                             scale_ctx->span_mul_y,
                             vertical_ctx->parts_row [0],
                             scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
finalize_vertical_128bpp (const uint64_t * SMOL_RESTRICT accums,
                          uint64_t multiplier,
                          uint64_t * SMOL_RESTRICT parts_out,
                          uint32_t n)
{
    uint64_t *parts_out_max = parts_out + n * 2;

    while (parts_out != parts_out_max)
    {
        *(parts_out++) = scale_128bpp_half (*(accums++), multiplier);
        *(parts_out++) = scale_128bpp_half (*(accums++), multiplier);
    }
}

static void
weight_row_128bpp (uint64_t *row,
                   uint16_t w,
                   uint32_t n)
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
scale_outrow_box_128bpp (const SmolScaleCtx *scale_ctx,
                         SmolVerticalCtx *vertical_ctx,
                         uint32_t outrow_index,
                         uint32_t *row_out)
{
    uint32_t ofs_y, ofs_y_max;
    uint16_t w;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first inrow and store it */

    scale_horizontal (scale_ctx,
                      inrow_ofs_to_pointer (scale_ctx, ofs_y),
                      vertical_ctx->parts_row [0]);
    weight_row_128bpp (vertical_ctx->parts_row [0],
                       outrow_index == 0 ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1],
                       scale_ctx->width_out);
    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
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
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [1]);
        weight_row_128bpp (vertical_ctx->parts_row [1],
                           w - 1,  /* Subtract 1 to avoid overflow */
                           scale_ctx->width_out);
        add_parts (vertical_ctx->parts_row [1],
                   vertical_ctx->parts_row [0],
                   scale_ctx->width_out * 2);
    }

    finalize_vertical_128bpp (vertical_ctx->parts_row [0],
                              scale_ctx->span_mul_y,
                              vertical_ctx->parts_row [1],
                              scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [1], row_out, scale_ctx->width_out);
}

static void
scale_outrow_one_64bpp (const SmolScaleCtx *scale_ctx,
                        SmolVerticalCtx *vertical_ctx,
                        uint32_t row_index,
                        uint32_t *row_out)
{
    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, 0),
                          vertical_ctx->parts_row [0]);
        vertical_ctx->in_ofs = 0;
    }

    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow_one_128bpp (const SmolScaleCtx *scale_ctx,
                        SmolVerticalCtx *vertical_ctx,
                        uint32_t row_index,
                        uint32_t *row_out)
{
    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal (scale_ctx,
                          inrow_ofs_to_pointer (scale_ctx, 0),
                          vertical_ctx->parts_row [0]);
        vertical_ctx->in_ofs = 0;
    }

    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow (const SmolScaleCtx *scale_ctx,
              SmolVerticalCtx *vertical_ctx,
              uint32_t outrow_index,
              uint32_t *row_out)
{
    scale_ctx->vfilter_func (scale_ctx,
                             vertical_ctx,
                             outrow_index,
                             row_out);
}

static void
do_rows (const SmolScaleCtx *scale_ctx,
         uint32_t row_out_index,
         uint32_t n_rows)
{
    SmolVerticalCtx vertical_ctx = { 0 };
    uint32_t n_parts_per_pixel = 1;
    uint32_t n_stored_rows = 3;
    uint32_t i;

    if (scale_ctx->algo_h > ALGORITHM_64BPP_LAST || scale_ctx->algo_v > ALGORITHM_64BPP_LAST)
        n_parts_per_pixel = 2;

    if (scale_ctx->algo_v == ALGORITHM_ONE_64BPP || scale_ctx->algo_v == ALGORITHM_ONE_128BPP)
        n_stored_rows = 1;

    /* Must be one less, or this test in update_vertical_ctx() will wrap around:
     * if (new_in_ofs == vertical_ctx->in_ofs + 1) { ... } */
    vertical_ctx.in_ofs = UINT_MAX - 1;

    for (i = 0; i < n_stored_rows; i++)
    {
        vertical_ctx.parts_row [i] = aligned_alloca (scale_ctx->width_out * n_parts_per_pixel * sizeof (uint64_t), 64);
    }

    for (i = row_out_index; i < row_out_index + n_rows; i++)
    {
        scale_outrow (scale_ctx, &vertical_ctx, i, outrow_ofs_to_pointer (scale_ctx, i));
    }
}

/* --- API --- */

static SmolHFilterFunc *hfilter_funcs [] =
{
    interp_horizontal_one_64bpp,
    interp_horizontal_bilinear_0h_64bpp,
    interp_horizontal_bilinear_1h_64bpp,
    interp_horizontal_bilinear_2h_64bpp,
    interp_horizontal_bilinear_3h_64bpp,
    interp_horizontal_bilinear_4h_64bpp,
    interp_horizontal_bilinear_5h_64bpp,
    interp_horizontal_bilinear_6h_64bpp,
    interp_horizontal_boxes_64bpp,
    interp_horizontal_one_128bpp,
    interp_horizontal_bilinear_0h_128bpp,
    interp_horizontal_bilinear_1h_128bpp,
    interp_horizontal_bilinear_2h_128bpp,
    interp_horizontal_bilinear_3h_128bpp,
    interp_horizontal_bilinear_4h_128bpp,
    interp_horizontal_bilinear_5h_128bpp,
    interp_horizontal_bilinear_6h_128bpp,
    interp_horizontal_boxes_128bpp,
};

static SmolVFilterFunc *vfilter_funcs [] =
{
    scale_outrow_one_64bpp,
    scale_outrow_bilinear_0h_64bpp,
    scale_outrow_bilinear_1h_64bpp,
    scale_outrow_bilinear_2h_64bpp,
    scale_outrow_bilinear_3h_64bpp,
    scale_outrow_bilinear_4h_64bpp,
    scale_outrow_bilinear_5h_64bpp,
    scale_outrow_bilinear_6h_64bpp,
    scale_outrow_box_64bpp,
    scale_outrow_one_128bpp,
    scale_outrow_bilinear_0h_128bpp,
    scale_outrow_bilinear_1h_128bpp,
    scale_outrow_bilinear_2h_128bpp,
    scale_outrow_bilinear_3h_128bpp,
    scale_outrow_bilinear_4h_128bpp,
    scale_outrow_bilinear_5h_128bpp,
    scale_outrow_bilinear_6h_128bpp,
    scale_outrow_box_128bpp,
};

void
smol_scale_init (SmolScaleCtx *scale_ctx,
                 const uint32_t *pixels_in,
                 uint32_t width_in,
                 uint32_t height_in,
                 uint32_t rowstride_in,
                 uint32_t *pixels_out,
                 uint32_t width_out,
                 uint32_t height_out,
                 uint32_t rowstride_out)
{
    int bpp = 64;

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

    if (scale_ctx->algo_h == ALGORITHM_BILINEAR_0H_64BPP)
    {
        precalc_bilinear_array (scale_ctx->offsets_x,
                                width_in, scale_ctx->width_bilin_out, FALSE);
    }
    else if (scale_ctx->algo_h != ALGORITHM_ONE_64BPP)
    {
        precalc_boxes_array (scale_ctx->offsets_x, &scale_ctx->span_mul_x,
                             width_in, scale_ctx->width_out, FALSE);
    }

    if (scale_ctx->algo_v == ALGORITHM_BILINEAR_0H_64BPP)
    {
        precalc_bilinear_array (scale_ctx->offsets_y,
                                height_in, scale_ctx->height_bilin_out, TRUE);
    }
    else if (scale_ctx->algo_v != ALGORITHM_ONE_64BPP)
    {
        precalc_boxes_array (scale_ctx->offsets_y, &scale_ctx->span_mul_y,
                             height_in, scale_ctx->height_out, TRUE);
    }

    if (scale_ctx->algo_h == ALGORITHM_BOX_128BPP
        || scale_ctx->algo_v == ALGORITHM_BOX_128BPP)
    {
        bpp = 128;
    }

    if (bpp == 64)
    {
        scale_ctx->unpack_row_func = unpack_row_64bpp;
        scale_ctx->pack_row_func = pack_row_64bpp;
    }
    else
    {
        scale_ctx->unpack_row_func = unpack_row_128bpp;
        scale_ctx->pack_row_func = pack_row_128bpp;

        if (scale_ctx->algo_h <= ALGORITHM_64BPP_LAST)
            scale_ctx->algo_h += ALGORITHM_64BPP_LAST + 1;
        if (scale_ctx->algo_v <= ALGORITHM_64BPP_LAST)
            scale_ctx->algo_v += ALGORITHM_64BPP_LAST + 1;
    }

    scale_ctx->hfilter_func = hfilter_funcs [scale_ctx->algo_h + scale_ctx->width_halvings];
    scale_ctx->vfilter_func = vfilter_funcs [scale_ctx->algo_v + scale_ctx->height_halvings];
}

void
smol_scale_finalize (SmolScaleCtx *scale_ctx)
{
    free (scale_ctx->offsets_x);
}

void
smol_scale_simple (const uint32_t *pixels_in,
                   uint32_t width_in,
                   uint32_t height_in,
                   uint32_t rowstride_in,
                   uint32_t *pixels_out,
                   uint32_t width_out,
                   uint32_t height_out,
                   uint32_t rowstride_out)
{
    SmolScaleCtx scale_ctx;

    smol_scale_init (&scale_ctx,
                     pixels_in, width_in, height_in, rowstride_in,
                     pixels_out, width_out, height_out, rowstride_out);
    do_rows (&scale_ctx, 0, scale_ctx.height_out);
    smol_scale_finalize (&scale_ctx);
}

void
smol_scale_batch (const SmolScaleCtx *scale_ctx,
                  uint32_t first_out_row,
                  uint32_t n_out_rows)
{
    do_rows (scale_ctx, first_out_row, n_out_rows);
}
