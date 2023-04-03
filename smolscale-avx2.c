/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019-2023 Hans Petter Jansson. See COPYING for details. */

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc, free, alloca */
#include <stddef.h> /* ptrdiff_t */
#include <string.h> /* memset */
#include <limits.h>
#include <immintrin.h>
#include "smolscale-private.h"

/* ---------------------- *
 * Context initialization *
 * ---------------------- */

#define BILIN_HORIZ_BATCH_PIXELS 16

static uint32_t
array_offset_offset (uint32_t elem_i)
{
    return (elem_i / (BILIN_HORIZ_BATCH_PIXELS)) * (BILIN_HORIZ_BATCH_PIXELS * 2)
        + (elem_i % BILIN_HORIZ_BATCH_PIXELS);
}

static uint32_t
array_offset_factor (uint32_t elem_i)
{
    return (elem_i / (BILIN_HORIZ_BATCH_PIXELS)) * (BILIN_HORIZ_BATCH_PIXELS * 2)
        + BILIN_HORIZ_BATCH_PIXELS
        + ((elem_i & ~2U) % (BILIN_HORIZ_BATCH_PIXELS))
        - ((elem_i % (BILIN_HORIZ_BATCH_PIXELS) / 4)) * 2
        + (elem_i & 2) * (BILIN_HORIZ_BATCH_PIXELS / 4);
}

/* Precalc array layout:
 *
 * |16xu16: Offsets |16xu16: Factors |16xu16: Offsets |16xu16: Factors |
 * |................|................|................|................| ...
 *
 * Offsets layout: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
 * Factors layout: 0 1 4 5 8 9 12 13 2 3 6 7 10 11 14 15
 */

static void
precalc_bilinear_array (uint16_t *array,
                        uint32_t dim_in,
                        uint32_t dim_out,
                        unsigned int make_absolute_offsets,
                        unsigned int do_batches)
{
    uint64_t ofs_stepF, fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t last_ofs = 0;
    uint32_t i = 0;

    if (dim_in > dim_out)
    {
        /* Minification */
        frac_stepF = ofs_stepF = (dim_in * SMOL_BILIN_MULTIPLIER) / dim_out;
        fracF = (frac_stepF - SMOL_BILIN_MULTIPLIER) / 2;
    }
    else
    {
        /* Magnification */
        frac_stepF = ofs_stepF = ((dim_in - 1) * SMOL_BILIN_MULTIPLIER)
            / (dim_out > 1 ? (dim_out - 1) : 1);
        fracF = 0;
    }

    if (do_batches)
    {
        while (dim_out >= BILIN_HORIZ_BATCH_PIXELS)
        {
            uint32_t j;

            for (j = 0; j < BILIN_HORIZ_BATCH_PIXELS; j++)
            {
                uint16_t ofs = fracF / SMOL_BILIN_MULTIPLIER;

                /* We sample ofs and its neighbor -- prevent out of bounds access
                 * for the latter by sampling the final pixel at 100%. */
                if (ofs >= dim_in - 1)
                {
                    array [array_offset_offset (i)] = make_absolute_offsets ? dim_in - 2 : (dim_in - 2) - last_ofs;
                    array [array_offset_factor (i)] = 0;
                    last_ofs = dim_in - 2;
                }
                else
                {
                    array [array_offset_offset (i)] = make_absolute_offsets ? ofs : ofs - last_ofs;
                    array [array_offset_factor (i)] = SMOL_SMALL_MUL - ((fracF / (SMOL_BILIN_MULTIPLIER / SMOL_SMALL_MUL))
                                                                        % SMOL_SMALL_MUL);
                    fracF += frac_stepF;
                    last_ofs = ofs;
                }

                i++;
                dim_out--;
            }
        }

        i = (i / BILIN_HORIZ_BATCH_PIXELS) * (BILIN_HORIZ_BATCH_PIXELS * 2);
    }

    while (dim_out)
    {
        uint16_t ofs = fracF / SMOL_BILIN_MULTIPLIER;

        if (ofs >= dim_in - 1)
        {
            array [i++] = make_absolute_offsets ? dim_in - 2 : (dim_in - 2) - last_ofs;
            array [i++] = 0;
            last_ofs = dim_in - 2;
        }
        else
        {
            array [i++] = make_absolute_offsets ? ofs : ofs - last_ofs;
            array [i++] = SMOL_SMALL_MUL - ((fracF / (SMOL_BILIN_MULTIPLIER / SMOL_SMALL_MUL))
                                            % SMOL_SMALL_MUL);
            fracF += frac_stepF;
            last_ofs = ofs;
        }

        dim_out--;
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

    frac_stepF = ((uint64_t) dim_in * SMOL_BIG_MUL) / (uint64_t) dim_out;
    fracF = 0;
    ofs = 0;

    stride = frac_stepF / (uint64_t) SMOL_BIG_MUL;
    f = (frac_stepF / SMOL_SMALL_MUL) % SMOL_SMALL_MUL;

    a = (SMOL_BOXES_MULTIPLIER * 255);
    b = ((stride * 255) + ((f * 255) / 256));
    *span_mul = (a + (b / 2)) / b;

    do
    {
        fracF += frac_stepF;
        next_ofs = (uint64_t) fracF / ((uint64_t) SMOL_BIG_MUL);

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
        f = (fracF / SMOL_SMALL_MUL) % SMOL_SMALL_MUL;

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

static void
init_horizontal (SmolScaleCtx *scale_ctx)
{
    if (scale_ctx->filter_h == SMOL_FILTER_ONE
        || scale_ctx->filter_h == SMOL_FILTER_COPY)
    {
    }
    else if (scale_ctx->filter_h == SMOL_FILTER_BOX)
    {
        precalc_boxes_array (scale_ctx->offsets_x, &scale_ctx->span_mul_x,
                             scale_ctx->width_in, scale_ctx->width_out,
                             FALSE);
    }
    else if (scale_ctx->filter_h == SMOL_FILTER_BILINEAR_0H
             && scale_ctx->storage_type == SMOL_STORAGE_64BPP)
    {
        precalc_bilinear_array (scale_ctx->offsets_x,
                                scale_ctx->width_in,
                                scale_ctx->width_bilin_out,
                                TRUE, TRUE);
    }
    else /* SMOL_FILTER_BILINEAR_?H */
    {
        precalc_bilinear_array (scale_ctx->offsets_x,
                                scale_ctx->width_in,
                                scale_ctx->width_bilin_out,
                                FALSE, FALSE);
    }
}

static void
init_vertical (SmolScaleCtx *scale_ctx)
{
    if (scale_ctx->filter_v == SMOL_FILTER_ONE
        || scale_ctx->filter_v == SMOL_FILTER_COPY)
    {
    }
    else if (scale_ctx->filter_v == SMOL_FILTER_BOX)
    {
        precalc_boxes_array (scale_ctx->offsets_y, &scale_ctx->span_mul_y,
                             scale_ctx->height_in, scale_ctx->height_out,
                             TRUE);
    }
    else /* SMOL_FILTER_BILINEAR_?H */
    {
        precalc_bilinear_array (scale_ctx->offsets_y,
                                scale_ctx->height_in,
                                scale_ctx->height_bilin_out,
                                TRUE, FALSE);
    }
}

/* ----------------- *
 * Premultiplication *
 * ----------------- */

static SMOL_INLINE void
premul_u_to_p8_128bpp (uint64_t * SMOL_RESTRICT inout,
                       uint8_t alpha)
{
    inout [0] = (((inout [0] + 0x0000000100000001) * ((uint16_t) alpha + 1) - 0x0000000100000001)
                 >> 8) & 0x000000ff000000ff;
    inout [1] = (((inout [1] + 0x0000000100000001) * ((uint16_t) alpha + 1) - 0x0000000100000001)
                 >> 8) & 0x000000ff000000ff;
}

static SMOL_INLINE void
unpremul_p8_to_u_128bpp (const uint64_t *in,
                         uint64_t *out,
                         uint8_t alpha)
{
    out [0] = ((in [0] * _smol_inv_div_p8_lut [alpha])
               >> INVERTED_DIV_SHIFT_P8) & 0x000000ff000000ff;
    out [1] = ((in [1] * _smol_inv_div_p8_lut [alpha])
               >> INVERTED_DIV_SHIFT_P8) & 0x000000ff000000ff;
}

static SMOL_INLINE uint64_t
premul_u_to_p8_64bpp (const uint64_t in,
                      uint8_t alpha)
{
    return (((in + 0x0001000100010001) * ((uint16_t) alpha + 1) - 0x0001000100010001)
            >> 8) & 0x00ff00ff00ff00ff;
}

static SMOL_INLINE uint64_t
unpremul_p8_to_u_64bpp (const uint64_t in,
                        uint8_t alpha)
{
    uint64_t in_128bpp [2];
    uint64_t out_128bpp [2];

    in_128bpp [0] = (in & 0x000000ff000000ff);
    in_128bpp [1] = (in & 0x00ff000000ff0000) >> 16;

    unpremul_p8_to_u_128bpp (in_128bpp, out_128bpp, alpha);

    return out_128bpp [0] | (out_128bpp [1] << 16);
}

static SMOL_INLINE void
premul_u_to_p16_128bpp (uint64_t *inout,
                        uint8_t alpha)
{
    inout [0] = inout [0] * alpha;
    inout [1] = inout [1] * alpha;
}

static SMOL_INLINE void
unpremul_p16_to_u_128bpp (const uint64_t * SMOL_RESTRICT in,
                          uint64_t * SMOL_RESTRICT out,
                          uint8_t alpha)
{
    out [0] = ((in [0] * _smol_inv_div_p16_lut [alpha])
               >> INVERTED_DIV_SHIFT_P16) & 0x000000ff000000ffULL;
    out [1] = ((in [1] * _smol_inv_div_p16_lut [alpha])
               >> INVERTED_DIV_SHIFT_P16) & 0x000000ff000000ffULL;
}

/* --------- *
 * Repacking *
 * --------- */

/* PACK_SHUF_MM256_EPI8_32_TO_128() 
 *
 * Generates a shuffling register for packing 8bpc pixel channels in the
 * provided order. The order (1, 2, 3, 4) is neutral and corresponds to
 *
 * _mm256_set_epi8 (13,12,15,14, 9,8,11,10, 5,4,7,6, 1,0,3,2,
 *                  13,12,15,14, 9,8,11,10, 5,4,7,6, 1,0,3,2);
 */
#define SHUF_ORDER_32_TO_128 0x01000302U
#define SHUF_CH_32_TO_128(n) ((char) (SHUF_ORDER_32_TO_128 >> ((4 - (n)) * 8)))
#define SHUF_QUAD_CH_32_TO_128(q, n) (4 * (q) + SHUF_CH_32_TO_128 (n))
#define SHUF_QUAD_32_TO_128(q, a, b, c, d) \
    SHUF_QUAD_CH_32_TO_128 ((q), (a)), \
    SHUF_QUAD_CH_32_TO_128 ((q), (b)), \
    SHUF_QUAD_CH_32_TO_128 ((q), (c)), \
    SHUF_QUAD_CH_32_TO_128 ((q), (d))
#define PACK_SHUF_EPI8_LANE_32_TO_128(a, b, c, d) \
    SHUF_QUAD_32_TO_128 (3, (a), (b), (c), (d)), \
    SHUF_QUAD_32_TO_128 (2, (a), (b), (c), (d)), \
    SHUF_QUAD_32_TO_128 (1, (a), (b), (c), (d)), \
    SHUF_QUAD_32_TO_128 (0, (a), (b), (c), (d))
#define PACK_SHUF_MM256_EPI8_32_TO_128(a, b, c, d) _mm256_set_epi8 ( \
    PACK_SHUF_EPI8_LANE_32_TO_128 ((a), (b), (c), (d)), \
    PACK_SHUF_EPI8_LANE_32_TO_128 ((a), (b), (c), (d)))

/* PACK_SHUF_MM256_EPI8_32_TO_64()
 *
 * 64bpp version. Packs only once, so fewer contortions required. */
#define SHUF_CH_32_TO_64(n) ((char) (4 - (n)))
#define SHUF_QUAD_CH_32_TO_64(q, n) (4 * (q) + SHUF_CH_32_TO_64 (n))
#define SHUF_QUAD_32_TO_64(q, a, b, c, d) \
    SHUF_QUAD_CH_32_TO_64 ((q), (a)), \
    SHUF_QUAD_CH_32_TO_64 ((q), (b)), \
    SHUF_QUAD_CH_32_TO_64 ((q), (c)), \
    SHUF_QUAD_CH_32_TO_64 ((q), (d))
#define PACK_SHUF_EPI8_LANE_32_TO_64(a, b, c, d) \
    SHUF_QUAD_32_TO_64 (3, (a), (b), (c), (d)), \
    SHUF_QUAD_32_TO_64 (2, (a), (b), (c), (d)), \
    SHUF_QUAD_32_TO_64 (1, (a), (b), (c), (d)), \
    SHUF_QUAD_32_TO_64 (0, (a), (b), (c), (d))
#define PACK_SHUF_MM256_EPI8_32_TO_64(a, b, c, d) _mm256_set_epi8 ( \
    PACK_SHUF_EPI8_LANE_32_TO_64 ((a), (b), (c), (d)), \
    PACK_SHUF_EPI8_LANE_32_TO_64 ((a), (b), (c), (d)))

/* It's nice to be able to shift by a negative amount */
#define SHIFT_S(in, s) ((s >= 0) ? (in) << (s) : (in) >> -(s))

/* This is kind of bulky (~13 x86 insns), but it's about the same as using
 * unions, and we don't have to worry about endianness. */
#define PACK_FROM_1234_64BPP(in, a, b, c, d)                  \
     ((SHIFT_S ((in), ((a) - 1) * 16 + 8 - 32) & 0xff000000)  \
    | (SHIFT_S ((in), ((b) - 1) * 16 + 8 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in), ((c) - 1) * 16 + 8 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in), ((d) - 1) * 16 + 8 - 56) & 0x000000ff))

