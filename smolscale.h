/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <stdint.h>

#ifndef _SMOLSCALE_H_
#define _SMOLSCALE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ALGORITHM_ONE_64BPP,
    ALGORITHM_BILINEAR_0H_64BPP,
    ALGORITHM_BILINEAR_1H_64BPP,
    ALGORITHM_BILINEAR_2H_64BPP,
    ALGORITHM_BILINEAR_3H_64BPP,
    ALGORITHM_BILINEAR_4H_64BPP,
    ALGORITHM_BILINEAR_5H_64BPP,
    ALGORITHM_BILINEAR_6H_64BPP,
    ALGORITHM_BOX_64BPP,

    ALGORITHM_64BPP_LAST = ALGORITHM_BOX_64BPP,

    ALGORITHM_ONE_128BPP,
    ALGORITHM_BILINEAR_0H_128BPP,
    ALGORITHM_BILINEAR_1H_128BPP,
    ALGORITHM_BILINEAR_2H_128BPP,
    ALGORITHM_BILINEAR_3H_128BPP,
    ALGORITHM_BILINEAR_4H_128BPP,
    ALGORITHM_BILINEAR_5H_128BPP,
    ALGORITHM_BILINEAR_6H_128BPP,
    ALGORITHM_BOX_128BPP,

    ALGORITHM_128BPP_LAST = ALGORITHM_BOX_128BPP
}
SmolAlgorithm;

/* For reusing rows that have already undergone horizontal scaling */
typedef struct
{
    uint32_t in_ofs;
    uint64_t *parts_row [3];
}
SmolVerticalCtx;

/* Defining struct SmolScaleCtx here allows client code to allocate it on
 * the stack. I could've made it opaque with dummy variables, but keeping
 * the private and public structs in sync would be messy and error-prone,
 * and we're not guaranteeing any kind of API or ABI stability anyway. */

typedef struct SmolScaleCtx SmolScaleCtx;

typedef void (SmolUnpackRowFunc) (const uint32_t *row_in,
                                  uint64_t *row_out,
                                  uint32_t n_pixels);
typedef void (SmolPackRowFunc) (const uint64_t *row_in,
                                uint32_t *row_out,
                                uint32_t n_pixels);
typedef void (SmolHFilterFunc) (const SmolScaleCtx *scale_ctx,
                                const uint64_t *row_limbs_in,
                                uint64_t *row_limbs_out);
typedef void (SmolVFilterFunc) (const SmolScaleCtx *scale_ctx,
                                SmolVerticalCtx *vertical_ctx,
                                uint32_t outrow_index,
                                uint32_t *row_out);

struct SmolScaleCtx
{
    /* <private> */

    const uint32_t *pixels_in;
    uint32_t *pixels_out;
    uint32_t width_in, height_in, rowstride_in;
    uint32_t width_out, height_out, rowstride_out;

    SmolAlgorithm algo_h, algo_v;
    SmolUnpackRowFunc *unpack_row_func;
    SmolPackRowFunc *pack_row_func;
    SmolHFilterFunc *hfilter_func;
    SmolVFilterFunc *vfilter_func;

    /* Each offset is split in two uint16s: { pixel index, fraction }. These
     * are relative to the image after halvings have taken place. */
    uint16_t *offsets_x, *offsets_y;
    uint32_t span_mul_x, span_mul_y;  /* For box filter */

    uint32_t width_bilin_out, height_bilin_out;
    unsigned int width_halvings, height_halvings;
};

/* Simple API: Scales an entire image in one shot. You must provide pointers to
 * the source memory and an existing allocation to receive the output data.
 * This interface can only be used from a single thread. */

void smol_scale_simple (const uint32_t *pixels_in,
                        uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                        uint32_t *pixels_out,
                        uint32_t width_out, uint32_t height_out, uint32_t rowstride_out);

/* Batch API: Allows scaling a few rows at a time. Suitable for multithreading. */

void smol_scale_init (SmolScaleCtx *scale_ctx,
                      const uint32_t *pixels_in,
                      uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                      uint32_t *pixels_out,
                      uint32_t width_out, uint32_t height_out, uint32_t rowstride_out);

void smol_scale_finalize (SmolScaleCtx *scale_ctx);

void smol_scale_batch (const SmolScaleCtx *scale_ctx, uint32_t first_row, uint32_t n_rows);

#ifdef __cplusplus
}
#endif

#endif
