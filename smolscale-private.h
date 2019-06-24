/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright (C) 2019 Hans Petter Jansson */

#include <stdint.h>
#include "smolscale.h"

#ifndef _SMOLSCALE_PRIVATE_H_
#define _SMOLSCALE_PRIVATE_H_

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
