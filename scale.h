/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <stdint.h>

#ifndef _SMOLSCALE_H_
#define _SMOLSCALE_H_

typedef enum
{
    ALGORITHM_BILINEAR,
    ALGORITHM_BOX_256,
    ALGORITHM_BOX_65536
}
SmolAlgorithm;

typedef struct SmolScaleCtx SmolScaleCtx;

/* Defining struct SmolScaleCtx here allows client code to allocate it on
 * the stack. I could've made it opaque with dummy variables, but keeping
 * the private and public structs in sync would be messy and error-prone,
 * and we're not guaranteeing any kind of API or ABI stability anyway. */

struct SmolScaleCtx
{
    /* <private> */

    const uint32_t *pixels_in;
    uint32_t *pixels_out;
    uint32_t width_in, height_in, rowstride_in;
    uint32_t width_out, height_out, rowstride_out;

    SmolAlgorithm algo_h, algo_v;

    /* Each offset is split in two uint16s: { pixel index, fraction }. These
     * are relative to the image after halvings have taken place. */
    uint16_t *offsets_x, *offsets_y;
    uint32_t span_mul_x, span_mul_y;  /* For box filter */
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

#endif