#define PACK_FROM_1234_128BPP(in, a, b, c, d)                                         \
     ((SHIFT_S ((in [((a) - 1) >> 1]), (((a) - 1) & 1) * 32 + 24 - 32) & 0xff000000)  \
    | (SHIFT_S ((in [((b) - 1) >> 1]), (((b) - 1) & 1) * 32 + 24 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in [((c) - 1) >> 1]), (((c) - 1) & 1) * 32 + 24 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in [((d) - 1) >> 1]), (((d) - 1) & 1) * 32 + 24 - 56) & 0x000000ff))

#define SWAP_2_AND_3(n) ((n) == 2 ? 3 : (n) == 3 ? 2 : n)

#define PACK_FROM_1324_64BPP(in, a, b, c, d)                               \
     ((SHIFT_S ((in), (SWAP_2_AND_3 (a) - 1) * 16 + 8 - 32) & 0xff000000)  \
    | (SHIFT_S ((in), (SWAP_2_AND_3 (b) - 1) * 16 + 8 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in), (SWAP_2_AND_3 (c) - 1) * 16 + 8 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in), (SWAP_2_AND_3 (d) - 1) * 16 + 8 - 56) & 0x000000ff))

/* ---------------------- *
 * Repacking: 24/32 -> 64 *
 * ---------------------- */

static void
unpack_8x_1234_p8_to_xxxx_p8_64bpp (const uint32_t * SMOL_RESTRICT *in,
                                    uint64_t * SMOL_RESTRICT *out,
                                    uint64_t *out_max,
                                    const __m256i channel_shuf)
{
    const __m256i zero = _mm256_setzero_si256 ();
    const __m256i * SMOL_RESTRICT my_in = (const __m256i * SMOL_RESTRICT) *in;
    __m256i * SMOL_RESTRICT my_out = (__m256i * SMOL_RESTRICT) *out;
    __m256i m0, m1, m2;

    SMOL_ASSUME_ALIGNED (my_out, __m256i * SMOL_RESTRICT);

    while ((ptrdiff_t) (my_out + 2) <= (ptrdiff_t) out_max)
    {
        m0 = _mm256_loadu_si256 (my_in);
        my_in++;

        m0 = _mm256_shuffle_epi8 (m0, channel_shuf);
        m0 = _mm256_permute4x64_epi64 (m0, SMOL_4X2BIT (3, 1, 2, 0));

        m1 = _mm256_unpacklo_epi8 (m0, zero);
        m2 = _mm256_unpackhi_epi8 (m0, zero);

        _mm256_store_si256 (my_out, m1);
        my_out++;
        _mm256_store_si256 (my_out, m2);
        my_out++;
    }

    *out = (uint64_t * SMOL_RESTRICT) my_out;
    *in = (const uint32_t * SMOL_RESTRICT) my_in;
}

static SMOL_INLINE uint64_t
unpack_pixel_123_p8_to_132a_p8_64bpp (const uint8_t *p)
{
    return ((uint64_t) p [0] << 48) | ((uint32_t) p [1] << 16)
        | ((uint64_t) p [2] << 32) | 0xff;
}

