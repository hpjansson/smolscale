/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdexcept>
#include <SkPixmap.h>

extern "C" void
skia_scale_raw (const uint32_t *in_raw, int in_width, int in_height,
                uint32_t *out_raw, int out_width, int out_height);

void
skia_scale_raw (const uint32_t *in_raw, int in_width, int in_height,
                uint32_t *out_raw, int out_width, int out_height)
{
    SkImageInfo in_info = SkImageInfo::MakeS32 (in_width, in_height, kPremul_SkAlphaType);
    SkImageInfo out_info = SkImageInfo::MakeS32 (out_width, out_height, kPremul_SkAlphaType);
    SkPixmap in_pixmap = SkPixmap (in_info, in_raw, in_width * sizeof (uint32_t));
    SkPixmap out_pixmap = SkPixmap (out_info, out_raw, out_width * sizeof (uint32_t));

    if (!in_pixmap.scalePixels (out_pixmap, kMedium_SkFilterQuality))
        throw std::runtime_error ("can't scale");
}
