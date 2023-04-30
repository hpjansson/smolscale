/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019-2023 Hans Petter Jansson. See COPYING for details. */

#include <stdint.h>

#ifndef _SMOLSCALE_H_
#define _SMOLSCALE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SMOL_SUBPIXEL_SHIFT 8
#define SMOL_SUBPIXEL_MUL (1 << (SMOL_SUBPIXEL_SHIFT))
#define SMOL_PX_TO_SPX(px) ((px) * (SMOL_SUBPIXEL_MUL))
#define SMOL_SPX_TO_PX(spx) (((spx) + (SMOL_SUBPIXEL_MUL) - 1) / (SMOL_SUBPIXEL_MUL))

typedef enum
{
    SMOL_NO_FLAGS               = 0,
    SMOL_FORCE_GENERIC_IMPL     = (1 << 0),
    SMOL_LINEARIZE_SRGB         = (1 << 1)
}
SmolFlags;

typedef enum
{
    /* 32 bits per pixel */

    SMOL_PIXEL_RGBA8_PREMULTIPLIED,
    SMOL_PIXEL_BGRA8_PREMULTIPLIED,
    SMOL_PIXEL_ARGB8_PREMULTIPLIED,
    SMOL_PIXEL_ABGR8_PREMULTIPLIED,

    SMOL_PIXEL_RGBA8_UNASSOCIATED,
    SMOL_PIXEL_BGRA8_UNASSOCIATED,
    SMOL_PIXEL_ARGB8_UNASSOCIATED,
    SMOL_PIXEL_ABGR8_UNASSOCIATED,

    /* 24 bits per pixel */

    SMOL_PIXEL_RGB8,
    SMOL_PIXEL_BGR8,

    SMOL_PIXEL_MAX
}
SmolPixelType;

typedef void (SmolPostRowFunc) (uint32_t *row_inout,
                                int width,
                                void *user_data);

typedef struct SmolScaleCtx SmolScaleCtx;

/* Simple API: Scales an entire image in one shot. You must provide pointers to
 * the source memory and an existing allocation to receive the output data.
 * This interface can only be used from a single thread. */

void smol_scale_simple (const void *pixels_in, SmolPixelType pixel_type_in,
                        uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                        void *pixels_out, SmolPixelType pixel_type_out,
                        uint32_t width_out, uint32_t height_out, uint32_t rowstride_out,
                        SmolFlags flags);

/* Batch API: Allows scaling a few rows at a time. Suitable for multithreading. */

SmolScaleCtx *smol_scale_new (const void *pixels_in, SmolPixelType pixel_type_in,
                              uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                              void *pixels_out, SmolPixelType pixel_type_out,
                              uint32_t width_out, uint32_t height_out, uint32_t rowstride_out,
                              SmolFlags flags);

SmolScaleCtx *smol_scale_new_full (const void *pixels_in, SmolPixelType pixel_type_in,
                                   uint32_t width_in, uint32_t height_in, uint32_t rowstride_in,
                                   void *pixels_out, SmolPixelType pixel_type_out,
                                   uint32_t width_out, uint32_t height_out, uint32_t rowstride_out,
                                   SmolFlags flags,
                                   SmolPostRowFunc post_row_func, void *user_data);

SmolScaleCtx *smol_scale_new_full_subpixel (const void *pixels_in,
                                            SmolPixelType pixel_type_in,
                                            uint32_t width_in,
                                            uint32_t height_in,
                                            uint32_t rowstride_in,
                                            void *pixels_out,
                                            SmolPixelType pixel_type_out,
                                            uint32_t width_out,
                                            uint32_t height_out,
                                            uint32_t rowstride_out,
                                            SmolFlags flags,
                                            SmolPostRowFunc post_row_func,
                                            void *user_data);

void smol_scale_destroy (SmolScaleCtx *scale_ctx);

/* It's ok to call smol_scale_batch() without locking from multiple concurrent
 * threads, as long as the outrows do not overlap. Make sure all workers are
 * finished before you call smol_scale_destroy(). */

void smol_scale_batch (const SmolScaleCtx *scale_ctx, uint32_t first_outrow, uint32_t n_outrows);

/* Like smol_scale_batch(), but will write the output rows to outrows_dest
 * instead of relative to pixels_out address handed to smol_scale_new(). The
 * other parameters from init (size, rowstride, etc) will still be used. */

void smol_scale_batch_full (const SmolScaleCtx *scale_ctx,
                            void *outrows_dest,
                            uint32_t first_outrow, uint32_t n_outrows);

#ifdef __cplusplus
}
#endif

#endif