SMOL_REPACK_ROW_DEF (123,  24,  8, PREMUL8, COMPRESSED,
                     1324, 64, 64, PREMUL8, COMPRESSED) {
    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_123_p8_to_132a_p8_64bpp (row_in);
        row_in += 3;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE uint64_t
unpack_pixel_1234_p8_to_1324_p8_64bpp (uint32_t p)
{
    return (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff00ff);
}

SMOL_REPACK_ROW_DEF (1234, 32, 32, PREMUL8, COMPRESSED,
                     1324, 64, 64, PREMUL8, COMPRESSED) {
    const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_64 (1, 3, 2, 4);
    unpack_8x_1234_p8_to_xxxx_p8_64bpp (&row_in, &row_out, row_out_max,
                                        channel_shuf);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_1234_p8_to_1324_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE uint64_t
unpack_pixel_1234_p8_to_3241_p8_64bpp (uint32_t p)
{
    return (((uint64_t) p & 0x0000ff00) << 40)
        | (((uint64_t) p & 0x00ff00ff) << 16) | (p >> 24);
}

SMOL_REPACK_ROW_DEF (1234, 32, 32, PREMUL8, COMPRESSED,
                     3241, 64, 64, PREMUL8, COMPRESSED) {
    const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_64 (3, 2, 4, 1);
    unpack_8x_1234_p8_to_xxxx_p8_64bpp (&row_in, &row_out, row_out_max,
                                        channel_shuf);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_1234_p8_to_3241_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE uint64_t
unpack_pixel_1234_p8_to_2431_p8_64bpp (uint32_t p)
{
    uint64_t p64 = p;

    return ((p64 & 0x00ff00ff) << 32) | ((p64 & 0x0000ff00) << 8)
        | ((p64 & 0xff000000) >> 24);
}

SMOL_REPACK_ROW_DEF (1234, 32, 32, PREMUL8, COMPRESSED,
                     2431, 64, 64, PREMUL8, COMPRESSED) {
    const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_64 (2, 4, 3, 1);
    unpack_8x_1234_p8_to_xxxx_p8_64bpp (&row_in, &row_out, row_out_max,
                                        channel_shuf);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_1234_p8_to_2431_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE uint64_t
unpack_pixel_a234_u_to_324a_p8_64bpp (uint32_t p)
{
    uint64_t p64 = (((uint64_t) p & 0x0000ff00) << 40) | (((uint64_t) p & 0x00ff00ff) << 16);
    uint8_t alpha = p >> 24;

    return premul_u_to_p8_64bpp (p64, alpha) | alpha;
}

SMOL_REPACK_ROW_DEF (1234, 32, 32, UNASSOCIATED, COMPRESSED,
                     3241, 64, 64, PREMUL8,      COMPRESSED) {
    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_a234_u_to_324a_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE uint64_t
unpack_pixel_1234_u_to_2431_p8_64bpp (uint32_t p)
{
    uint64_t p64 = (((uint64_t) p & 0x00ff00ff) << 32) | (((uint64_t) p & 0x0000ff00) << 8);
    uint8_t alpha = p >> 24;

    return premul_u_to_p8_64bpp (p64, alpha) | alpha;
}

SMOL_REPACK_ROW_DEF (1234, 32, 32, UNASSOCIATED, COMPRESSED,
                     2431, 64, 64, PREMUL8,      COMPRESSED) {
    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_1234_u_to_2431_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE uint64_t
unpack_pixel_123a_u_to_132a_p8_64bpp (uint32_t p)
{
    uint64_t p64 = (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff0000);
    uint8_t alpha = p & 0xff;

    return premul_u_to_p8_64bpp (p64, alpha) | alpha;
}

SMOL_REPACK_ROW_DEF (1234, 32, 32, UNASSOCIATED, COMPRESSED,
                     1324, 64, 64, PREMUL8,      COMPRESSED) {
    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_123a_u_to_132a_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

/* ----------------------- *
 * Repacking: 24/32 -> 128 *
 * ----------------------- */

static void
unpack_8x_xxxx_u_to_123a_p16_128bpp (const uint32_t * SMOL_RESTRICT *in,
                                     uint64_t * SMOL_RESTRICT *out,
                                     uint64_t *out_max,
                                     const __m256i channel_shuf)
{
    const __m256i zero = _mm256_setzero_si256 ();
    const __m256i factor_shuf = _mm256_set_epi8 (
        -1, 12, -1, -1, -1, 12, -1, 12,  -1, 4, -1, -1, -1, 4, -1, 4,
        -1, 12, -1, -1, -1, 12, -1, 12,  -1, 4, -1, -1, -1, 4, -1, 4);
    const __m256i alpha_mul = _mm256_set_epi16 (
        0, 0x100, 0, 0,  0, 0x100, 0, 0,
        0, 0x100, 0, 0,  0, 0x100, 0, 0);
    const __m256i alpha_add = _mm256_set_epi16 (
        0, 0x80, 0, 0,  0, 0x80, 0, 0,
        0, 0x80, 0, 0,  0, 0x80, 0, 0);
    const __m256i * SMOL_RESTRICT my_in = (const __m256i * SMOL_RESTRICT) *in;
    __m256i * SMOL_RESTRICT my_out = (__m256i * SMOL_RESTRICT) *out;
    __m256i m0, m1, m2, m3, m4, m5, m6;
    __m256i fact1, fact2;

    SMOL_ASSUME_ALIGNED (my_out, __m256i * SMOL_RESTRICT);

    while ((ptrdiff_t) (my_out + 4) <= (ptrdiff_t) out_max)
    {
        m0 = _mm256_loadu_si256 (my_in);
        my_in++;

        m0 = _mm256_shuffle_epi8 (m0, channel_shuf);
        m0 = _mm256_permute4x64_epi64 (m0, SMOL_4X2BIT (3, 1, 2, 0));

        m1 = _mm256_unpacklo_epi8 (m0, zero);
        m2 = _mm256_unpackhi_epi8 (m0, zero);

        fact1 = _mm256_shuffle_epi8 (m1, factor_shuf);
        fact2 = _mm256_shuffle_epi8 (m2, factor_shuf);

        fact1 = _mm256_or_si256 (fact1, alpha_mul);
        fact2 = _mm256_or_si256 (fact2, alpha_mul);

        m1 = _mm256_mullo_epi16 (m1, fact1);
        m2 = _mm256_mullo_epi16 (m2, fact2);

        m1 = _mm256_add_epi16 (m1, alpha_add);
        m2 = _mm256_add_epi16 (m2, alpha_add);

        m1 = _mm256_permute4x64_epi64 (m1, SMOL_4X2BIT (3, 1, 2, 0));
        m2 = _mm256_permute4x64_epi64 (m2, SMOL_4X2BIT (3, 1, 2, 0));

        m3 = _mm256_unpacklo_epi16 (m1, zero);
        m4 = _mm256_unpackhi_epi16 (m1, zero);
        m5 = _mm256_unpacklo_epi16 (m2, zero);
        m6 = _mm256_unpackhi_epi16 (m2, zero);

        _mm256_store_si256 (my_out, m3);
        my_out++;
        _mm256_store_si256 (my_out, m4);
        my_out++;
        _mm256_store_si256 (my_out, m5);
        my_out++;
        _mm256_store_si256 (my_out, m6);
        my_out++;
    }

    *out = (uint64_t * SMOL_RESTRICT) my_out;
    *in = (const uint32_t * SMOL_RESTRICT) my_in;
}

static SMOL_INLINE void
unpack_pixel_123_p8_to_123a_p8_128bpp (const uint8_t *in,
                                       uint64_t *out)
{
    out [0] = ((uint64_t) in [0] << 32) | in [1];
    out [1] = ((uint64_t) in [2] << 32) | 0xff;
}

SMOL_REPACK_ROW_DEF (123,   24,  8, PREMUL8, COMPRESSED,
                     1234, 128, 64, PREMUL8, COMPRESSED) {
    while (row_out != row_out_max)
    {
        unpack_pixel_123_p8_to_123a_p8_128bpp (row_in, row_out);
        row_in += 3;
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE void
unpack_pixel_123a_p8_to_123a_p8_128bpp (uint32_t p,
                                        uint64_t *out)
{
    uint64_t p64 = p;
    out [0] = ((p64 & 0xff000000) << 8) | ((p64 & 0x00ff0000) >> 16);
    out [1] = ((p64 & 0x0000ff00) << 24) | (p64 & 0x000000ff);
}

SMOL_REPACK_ROW_DEF (1234,  32, 32, PREMUL8, COMPRESSED,
                     1234, 128, 64, PREMUL8, COMPRESSED) {
    while (row_out != row_out_max)
    {
        unpack_pixel_123a_p8_to_123a_p8_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE void
unpack_pixel_a234_p8_to_234a_p8_128bpp (uint32_t p,
                                        uint64_t *out)
{
    uint64_t p64 = p;
    out [0] = ((p64 & 0x00ff0000) << 16) | ((p64 & 0x0000ff00) >> 8);
    out [1] = ((p64 & 0x000000ff) << 32) | ((p64 & 0xff000000) >> 24);
}

SMOL_REPACK_ROW_DEF (1234,  32, 32, PREMUL8, COMPRESSED,
                     2341, 128, 64, PREMUL8, COMPRESSED) {
    while (row_out != row_out_max)
    {
        unpack_pixel_a234_p8_to_234a_p8_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE void
unpack_pixel_a234_u_to_234a_p8_128bpp (uint32_t p,
                                       uint64_t *out)
{
    uint64_t p64 = (((uint64_t) p & 0x00ff00ff) << 32) | (((uint64_t) p & 0x0000ff00) << 8);
    uint8_t alpha = p >> 24;

    p64 = premul_u_to_p8_64bpp (p64, alpha) | alpha;
    out [0] = (p64 >> 16) & 0x000000ff000000ff;
    out [1] = p64 & 0x000000ff000000ff;
}

SMOL_REPACK_ROW_DEF (1234,  32, 32, UNASSOCIATED, COMPRESSED,
                     2341, 128, 64, PREMUL8,      COMPRESSED) {
    while (row_out != row_out_max)
    {
        unpack_pixel_a234_u_to_234a_p8_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE void
unpack_pixel_a234_u_to_234a_p16_128bpp (uint32_t p,
                                        uint64_t *out)
{
    uint64_t p64 = p;
    uint64_t alpha = p >> 24;

    out [0] = (((((p64 & 0x00ff0000) << 16) | ((p64 & 0x0000ff00) >> 8)) * alpha));
    out [1] = (((((p64 & 0x000000ff) << 32) * alpha))) | (alpha << 8) | 0x80;
}

SMOL_REPACK_ROW_DEF (1234,  32, 32, UNASSOCIATED, COMPRESSED,
                     2341, 128, 64, PREMUL16,     COMPRESSED) {
    const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_128 (2, 3, 4, 1);
    unpack_8x_xxxx_u_to_123a_p16_128bpp (&row_in, &row_out, row_out_max,
                                         channel_shuf);

    while (row_out != row_out_max)
    {
        unpack_pixel_a234_u_to_234a_p16_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE void
unpack_pixel_123a_u_to_123a_p8_128bpp (uint32_t p,
                                       uint64_t *out)
{
    uint64_t p64 = (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff0000);
    uint8_t alpha = p & 0xff;

    p64 = premul_u_to_p8_64bpp (p64, alpha) | ((uint64_t) alpha);
    out [0] = (p64 >> 16) & 0x000000ff000000ff;
    out [1] = p64 & 0x000000ff000000ff;
}

SMOL_REPACK_ROW_DEF (1234,  32, 32, UNASSOCIATED, COMPRESSED,
                     1234, 128, 64, PREMUL8,      COMPRESSED) {
    while (row_out != row_out_max)
    {
        unpack_pixel_123a_u_to_123a_p8_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

static SMOL_INLINE void
unpack_pixel_123a_u_to_123a_p16_128bpp (uint32_t p,
                                        uint64_t *out)
{
    uint64_t p64 = p;
    uint64_t alpha = p & 0xff;

    out [0] = (((((p64 & 0xff000000) << 8) | ((p64 & 0x00ff0000) >> 16)) * alpha));
    out [1] = (((((p64 & 0x0000ff00) << 24) * alpha))) | (alpha << 8) | 0x80;
}

SMOL_REPACK_ROW_DEF (1234,  32, 32, UNASSOCIATED, COMPRESSED,
                     1234, 128, 64, PREMUL16,     COMPRESSED) {
    const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_128 (1, 2, 3, 4);
    unpack_8x_xxxx_u_to_123a_p16_128bpp (&row_in, &row_out, row_out_max,
                                         channel_shuf);

    while (row_out != row_out_max)
    {
        unpack_pixel_123a_u_to_123a_p16_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
} SMOL_REPACK_ROW_DEF_END

/* ---------------------- *
 * Repacking: 64 -> 24/32 *
 * ---------------------- */

static void
pack_8x_1234_p8_to_xxxx_p8_64bpp (const uint64_t * SMOL_RESTRICT *in,
                                  uint32_t * SMOL_RESTRICT *out,
                                  uint32_t * out_max,
                                  const __m256i channel_shuf)
{
    const __m256i * SMOL_RESTRICT my_in = (const __m256i * SMOL_RESTRICT) *in;
    __m256i * SMOL_RESTRICT my_out = (__m256i * SMOL_RESTRICT) *out;
    __m256i m0, m1;

    SMOL_ASSUME_ALIGNED (my_in, __m256i * SMOL_RESTRICT);

    while ((ptrdiff_t) (my_out + 1) <= (ptrdiff_t) out_max)
    {
        /* Load inputs */

        m0 = _mm256_stream_load_si256 (my_in);
        my_in++;
        m1 = _mm256_stream_load_si256 (my_in);
        my_in++;

        /* Pack and store */

        m0 = _mm256_packus_epi16 (m0, m1);
        m0 = _mm256_shuffle_epi8 (m0, channel_shuf);
        m0 = _mm256_permute4x64_epi64 (m0, SMOL_4X2BIT (3, 1, 2, 0));

        _mm256_storeu_si256 (my_out, m0);
        my_out++;
    }

    *out = (uint32_t * SMOL_RESTRICT) my_out;
    *in = (const uint64_t * SMOL_RESTRICT) my_in;
}

static SMOL_INLINE uint32_t
pack_pixel_1234_p8_to_1324_p8_64bpp (uint64_t in)
{
    return in | (in >> 24);
}

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     132,  24,  8, PREMUL8,       COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (*(row_in++));
        *(row_out++) = p >> 24;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     132,  24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint8_t alpha = *row_in;
        uint64_t t = (unpremul_p8_to_u_64bpp (*row_in, alpha) & 0xffffffffffffff00ULL) | alpha;
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (t);
        *(row_out++) = p >> 24;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
        row_in++;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     231,  24,  8, PREMUL8,       COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (*(row_in++));
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 24;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     231,  24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint8_t alpha = *row_in;
        uint64_t t = (unpremul_p8_to_u_64bpp (*row_in, alpha) & 0xffffffffffffff00ULL) | alpha;
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (t);
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 24;
        row_in++;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     324,  24,  8, PREMUL8,       COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (*(row_in++));
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
        *(row_out++) = p;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     324,  24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint8_t alpha = *row_in >> 24;
        uint64_t t = (unpremul_p8_to_u_64bpp (*row_in, alpha) & 0xffffffffffffff00ULL) | alpha;
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (t);
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
        *(row_out++) = p;
        row_in++;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     423,  24,  8, PREMUL8,       COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (*(row_in++));
        *(row_out++) = p;
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     423,  24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint8_t alpha = *row_in >> 24;
        uint64_t t = (unpremul_p8_to_u_64bpp (*row_in, alpha) & 0xffffffffffffff00ULL) | alpha;
        uint32_t p = pack_pixel_1234_p8_to_1324_p8_64bpp (t);
        *(row_out++) = p;
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        row_in++;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     1324, 32, 32, PREMUL8,       COMPRESSED) {
    const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_64 (1, 3, 2, 4);
    pack_8x_1234_p8_to_xxxx_p8_64bpp (&row_in, &row_out, row_out_max,
                                      channel_shuf);
    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_1234_p8_to_1324_p8_64bpp (*(row_in++));
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 64, 64, PREMUL8,       COMPRESSED,
                     1324, 32, 32, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint8_t alpha = *row_in;
        uint64_t t = (unpremul_p8_to_u_64bpp (*row_in, alpha) & 0xffffffffffffff00ULL) | alpha;
        *(row_out++) = pack_pixel_1234_p8_to_1324_p8_64bpp (t);
        row_in++;
    }
} SMOL_REPACK_ROW_DEF_END

#define DEF_REPACK_FROM_1234_64BPP_TO_32BPP(a, b, c, d) \
    SMOL_REPACK_ROW_DEF (1234,       64, 64, PREMUL8,       COMPRESSED, \
                         a##b##c##d, 32, 32, PREMUL8,       COMPRESSED) { \
        const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_64 ((a), (b), (c), (d)); \
        pack_8x_1234_p8_to_xxxx_p8_64bpp (&row_in, &row_out, row_out_max, \
                                          channel_shuf); \
        while (row_out != row_out_max) \
        { \
            *(row_out++) = PACK_FROM_1234_64BPP (*row_in, a, b, c, d); \
            row_in++; \
        } \
    } SMOL_REPACK_ROW_DEF_END \
    SMOL_REPACK_ROW_DEF (1234,       64, 64, PREMUL8,       COMPRESSED, \
                         a##b##c##d, 32, 32, UNASSOCIATED,  COMPRESSED) { \
        while (row_out != row_out_max) \
        { \
            uint8_t alpha = *row_in; \
            uint64_t t = (unpremul_p8_to_u_64bpp (*row_in, alpha) & 0xffffffffffffff00ULL) | alpha; \
            *(row_out++) = PACK_FROM_1234_64BPP (t, a, b, c, d); \
            row_in++; \
        } \
    } SMOL_REPACK_ROW_DEF_END

DEF_REPACK_FROM_1234_64BPP_TO_32BPP (1, 4, 2, 3)
DEF_REPACK_FROM_1234_64BPP_TO_32BPP (2, 3, 1, 4)
DEF_REPACK_FROM_1234_64BPP_TO_32BPP (4, 1, 3, 2)
DEF_REPACK_FROM_1234_64BPP_TO_32BPP (4, 2, 3, 1)

/* ----------------------- *
 * Repacking: 128 -> 24/32 *
 * ----------------------- */

static void
pack_8x_123a_p16_to_xxxx_u_128bpp (const uint64_t * SMOL_RESTRICT *in,
                                   uint32_t * SMOL_RESTRICT *out,
                                   uint32_t * out_max,
                                   const __m256i channel_shuf)
{
#define ALPHA_MUL (1 << (INVERTED_DIV_SHIFT_P16 - 8))
#define ALPHA_MASK SMOL_8X1BIT (0, 1, 0, 0, 0, 1, 0, 0)

    const __m256i ones = _mm256_set_epi32 (
        ALPHA_MUL, ALPHA_MUL, ALPHA_MUL, ALPHA_MUL,
        ALPHA_MUL, ALPHA_MUL, ALPHA_MUL, ALPHA_MUL);
    const __m256i alpha_clean_mask = _mm256_set_epi32 (
        0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
        0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff);
    const __m256i * SMOL_RESTRICT my_in = (const __m256i * SMOL_RESTRICT) *in;
    __m256i * SMOL_RESTRICT my_out = (__m256i * SMOL_RESTRICT) *out;
    __m256i m0, m1, m2, m3, m4, m5, m6, m7, m8;

    SMOL_ASSUME_ALIGNED (my_in, __m256i * SMOL_RESTRICT);

    while ((ptrdiff_t) (my_out + 1) <= (ptrdiff_t) out_max)
    {
        /* Load inputs */

        m0 = _mm256_stream_load_si256 (my_in);
        my_in++;
        m1 = _mm256_stream_load_si256 (my_in);
        my_in++;
        m2 = _mm256_stream_load_si256 (my_in);
        my_in++;
        m3 = _mm256_stream_load_si256 (my_in);
        my_in++;

        /* Load alpha factors */

        m4 = _mm256_slli_si256 (m0, 4);
        m6 = _mm256_srli_si256 (m3, 4);
        m5 = _mm256_blend_epi32 (m4, m1, ALPHA_MASK);
        m7 = _mm256_blend_epi32 (m6, m2, ALPHA_MASK);
        m7 = _mm256_srli_si256 (m7, 4);

        m4 = _mm256_blend_epi32 (m5, m7, SMOL_8X1BIT (0, 0, 1, 1, 0, 0, 1, 1));
        m4 = _mm256_srli_epi32 (m4, 8);
        m4 = _mm256_and_si256 (m4, alpha_clean_mask);
        m4 = _mm256_i32gather_epi32 ((const void *) _smol_inv_div_p16_lut, m4, 4);

        /* 2 pixels times 4 */

        m5 = _mm256_shuffle_epi32 (m4, SMOL_4X2BIT (3, 3, 3, 3));
        m6 = _mm256_shuffle_epi32 (m4, SMOL_4X2BIT (2, 2, 2, 2));
        m7 = _mm256_shuffle_epi32 (m4, SMOL_4X2BIT (1, 1, 1, 1));
        m8 = _mm256_shuffle_epi32 (m4, SMOL_4X2BIT (0, 0, 0, 0));

        m5 = _mm256_blend_epi32 (m5, ones, ALPHA_MASK);
        m6 = _mm256_blend_epi32 (m6, ones, ALPHA_MASK);
        m7 = _mm256_blend_epi32 (m7, ones, ALPHA_MASK);
        m8 = _mm256_blend_epi32 (m8, ones, ALPHA_MASK);

        m5 = _mm256_mullo_epi32 (m5, m0);
        m6 = _mm256_mullo_epi32 (m6, m1);
        m7 = _mm256_mullo_epi32 (m7, m2);
        m8 = _mm256_mullo_epi32 (m8, m3);

        m5 = _mm256_srli_epi32 (m5, INVERTED_DIV_SHIFT_P16);
        m6 = _mm256_srli_epi32 (m6, INVERTED_DIV_SHIFT_P16);
        m7 = _mm256_srli_epi32 (m7, INVERTED_DIV_SHIFT_P16);
        m8 = _mm256_srli_epi32 (m8, INVERTED_DIV_SHIFT_P16);

        /* Pack and store */

        m0 = _mm256_packus_epi32 (m5, m6);
        m1 = _mm256_packus_epi32 (m7, m8);
        m0 = _mm256_packus_epi16 (m0, m1);

        m0 = _mm256_shuffle_epi8 (m0, channel_shuf);
        m0 = _mm256_permute4x64_epi64 (m0, SMOL_4X2BIT (3, 1, 2, 0));
        m0 = _mm256_shuffle_epi32 (m0, SMOL_4X2BIT (3, 1, 2, 0));

        _mm256_storeu_si256 (my_out, m0);
        my_out += 1;
    }

    *out = (uint32_t * SMOL_RESTRICT) my_out;
    *in = (const uint64_t * SMOL_RESTRICT) my_in;

#undef ALPHA_MUL
#undef ALPHA_MASK
}

SMOL_REPACK_ROW_DEF (1234, 128, 64, PREMUL8,       COMPRESSED,
                     123,   24,  8, PREMUL8,       COMPRESSED) {
    while (row_out != row_out_max)
    {
        *(row_out++) = *row_in >> 32;
        *(row_out++) = *(row_in++);
        *(row_out++) = *(row_in++) >> 32;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 128, 64, PREMUL8,       COMPRESSED,
                     123,   24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint64_t t [2];
        uint8_t alpha = row_in [1];
        unpremul_p8_to_u_128bpp (row_in, t, alpha);
        t [1] = (t [1] & 0xffffffff00000000ULL) | alpha;
        *(row_out++) = t [0] >> 32;
        *(row_out++) = t [0];
        *(row_out++) = t [1] >> 32;
        row_in += 2;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 128, 64, PREMUL16,      COMPRESSED,
                     123,   24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint64_t t [2];
        uint8_t alpha = row_in [1];
        unpremul_p16_to_u_128bpp (row_in, t, alpha);
        t [1] = (t [1] & 0xffffffff00000000ULL) | alpha;
        *(row_out++) = t [0] >> 32;
        *(row_out++) = t [0];
        *(row_out++) = t [1] >> 32;
        row_in += 2;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 128, 64, PREMUL8,       COMPRESSED,
                     321,   24,  8, PREMUL8,       COMPRESSED) {
    while (row_out != row_out_max)
    {
        *(row_out++) = row_in [1] >> 32;
        *(row_out++) = row_in [0];
        *(row_out++) = row_in [0] >> 32;
        row_in += 2;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 128, 64, PREMUL8,       COMPRESSED,
                     321,   24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint64_t t [2];
        uint8_t alpha = row_in [1];
        unpremul_p8_to_u_128bpp (row_in, t, alpha);
        t [1] = (t [1] & 0xffffffff00000000ULL) | alpha;
        *(row_out++) = t [1] >> 32;
        *(row_out++) = t [0];
        *(row_out++) = t [0] >> 32;
        row_in += 2;
    }
} SMOL_REPACK_ROW_DEF_END

SMOL_REPACK_ROW_DEF (1234, 128, 64, PREMUL16,      COMPRESSED,
                     321,   24,  8, UNASSOCIATED,  COMPRESSED) {
    while (row_out != row_out_max)
    {
        uint64_t t [2];
        uint8_t alpha = row_in [1] >> 8;
        unpremul_p16_to_u_128bpp (row_in, t, alpha);
        t [1] = (t [1] & 0xffffffff00000000ULL) | alpha;
        *(row_out++) = t [1] >> 32;
        *(row_out++) = t [0];
        *(row_out++) = t [0] >> 32;
        row_in += 2;
    }
} SMOL_REPACK_ROW_DEF_END

#define DEF_REPACK_FROM_1234_128BPP_TO_32BPP(a, b, c, d) \
    SMOL_REPACK_ROW_DEF (1234,       128, 64, PREMUL8,       COMPRESSED, \
                         a##b##c##d,  32, 32, PREMUL8,       COMPRESSED) { \
        while (row_out != row_out_max) \
        { \
            *(row_out++) = PACK_FROM_1234_128BPP (row_in, a, b, c, d); \
            row_in += 2; \
        } \
    } SMOL_REPACK_ROW_DEF_END \
    SMOL_REPACK_ROW_DEF (1234,       128, 64, PREMUL8,       COMPRESSED, \
                         a##b##c##d,  32, 32, UNASSOCIATED,  COMPRESSED) { \
        while (row_out != row_out_max) \
        { \
            uint64_t t [2]; \
            uint8_t alpha = row_in [1]; \
            unpremul_p8_to_u_128bpp (row_in, t, alpha); \
            t [1] = (t [1] & 0xffffffff00000000ULL) | alpha; \
            *(row_out++) = PACK_FROM_1234_128BPP (t, a, b, c, d); \
            row_in += 2; \
        } \
    } SMOL_REPACK_ROW_DEF_END \
    SMOL_REPACK_ROW_DEF (1234,       128, 64, PREMUL16,      COMPRESSED, \
                         a##b##c##d,  32, 32, UNASSOCIATED,  COMPRESSED) { \
        const __m256i channel_shuf = PACK_SHUF_MM256_EPI8_32_TO_128 ((a), (b), (c), (d)); \
        pack_8x_123a_p16_to_xxxx_u_128bpp (&row_in, &row_out, row_out_max, \
                                           channel_shuf);               \
        while (row_out != row_out_max) \
        { \
            uint64_t t [2]; \
            uint8_t alpha = row_in [1] >> 8; \
            unpremul_p16_to_u_128bpp (row_in, t, alpha); \
            t [1] = (t [1] & 0xffffffff00000000ULL) | alpha; \
            *(row_out++) = PACK_FROM_1234_128BPP (t, a, b, c, d); \
            row_in += 2; \
        } \
    } SMOL_REPACK_ROW_DEF_END

DEF_REPACK_FROM_1234_128BPP_TO_32BPP (1, 2, 3, 4)
DEF_REPACK_FROM_1234_128BPP_TO_32BPP (3, 2, 1, 4)
DEF_REPACK_FROM_1234_128BPP_TO_32BPP (4, 1, 2, 3)
DEF_REPACK_FROM_1234_128BPP_TO_32BPP (4, 3, 2, 1)

/* -------------- *
 * Filter helpers *
 * -------------- */

#define LERP_SIMD256_EPI32(a, b, f)                                     \
    _mm256_add_epi32 (                                                  \
    _mm256_srli_epi32 (                                                 \
    _mm256_mullo_epi32 (                                                \
    _mm256_sub_epi32 ((a), (b)), (f)), 8), (b))

#define LERP_SIMD128_EPI32(a, b, f)                                     \
    _mm_add_epi32 (                                                     \
    _mm_srli_epi32 (                                                    \
    _mm_mullo_epi32 (                                                   \
    _mm_sub_epi32 ((a), (b)), (f)), 8), (b))

#define LERP_SIMD256_EPI32_AND_MASK(a, b, f, mask)                      \
    _mm256_and_si256 (LERP_SIMD256_EPI32 ((a), (b), (f)), (mask))

#define LERP_SIMD128_EPI32_AND_MASK(a, b, f, mask)                      \
    _mm_and_si128 (LERP_SIMD128_EPI32 ((a), (b), (f)), (mask))

static SMOL_INLINE const char *
inrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx,
                      uint32_t inrow_ofs)
{
    return scale_ctx->pixels_in + scale_ctx->rowstride_in * inrow_ofs;
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
sum_parts_64bpp (const uint64_t ** SMOL_RESTRICT parts_in,
                 uint64_t * SMOL_RESTRICT accum,
                 uint32_t n)
{
    const uint64_t *pp_end;
    const uint64_t * SMOL_RESTRICT pp = *parts_in;

    SMOL_ASSUME_ALIGNED_TO (pp, const uint64_t *, sizeof (uint64_t));

    for (pp_end = pp + n; pp < pp_end; pp++)
    {
        *accum += *pp;
    }

    *parts_in = pp;
}

static SMOL_INLINE void
sum_parts_128bpp (const uint64_t ** SMOL_RESTRICT parts_in,
                  uint64_t * SMOL_RESTRICT accum,
                  uint32_t n)
{
    const uint64_t *pp_end;
    const uint64_t * SMOL_RESTRICT pp = *parts_in;

    SMOL_ASSUME_ALIGNED_TO (pp, const uint64_t *, sizeof (uint64_t) * 2);

    for (pp_end = pp + n * 2; pp < pp_end; )
    {
        accum [0] += *(pp++);
        accum [1] += *(pp++);
    }

    *parts_in = pp;
}

static SMOL_INLINE uint64_t
scale_64bpp (uint64_t accum,
             uint64_t multiplier)
{
    uint64_t a, b;

    /* Average the inputs */
    a = ((accum & 0x0000ffff0000ffffULL) * multiplier
         + (SMOL_BOXES_MULTIPLIER / 2) + ((SMOL_BOXES_MULTIPLIER / 2) << 32)) / SMOL_BOXES_MULTIPLIER;
    b = (((accum & 0xffff0000ffff0000ULL) >> 16) * multiplier
         + (SMOL_BOXES_MULTIPLIER / 2) + ((SMOL_BOXES_MULTIPLIER / 2) << 32)) / SMOL_BOXES_MULTIPLIER;

    /* Return pixel */
    return (a & 0x000000ff000000ffULL) | ((b & 0x000000ff000000ffULL) << 16);
}

static SMOL_INLINE uint64_t
scale_128bpp_half (uint64_t accum,
                   uint64_t multiplier)
{
    uint64_t a, b;

    a = accum & 0x00000000ffffffffULL;
    a = (a * multiplier + SMOL_BOXES_MULTIPLIER / 2) / SMOL_BOXES_MULTIPLIER;

    b = (accum & 0xffffffff00000000ULL) >> 32;
    b = (b * multiplier + SMOL_BOXES_MULTIPLIER / 2) / SMOL_BOXES_MULTIPLIER;

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

    SMOL_ASSUME_ALIGNED (parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_acc_out, uint64_t *);

    while (parts_in + 4 <= parts_in_max)
    {
        __m256i m0, m1;

        m0 = _mm256_stream_load_si256 ((const __m256i *) parts_in);
        parts_in += 4;
        m1 = _mm256_load_si256 ((__m256i *) parts_acc_out);

        m0 = _mm256_add_epi32 (m0, m1);
        _mm256_store_si256 ((__m256i *) parts_acc_out, m0);
        parts_acc_out += 4;
    }

    while (parts_in < parts_in_max)
        *(parts_acc_out++) += *(parts_in++);
}

/* ------------------ *
 * Horizontal scaling *
 * ------------------ */

#define DEF_INTERP_HORIZONTAL_BILINEAR(n_halvings)                      \
static void                                                             \
interp_horizontal_bilinear_##n_halvings##h_64bpp (const SmolScaleCtx *scale_ctx, \
                                                  const uint64_t * SMOL_RESTRICT row_parts_in, \
                                                  uint64_t * SMOL_RESTRICT row_parts_out) \
{                                                                       \
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out; \
    uint64_t p, q;                                                      \
    uint64_t F;                                                         \
    int i;                                                              \
                                                                        \
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);               \
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);                    \
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
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out * 2; \
    const __m256i mask256 = _mm256_set_epi32 (                          \
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,                 \
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);                \
    const __m128i mask128 = _mm_set_epi32 (                             \
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);                \
    const __m256i zero256 = _mm256_setzero_si256 ();                    \
    int i;                                                              \
                                                                        \
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);               \
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);                    \
                                                                        \
    while (row_parts_out != row_parts_out_max)                          \
    {                                                                   \
        __m256i a0 = _mm256_setzero_si256 ();                           \
        __m128i a1;                                                     \
                                                                        \
        for (i = 0; i < (1 << ((n_halvings) - 1)); i++)                 \
        {                                                               \
            __m256i m0, m1;                                             \
            __m256i factors;                                            \
            __m128i n0, n1, n2, n3, n4, n5;                             \
                                                                        \
            row_parts_in += *(ofs_x++) * 2;                             \
            n4 = _mm_set1_epi16 (*(ofs_x++));                           \
            n0 = _mm_load_si128 ((__m128i *) row_parts_in);             \
            n1 = _mm_load_si128 ((__m128i *) row_parts_in + 1);         \
                                                                        \
            row_parts_in += *(ofs_x++) * 2;                             \
            n5 = _mm_set1_epi16 (*(ofs_x++));                           \
            n2 = _mm_load_si128 ((__m128i *) row_parts_in);             \
            n3 = _mm_load_si128 ((__m128i *) row_parts_in + 1);         \
                                                                        \
            m0 = _mm256_set_m128i (n2, n0);                             \
            m1 = _mm256_set_m128i (n3, n1);                             \
            factors = _mm256_set_m128i (n5, n4);                        \
            factors = _mm256_blend_epi16 (factors, zero256, 0xaa);      \
                                                                        \
            m0 = LERP_SIMD256_EPI32_AND_MASK (m0, m1, factors, mask256); \
            a0 = _mm256_add_epi32 (a0, m0);                             \
        }                                                               \
                                                                        \
        a1 = _mm_add_epi32 (_mm256_extracti128_si256 (a0, 0),           \
                            _mm256_extracti128_si256 (a0, 1));          \
        a1 = _mm_srli_epi32 (a1, (n_halvings));                         \
        a1 = _mm_and_si128 (a1, mask128);                               \
        _mm_store_si128 ((__m128i *) row_parts_out, a1);                \
        row_parts_out += 2;                                             \
    }                                                                   \
}

static void
interp_horizontal_bilinear_0h_64bpp (const SmolScaleCtx *scale_ctx,
                                     const uint64_t * SMOL_RESTRICT row_parts_in,
                                     uint64_t * SMOL_RESTRICT row_parts_out)
{
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out;
    const __m256i mask = _mm256_set_epi16 (0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff,
                                           0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff);
    const __m256i shuf_0 = _mm256_set_epi8 (3, 2, 3, 2, 3, 2, 3, 2, 1, 0, 1, 0, 1, 0, 1, 0,
                                            3, 2, 3, 2, 3, 2, 3, 2, 1, 0, 1, 0, 1, 0, 1, 0);
    const __m256i shuf_1 = _mm256_set_epi8 (7, 6, 7, 6, 7, 6, 7, 6, 5, 4, 5, 4, 5, 4, 5, 4,
                                            7, 6, 7, 6, 7, 6, 7, 6, 5, 4, 5, 4, 5, 4, 5, 4);
    const __m256i shuf_2 = _mm256_set_epi8 (11, 10, 11, 10, 11, 10, 11, 10, 9, 8, 9, 8, 9, 8, 9, 8,
                                            11, 10, 11, 10, 11, 10, 11, 10, 9, 8, 9, 8, 9, 8, 9, 8);
    const __m256i shuf_3 = _mm256_set_epi8 (13, 12, 13, 12, 13, 12, 13, 12, 15, 14, 15, 14, 15, 14, 15, 14,
                                            13, 12, 13, 12, 13, 12, 13, 12, 15, 14, 15, 14, 15, 14, 15, 14);

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t * SMOL_RESTRICT);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t * SMOL_RESTRICT);
    SMOL_ASSUME_ALIGNED (ofs_x, const uint16_t * SMOL_RESTRICT);

    while (row_parts_out + BILIN_HORIZ_BATCH_PIXELS <= row_parts_out_max)
    {
        __m256i m0, m1, m2, m3, m4, m5, m6;
        __m256i f0, f1, f2, f3;
        __m256i p00, p01, p10, p11, p20, p21, p30, p31;
        __m128i n0, n1, n2, n3, n4, n5;
        __m256i f;

        m4 = _mm256_load_si256 ((const __m256i *) ofs_x);  /* Offsets */
        f = _mm256_load_si256 ((const __m256i *) ofs_x + 1);  /* Factors */
        ofs_x += 32;

        n0 = _mm256_extracti128_si256 (m4, 0);
        n1 = _mm256_extracti128_si256 (m4, 1);
        m5 = _mm256_cvtepu16_epi32 (n0);
        m6 = _mm256_cvtepu16_epi32 (n1);

        n2 = _mm256_extracti128_si256 (m5, 0);
        n3 = _mm256_extracti128_si256 (m5, 1);
        n4 = _mm256_extracti128_si256 (m6, 0);
        n5 = _mm256_extracti128_si256 (m6, 1);

        p00 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in, n2, 8);
        p10 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in, n3, 8);
        p20 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in, n4, 8);
        p30 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in, n5, 8);

        p01 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in + 1, n2, 8);
        p11 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in + 1, n3, 8);
        p21 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in + 1, n4, 8);
        p31 = _mm256_i32gather_epi64 ((const long long int *) row_parts_in + 1, n5, 8);

        m0 = _mm256_sub_epi16 (p00, p01);
        m1 = _mm256_sub_epi16 (p10, p11);
        m2 = _mm256_sub_epi16 (p20, p21);
        m3 = _mm256_sub_epi16 (p30, p31);

        f0 = _mm256_shuffle_epi8 (f, shuf_0);
        f1 = _mm256_shuffle_epi8 (f, shuf_1);
        f2 = _mm256_shuffle_epi8 (f, shuf_2);
        f3 = _mm256_shuffle_epi8 (f, shuf_3);

        m0 = _mm256_mullo_epi16 (m0, f0);
        m1 = _mm256_mullo_epi16 (m1, f1);
        m2 = _mm256_mullo_epi16 (m2, f2);
        m3 = _mm256_mullo_epi16 (m3, f3);

        m0 = _mm256_srli_epi16 (m0, 8);
        m1 = _mm256_srli_epi16 (m1, 8);
        m2 = _mm256_srli_epi16 (m2, 8);
        m3 = _mm256_srli_epi16 (m3, 8);

        m0 = _mm256_add_epi16 (m0, p01);
        m1 = _mm256_add_epi16 (m1, p11);
        m2 = _mm256_add_epi16 (m2, p21);
        m3 = _mm256_add_epi16 (m3, p31);

        m0 = _mm256_and_si256 (m0, mask);
        m1 = _mm256_and_si256 (m1, mask);
        m2 = _mm256_and_si256 (m2, mask);
        m3 = _mm256_and_si256 (m3, mask);

        _mm256_store_si256 ((__m256i *) row_parts_out, m0);
        _mm256_store_si256 ((__m256i *) row_parts_out + 1, m1);
        _mm256_store_si256 ((__m256i *) row_parts_out + 2, m2);
        _mm256_store_si256 ((__m256i *) row_parts_out + 3, m3);

        row_parts_out += 16;
    }

    while (row_parts_out != row_parts_out_max)
    {
        uint64_t p, q;
        uint64_t F;

        p = *(row_parts_in + *ofs_x);
        q = *(row_parts_in + *ofs_x + 1);
        ofs_x++;
        F = *(ofs_x++);

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
}

static void
interp_horizontal_bilinear_0h_128bpp (const SmolScaleCtx *scale_ctx,
                                      const uint64_t * SMOL_RESTRICT row_parts_in,
                                      uint64_t * SMOL_RESTRICT row_parts_out)
{
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out * 2;
    const __m256i mask256 = _mm256_set_epi32 (
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);
    const __m128i mask128 = _mm_set_epi32 (
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);
    const __m256i zero = _mm256_setzero_si256 ();

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    while (row_parts_out + 4 <= row_parts_out_max)
    {
        __m256i factors;
        __m256i m0, m1;
        __m128i n0, n1, n2, n3, n4, n5;

        row_parts_in += *(ofs_x++) * 2;
        n4 = _mm_set1_epi16 (*(ofs_x++));
        n0 = _mm_load_si128 ((__m128i *) row_parts_in);
        n1 = _mm_load_si128 ((__m128i *) row_parts_in + 1);

        row_parts_in += *(ofs_x++) * 2;
        n5 = _mm_set1_epi16 (*(ofs_x++));
        n2 = _mm_load_si128 ((__m128i *) row_parts_in);
        n3 = _mm_load_si128 ((__m128i *) row_parts_in + 1);

        m0 = _mm256_set_m128i (n2, n0);
        m1 = _mm256_set_m128i (n3, n1);
        factors = _mm256_set_m128i (n5, n4);
        factors = _mm256_blend_epi16 (factors, zero, 0xaa);

        m0 = LERP_SIMD256_EPI32_AND_MASK (m0, m1, factors, mask256);
        _mm256_store_si256 ((__m256i *) row_parts_out, m0);
        row_parts_out += 4;
    }

    /* No need for a loop here; let compiler know we're doing it at most once */
    if (row_parts_out != row_parts_out_max)
    {
        __m128i factors;
        __m128i m0, m1;
        uint32_t f;

        row_parts_in += *(ofs_x++) * 2;
        f = *(ofs_x++);

        factors = _mm_set1_epi32 ((uint32_t) f);
        m0 = _mm_stream_load_si128 ((__m128i *) row_parts_in);
        m1 = _mm_stream_load_si128 ((__m128i *) row_parts_in + 1);

        m0 = LERP_SIMD128_EPI32_AND_MASK (m0, m1, factors, mask128);
        _mm_store_si128 ((__m128i *) row_parts_out, m0);
        row_parts_out += 2;
    }
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

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

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

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

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

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

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

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    while (row_parts_out != row_parts_out_max)
    {
        *(row_parts_out++) = row_parts_in [0];
        *(row_parts_out++) = row_parts_in [1];
    }
}

static void
interp_horizontal_copy_64bpp (const SmolScaleCtx *scale_ctx,
                              const uint64_t * SMOL_RESTRICT row_parts_in,
                              uint64_t * SMOL_RESTRICT row_parts_out)
{
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    memcpy (row_parts_out, row_parts_in, scale_ctx->width_out * sizeof (uint64_t));
}

static void
interp_horizontal_copy_128bpp (const SmolScaleCtx *scale_ctx,
                               const uint64_t * SMOL_RESTRICT row_parts_in,
                               uint64_t * SMOL_RESTRICT row_parts_out)
{
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    memcpy (row_parts_out, row_parts_in, scale_ctx->width_out * 2 * sizeof (uint64_t));
}

static void
scale_horizontal (const SmolScaleCtx *scale_ctx,
                  SmolVerticalCtx *vertical_ctx,
                  const char *row_in,
                  uint64_t *row_parts_out)
{
    uint64_t * SMOL_RESTRICT unpacked_in;

    unpacked_in = vertical_ctx->parts_row [3];

    /* 32-bit unpackers need 32-bit alignment */
    if ((((uintptr_t) row_in) & 3)
        && scale_ctx->pixel_type_in != SMOL_PIXEL_RGB8
        && scale_ctx->pixel_type_in != SMOL_PIXEL_BGR8)
    {
        if (!vertical_ctx->in_aligned)
            vertical_ctx->in_aligned =
                smol_alloc_aligned (scale_ctx->width_in * sizeof (uint32_t),
                                    &vertical_ctx->in_aligned_storage);
        memcpy (vertical_ctx->in_aligned, row_in, scale_ctx->width_in * sizeof (uint32_t));
        row_in = (const char *) vertical_ctx->in_aligned;
    }

    scale_ctx->unpack_row_func ((const uint32_t *) row_in,
                                unpacked_in,
                                scale_ctx->width_in);
    scale_ctx->hfilter_func (scale_ctx,
                             unpacked_in,
                             row_parts_out);
}

/* ---------------- *
 * Vertical scaling *
 * ---------------- */

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
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                          vertical_ctx->parts_row [1]);
    }
    else
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs),
                          vertical_ctx->parts_row [0]);
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                          vertical_ctx->parts_row [1]);
    }

    vertical_ctx->in_ofs = new_in_ofs;
}

