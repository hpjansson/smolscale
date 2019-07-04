/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019 Hans Petter Jansson. See COPYING for details. */

#include <stdint.h>

#ifndef _SMOLSCALE_H_
#define _SMOLSCALE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SMOL_PIXEL_RGBA8_PREMULTIPLIED,
    SMOL_PIXEL_BGRA8_PREMULTIPLIED,
    SMOL_PIXEL_ARGB8_PREMULTIPLIED,
    SMOL_PIXEL_ABGR8_PREMULTIPLIED,

    SMOL_PIXEL_RGBA8_UNASSOCIATED,
    SMOL_PIXEL_BGRA8_UNASSOCIATED,
    SMOL_PIXEL_ARGB8_UNASSOCIATED,
    SMOL_PIXEL_ABGR8_UNASSOCIATED,

    SMOL_PIXEL_MAX
}
SmolPixelType;

typedef struct SmolScaleCtx SmolScaleCtx;

/* Simple API: Scales an entire image in one shot. You must provide pointers to
 * the source memory and an existing allocation to receive the output data.
 * This interface can only be used from a single thread. */

void smol_scale_simple (SmolPixelType pixel_type_in, const uint32_t *pixels_in,
                        uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                        SmolPixelType pixel_type_out, uint32_t *pixels_out,
                        uint32_t width_out, uint32_t height_out, uint32_t rowstride_out);

/* Batch API: Allows scaling a few rows at a time. Suitable for multithreading. */

SmolScaleCtx *smol_scale_new (SmolPixelType pixel_type_in, const uint32_t *pixels_in,
                              uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                              SmolPixelType pixel_type_out, uint32_t *pixels_out,
                              uint32_t width_out, uint32_t height_out, uint32_t rowstride_out);

void smol_scale_destroy (SmolScaleCtx *scale_ctx);

void smol_scale_batch (const SmolScaleCtx *scale_ctx, uint32_t first_outrow, uint32_t n_outrows);

#ifdef __cplusplus
}
#endif

#endif