static void
interp_vertical_bilinear_store_64bpp (uint64_t F,
                                      const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                      const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                      uint64_t * SMOL_RESTRICT parts_out,
                                      uint32_t width)
{
    uint64_t *parts_out_last = parts_out + width;
    __m256i F256;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

    F256 = _mm256_set1_epi16 ((uint16_t) F);

    while (parts_out + 4 <= parts_out_last)
    {
        __m256i m0, m1;

        m0 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);
        top_row_parts_in += 4;
        m1 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in);
        bottom_row_parts_in += 4;

        m0 = _mm256_sub_epi16 (m0, m1);
        m0 = _mm256_mullo_epi16 (m0, F256);
        m0 = _mm256_srli_epi16 (m0, 8);
        m0 = _mm256_add_epi16 (m0, m1);

        _mm256_store_si256 ((__m256i *) parts_out, m0);
        parts_out += 4;
    }

    while (parts_out != parts_out_last)
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
}

static void
interp_vertical_bilinear_add_64bpp (uint16_t F,
                                    const uint64_t *top_row_parts_in,
                                    const uint64_t *bottom_row_parts_in,
                                    uint64_t *accum_out,
                                    uint32_t width)
{
    uint64_t *accum_out_last = accum_out + width;
    __m256i F256;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (accum_out, uint64_t *);

    F256 = _mm256_set1_epi16 ((uint16_t) F);

    while (accum_out + 4 <= accum_out_last)
    {
        __m256i m0, m1, o0;

        m0 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);
        top_row_parts_in += 4;
        m1 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in);
        bottom_row_parts_in += 4;
        o0 = _mm256_load_si256 ((const __m256i *) accum_out);

        m0 = _mm256_sub_epi16 (m0, m1);
        m0 = _mm256_mullo_epi16 (m0, F256);
        m0 = _mm256_srli_epi16 (m0, 8);
        m0 = _mm256_add_epi16 (m0, m1);

        o0 = _mm256_add_epi16 (o0, m0);
        _mm256_store_si256 ((__m256i *) accum_out, o0);
        accum_out += 4;
    }

    while (accum_out != accum_out_last)
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(accum_out++) += ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
}

static void
interp_vertical_bilinear_store_128bpp (uint64_t F,
                                       const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                       const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                       uint64_t * SMOL_RESTRICT parts_out,
                                       uint32_t width)
{
    uint64_t *parts_out_last = parts_out + width;
    const __m256i mask = _mm256_set_epi32 (
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);
    __m256i F256;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

    F256 = _mm256_set1_epi32 ((uint32_t) F);

    while (parts_out + 8 <= parts_out_last)
    {
        __m256i m0, m1, m2, m3;

        m0 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);
        top_row_parts_in += 4;
        m2 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);
        top_row_parts_in += 4;
        m1 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in);
        bottom_row_parts_in += 4;
        m3 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in);
        bottom_row_parts_in += 4;

        m0 = _mm256_sub_epi32 (m0, m1);
        m2 = _mm256_sub_epi32 (m2, m3);
        m0 = _mm256_mullo_epi32 (m0, F256);
        m2 = _mm256_mullo_epi32 (m2, F256);
        m0 = _mm256_srli_epi32 (m0, 8);
        m2 = _mm256_srli_epi32 (m2, 8);
        m0 = _mm256_add_epi32 (m0, m1);
        m2 = _mm256_add_epi32 (m2, m3);
        m0 = _mm256_and_si256 (m0, mask);
        m2 = _mm256_and_si256 (m2, mask);

        _mm256_store_si256 ((__m256i *) parts_out, m0);
        parts_out += 4;
        _mm256_store_si256 ((__m256i *) parts_out, m2);
        parts_out += 4;
    }

    while (parts_out != parts_out_last)
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
}

static void
interp_vertical_bilinear_add_128bpp (uint64_t F,
                                     const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                     const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                     uint64_t * SMOL_RESTRICT accum_out,
                                     uint32_t width)
{
    uint64_t *accum_out_last = accum_out + width;
    const __m256i mask = _mm256_set_epi32 (
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 
        0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);
    __m256i F256;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (accum_out, uint64_t *);

    F256 = _mm256_set1_epi32 ((uint32_t) F);

    while (accum_out + 8 <= accum_out_last)
    {
        __m256i m0, m1, m2, m3, o0, o1;

        m0 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);
        top_row_parts_in += 4;
        m2 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);
        top_row_parts_in += 4;
        m1 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in);
        bottom_row_parts_in += 4;
        m3 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in);
        bottom_row_parts_in += 4;
        o0 = _mm256_load_si256 ((const __m256i *) accum_out);
        o1 = _mm256_load_si256 ((const __m256i *) (accum_out + 4));

        m0 = _mm256_sub_epi32 (m0, m1);
        m2 = _mm256_sub_epi32 (m2, m3);
        m0 = _mm256_mullo_epi32 (m0, F256);
        m2 = _mm256_mullo_epi32 (m2, F256);
        m0 = _mm256_srli_epi32 (m0, 8);
        m2 = _mm256_srli_epi32 (m2, 8);
        m0 = _mm256_add_epi32 (m0, m1);
        m2 = _mm256_add_epi32 (m2, m3);
        m0 = _mm256_and_si256 (m0, mask);
        m2 = _mm256_and_si256 (m2, mask);

        o0 = _mm256_add_epi32 (o0, m0);
        o1 = _mm256_add_epi32 (o1, m2);
        _mm256_store_si256 ((__m256i *) accum_out, o0);
        accum_out += 4;
        _mm256_store_si256 ((__m256i *) accum_out, o1);
        accum_out += 4;
    }

    while (accum_out != accum_out_last)
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(accum_out++) += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
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
    __m256i F256;                                                       \
                                                                        \
    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);           \
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);        \
    SMOL_ASSUME_ALIGNED (accum_inout, uint64_t *);                      \
                                                                        \
    F256 = _mm256_set1_epi16 ((uint16_t) F);                            \
                                                                        \
    while (accum_inout + 4 <= accum_inout_last)                         \
    {                                                                   \
        __m256i m0, m1, o0;                                             \
                                                                        \
        m0 = _mm256_load_si256 ((const __m256i *) top_row_parts_in);    \
        top_row_parts_in += 4;                                          \
        m1 = _mm256_load_si256 ((const __m256i *) bottom_row_parts_in); \
        bottom_row_parts_in += 4;                                       \
        o0 = _mm256_load_si256 ((const __m256i *) accum_inout);         \
                                                                        \
        m0 = _mm256_sub_epi16 (m0, m1);                                 \
        m0 = _mm256_mullo_epi16 (m0, F256);                             \
        m0 = _mm256_srli_epi16 (m0, 8);                                 \
        m0 = _mm256_add_epi16 (m0, m1);                                 \
                                                                        \
        o0 = _mm256_add_epi16 (o0, m0);                                 \
        o0 = _mm256_srli_epi16 (o0, n_halvings);                        \
                                                                        \
        _mm256_store_si256 ((__m256i *) accum_inout, o0);               \
        accum_inout += 4;                                               \
    }                                                                   \
                                                                        \
    while (accum_inout != accum_inout_last)                             \
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
    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);           \
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);        \
    SMOL_ASSUME_ALIGNED (accum_inout, uint64_t *);                      \
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

    SMOL_ASSUME_ALIGNED (accums, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

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

    SMOL_ASSUME_ALIGNED (row, uint64_t *);

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

    SMOL_ASSUME_ALIGNED (first_row, const uint64_t *);
    SMOL_ASSUME_ALIGNED (last_row, uint64_t *);
    SMOL_ASSUME_ALIGNED (accum, uint64_t *);

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
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [0]);
        weight_edge_row_64bpp (vertical_ctx->parts_row [0], w1, scale_ctx->width_out);
    }

    /* When w2 == 0, the final inrow may be out of bounds. Don't try to access it in
     * that case. */
    if (w2 || ofs_y_max < scale_ctx->height_in)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
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
                          vertical_ctx,
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

    SMOL_ASSUME_ALIGNED (accums, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

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

    SMOL_ASSUME_ALIGNED (row, uint64_t *);

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
                      vertical_ctx,
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
                          vertical_ctx,
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
                          vertical_ctx,
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
    SMOL_UNUSED (row_index);

    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
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
    SMOL_UNUSED (row_index);

    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, 0),
                          vertical_ctx->parts_row [0]);
        vertical_ctx->in_ofs = 0;
    }

    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow_copy (const SmolScaleCtx *scale_ctx,
                   SmolVerticalCtx *vertical_ctx,
                   uint32_t row_index,
                   uint32_t *row_out)
{
    scale_horizontal (scale_ctx,
                      vertical_ctx,
                      inrow_ofs_to_pointer (scale_ctx, row_index),
                      vertical_ctx->parts_row [0]);

    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

/* --------------- *
 * Function tables *
 * --------------- */

#define R SMOL_REPACK_META

static const SmolRepackMeta repack_meta [] =
{
    R (123,   24, PREMUL8,      COMPRESSED, 1324,  64, PREMUL8,       COMPRESSED),

    R (123,   24, PREMUL8,      COMPRESSED, 1234, 128, PREMUL8,       COMPRESSED),

    R (1234,  32, PREMUL8,      COMPRESSED, 1324,  64, PREMUL8,       COMPRESSED),
    R (1234,  32, PREMUL8,      COMPRESSED, 2431,  64, PREMUL8,       COMPRESSED),
    R (1234,  32, PREMUL8,      COMPRESSED, 3241,  64, PREMUL8,       COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 1324,  64, PREMUL8,       COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 2431,  64, PREMUL8,       COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 3241,  64, PREMUL8,       COMPRESSED),

    R (1234,  32, PREMUL8,      COMPRESSED, 1234, 128, PREMUL8,       COMPRESSED),
    R (1234,  32, PREMUL8,      COMPRESSED, 2341, 128, PREMUL8,       COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 1234, 128, PREMUL8,       COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 2341, 128, PREMUL8,       COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 1234, 128, PREMUL16,      COMPRESSED),
    R (1234,  32, UNASSOCIATED, COMPRESSED, 2341, 128, PREMUL16,      COMPRESSED),

    R (1234,  64, PREMUL8,      COMPRESSED, 132,   24, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 231,   24, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 324,   24, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 423,   24, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 132,   24, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 231,   24, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 324,   24, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 423,   24, UNASSOCIATED,  COMPRESSED),

    R (1234,  64, PREMUL8,      COMPRESSED, 1324,  32, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 1423,  32, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 2314,  32, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 4132,  32, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 4231,  32, PREMUL8,       COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 1324,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 1423,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 2314,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 4132,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMUL8,      COMPRESSED, 4231,  32, UNASSOCIATED,  COMPRESSED),

    R (1234, 128, PREMUL8,      COMPRESSED, 123,   24, PREMUL8,       COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 321,   24, PREMUL8,       COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 123,   24, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 321,   24, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL16,     COMPRESSED, 123,   24, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL16,     COMPRESSED, 321,   24, UNASSOCIATED,  COMPRESSED),

    R (1234, 128, PREMUL8,      COMPRESSED, 1234,  32, PREMUL8,       COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 3214,  32, PREMUL8,       COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 4123,  32, PREMUL8,       COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 4321,  32, PREMUL8,       COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 1234,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 3214,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 4123,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL8,      COMPRESSED, 4321,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL16,     COMPRESSED, 1234,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL16,     COMPRESSED, 3214,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL16,     COMPRESSED, 4123,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMUL16,     COMPRESSED, 4321,  32, UNASSOCIATED,  COMPRESSED),

    SMOL_REPACK_META_LAST
};

#undef R

static const SmolImplementation implementation =
{
    /* Horizontal init */
    init_horizontal,

    /* Vertical init */
    init_vertical,

    {
        /* Horizontal filters */
        {
            /* 24bpp */
        },
        {
            /* 32bpp */
        },
        {
            /* 64bpp */
            interp_horizontal_copy_64bpp,
            interp_horizontal_one_64bpp,
            interp_horizontal_bilinear_0h_64bpp,
            interp_horizontal_bilinear_1h_64bpp,
            interp_horizontal_bilinear_2h_64bpp,
            interp_horizontal_bilinear_3h_64bpp,
            interp_horizontal_bilinear_4h_64bpp,
            interp_horizontal_bilinear_5h_64bpp,
            interp_horizontal_bilinear_6h_64bpp,
            interp_horizontal_boxes_64bpp
        },
        {
            /* 128bpp */
            interp_horizontal_copy_128bpp,
            interp_horizontal_one_128bpp,
            interp_horizontal_bilinear_0h_128bpp,
            interp_horizontal_bilinear_1h_128bpp,
            interp_horizontal_bilinear_2h_128bpp,
            interp_horizontal_bilinear_3h_128bpp,
            interp_horizontal_bilinear_4h_128bpp,
            interp_horizontal_bilinear_5h_128bpp,
            interp_horizontal_bilinear_6h_128bpp,
            interp_horizontal_boxes_128bpp
        }
    },
    {
        /* Vertical filters */
        {
            /* 24bpp */
        },
        {
            /* 32bpp */
        },
        {
            /* 64bpp */
            scale_outrow_copy,
            scale_outrow_one_64bpp,
            scale_outrow_bilinear_0h_64bpp,
            scale_outrow_bilinear_1h_64bpp,
            scale_outrow_bilinear_2h_64bpp,
            scale_outrow_bilinear_3h_64bpp,
            scale_outrow_bilinear_4h_64bpp,
            scale_outrow_bilinear_5h_64bpp,
            scale_outrow_bilinear_6h_64bpp,
            scale_outrow_box_64bpp
        },
        {
            /* 128bpp */
            scale_outrow_copy,
            scale_outrow_one_128bpp,
            scale_outrow_bilinear_0h_128bpp,
            scale_outrow_bilinear_1h_128bpp,
            scale_outrow_bilinear_2h_128bpp,
            scale_outrow_bilinear_3h_128bpp,
            scale_outrow_bilinear_4h_128bpp,
            scale_outrow_bilinear_5h_128bpp,
            scale_outrow_bilinear_6h_128bpp,
            scale_outrow_box_128bpp
        }
    },
    repack_meta
};

const SmolImplementation *
_smol_get_avx2_implementation (void)
{
    return &implementation;
}
