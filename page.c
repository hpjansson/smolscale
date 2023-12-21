/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2023 Hans Petter Jansson. See COPYING for details. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "smolscale.h"
#include "png.h"

#define SCALE_FLAGS /* SMOL_DISABLE_SRGB_LINEARIZATION */ SMOL_NO_FLAGS
#define PIXEL_TYPE SMOL_PIXEL_RGBA8_UNASSOCIATED
#define WIDTH 3000
#define HEIGHT 2000
#define N_CH 4
#define PIXELSTRIDE (N_CH)
#define ROWSTRIDE (WIDTH * PIXELSTRIDE)

typedef struct
{
    SmolPixelType pixel_type;
    int width, height, rowstride;
    uint8_t *data;
}
Image;

typedef struct
{
    int x, y;
    int width, height;
}
Rect;

/* Defined in png.c */
gboolean smoltest_load_image (const gchar *file_name,
                              guint *width_out,
                              guint *height_out,
                              gpointer *data_out);
void smoltest_save_image (const gchar *prefix,
                          guint32 *data,
                          guint width,
                          guint height);

/* --- Image generation --- */

static Image *
image_alloc (SmolPixelType pixel_type, int width, int height)
{
    Image *image;

    image = malloc (sizeof (Image));
    image->pixel_type = pixel_type;
    image->width = width;
    image->height = height;
    image->rowstride = width * 4;
    image->data = malloc (image->height * image->rowstride);

    return image;
}

static void
image_free (Image *image)
{
    free (image->data);
    free (image);
}

static Image *
make_background (int width, int height)
{
    Image *image;
    uint8_t pixel [4] = { 0, 0, 0, 0 };
    int i;

    image = image_alloc (PIXEL_TYPE, width, height);

    for (i = 0; i < width * height; i++)
    {
        int j;

        for (j = 0; j < 4; j++)
            image->data [i * 4 + j] = pixel [j];
    }

    return image;
}

static Image *
make_argb_row (int width)
{
    Image *image;
    uint8_t pixel [3] [4] = { { 255,   0,   0, 255 },
                              { 0,   255,   0, 255 },
                              { 0,     0, 255, 255 } };
    int i;

    image = image_alloc (PIXEL_TYPE, width, 1);

    for (i = 0; i < width; i++)
    {
        int j;

        for (j = 0; j < 4; j++)
            image->data [i * 4 + j] = pixel [(i / 2) % 3] [j];
    }

    return image;
}

static Image *
make_unassoc_alpha_row (int width)
{
    Image *image;
    uint8_t pixel [3] [4] = { { 255,   0,   0, 127 },
                              { 0,   255,   0, 0   },
                              { 0,     0, 255, 255 } };
    int i;

    image = image_alloc (PIXEL_TYPE, width, 1);

    for (i = 0; i < width; i++)
    {
        int j;

        for (j = 0; j < 4; j++)
            image->data [i * 4 + j] = pixel [(i / 2) % 3] [j];
    }

    return image;
}

static Image *
make_premul_alpha_row (int width)
{
    Image *image;
    uint8_t pixel [3] [4] = { { 127,   0,   0, 127 },
                              { 0,     2,   0, 2   },
                              { 0,     0, 255, 255 } };
    int i;

    image = image_alloc (PIXEL_TYPE, width, 1);

    for (i = 0; i < width; i++)
    {
        int j;

        for (j = 0; j < 4; j++)
            image->data [i * 4 + j] = pixel [(i / 2) % 3] [j];
    }

    return image;
}

static void
draw_frame (Image *img, int x, int y, int width, int height)
{
    int pixel_stride = 4;
    int rowstride = img->rowstride;
    int i;

    for (i = 0; i < width; i++)
    {
        img->data [(x + i) * pixel_stride + y * rowstride] = 0;
        img->data [(x + i) * pixel_stride + y * rowstride + 1] = 0;
        img->data [(x + i) * pixel_stride + y * rowstride + 2] = 0;
        img->data [(x + i) * pixel_stride + y * rowstride + 3] = 0xff;

        img->data [(x + i) * pixel_stride + (y + height - 1) * rowstride] = 0;
        img->data [(x + i) * pixel_stride + (y + height - 1) * rowstride + 1] = 0;
        img->data [(x + i) * pixel_stride + (y + height - 1) * rowstride + 2] = 0;
        img->data [(x + i) * pixel_stride + (y + height - 1) * rowstride + 3] = 0xff;
    }

    for (i = 0; i < height; i++)
    {
        img->data [x * pixel_stride + (y + i) * rowstride] = 0;
        img->data [x * pixel_stride + (y + i) * rowstride + 1] = 0;
        img->data [x * pixel_stride + (y + i) * rowstride + 2] = 0;
        img->data [x * pixel_stride + (y + i) * rowstride + 3] = 0xff;

        img->data [(x + width - 1) * pixel_stride + (y + i) * rowstride] = 0;
        img->data [(x + width - 1) * pixel_stride + (y + i) * rowstride + 1] = 0;
        img->data [(x + width - 1) * pixel_stride + (y + i) * rowstride + 2] = 0;
        img->data [(x + width - 1) * pixel_stride + (y + i) * rowstride + 3] = 0xff;
    }
}

#if 0
static void
gen_horiz_alpha_ramp (Image *dest_img, const Rect *rect, SmolFlags flags)
{
    Image *argb_row;
    int i;

    argb_row = make_alpha_ramp ();

    for (i = 0; i < rect.width; i++)
    {
        int j;

        for (j = 0; j < rect.height; j++)
        {
            
        }
    }
}
#endif

/* Horizontal wedge made up of vertical slices. Align <0 (left) 0 (mid) >0 (right) */
static void
gen_horiz_wedge (Image *dest_img, Image *src_pixel_row, const Rect *rect, int align, int step, SmolFlags flags)
{
    int first_width;
    int i;

    assert (step != 0);

    if (step < 0)
    {
        first_width = rect->width - 1;
        step = -1;
    }
    else
    {
        first_width = 0;
        step = 1;
    }

    for (i = 0; i < rect->width; i++)
    {
        SmolScaleCtx *ctx;
        int height_spx = ((first_width + i * step) * rect->height * SMOL_SUBPIXEL_MUL) / rect->width;
        int inner_ofs_y_spx = (align < 0 ? 0 :
                               align == 0 ? ((rect->height * SMOL_SUBPIXEL_MUL - height_spx) / 2) :
                               (rect->height * SMOL_SUBPIXEL_MUL - height_spx));
        int ofs_x = rect->x + i;
        int ofs_y = rect->y;

        ctx = smol_scale_new_full (
            /* Input */
            src_pixel_row->data,
            src_pixel_row->pixel_type,
            1,
            src_pixel_row->width,
            4,
            /* BG pixel */
            NULL,
            PIXEL_TYPE,
            /* Output */
            dest_img->data,
            SMOL_PIXEL_RGBA8_UNASSOCIATED,
            dest_img->width,
            dest_img->height,
            WIDTH * N_CH,
            /* Placement */
            ofs_x * SMOL_SUBPIXEL_MUL,
            ofs_y * SMOL_SUBPIXEL_MUL + inner_ofs_y_spx,
            1 * SMOL_SUBPIXEL_MUL,
            height_spx,
            /* Extra parameters */
            SMOL_COMPOSITE_SRC,
            flags,
            NULL,
            NULL);

        smol_scale_batch (ctx, 0, -1);
        smol_scale_destroy (ctx);
    }
}

/* Vertical wedge made up of horizontal slices. Align <0 (left) 0 (mid) >0 (right) */
static void
gen_vert_wedge (Image *dest_img, Image *src_pixel_row, const Rect *rect, int align, int step, SmolFlags flags)
{
    int first_height;
    int i;

    assert (step != 0);

    if (step < 0)
    {
        first_height = rect->height - 1;
        step = -1;
    }
    else
    {
        first_height = 0;
        step = 1;
    }

    for (i = 0; i < rect->height; i++)
    {
        SmolScaleCtx *ctx;
        int width_spx = ((first_height + i * step) * rect->width * SMOL_SUBPIXEL_MUL) / rect->height;
        int inner_ofs_x_spx = (align < 0 ? 0 :
                               align == 0 ? ((rect->width * SMOL_SUBPIXEL_MUL - width_spx) / 2) :
                               (rect->width * SMOL_SUBPIXEL_MUL - width_spx));
        int ofs_x = rect->x;
        int ofs_y = rect->y + i;

        ctx = smol_scale_new_full (
            /* Input */
            src_pixel_row->data,
            src_pixel_row->pixel_type,
            src_pixel_row->width,
            1,
            src_pixel_row->rowstride,
            /* BG pixel */
            NULL,
            PIXEL_TYPE,
            /* Output */
            dest_img->data,
            SMOL_PIXEL_RGBA8_UNASSOCIATED,
            dest_img->width,
            dest_img->height,
            WIDTH * N_CH,
            /* Placement */
            ofs_x * SMOL_SUBPIXEL_MUL + inner_ofs_x_spx,
            ofs_y * SMOL_SUBPIXEL_MUL,
            width_spx,
            1 * SMOL_SUBPIXEL_MUL,
            /* Extra parameters */
            SMOL_COMPOSITE_SRC,
            flags,
            NULL,
            NULL);

        smol_scale_batch (ctx, 0, -1);
        smol_scale_destroy (ctx);
    }
}

static void
gen_horiz_wedges (Image *bg, Image *src, const Rect *r)
{
    Rect s;

    s = *r;

    gen_horiz_wedge (bg, src, &s, 1, 1, SMOL_NO_FLAGS); s.y += 258;
    gen_horiz_wedge (bg, src, &s, -1, 1, SMOL_NO_FLAGS); s.y += 2;

    gen_horiz_wedge (bg, src, &s, 1, -1, SMOL_DISABLE_ACCELERATION); s.y += 258;
    gen_horiz_wedge (bg, src, &s, -1, -1, SMOL_DISABLE_ACCELERATION); s.y += 2;

    gen_horiz_wedge (bg, src, &s, 1, 1, SMOL_DISABLE_SRGB_LINEARIZATION); s.y += 258;
    gen_horiz_wedge (bg, src, &s, -1, 1, SMOL_DISABLE_SRGB_LINEARIZATION); s.y += 2;

    gen_horiz_wedge (bg, src, &s, 1, -1, SMOL_DISABLE_SRGB_LINEARIZATION | SMOL_DISABLE_ACCELERATION); s.y += 258;
    gen_horiz_wedge (bg, src, &s, -1, -1, SMOL_DISABLE_SRGB_LINEARIZATION | SMOL_DISABLE_ACCELERATION);
}

static void
gen_vert_wedges (Image *bg, Image *src, const Rect *r)
{
    Rect s;

    s = *r;

    gen_vert_wedge (bg, src, &s, 1, 1, SMOL_NO_FLAGS); s.x += 258;
    gen_vert_wedge (bg, src, &s, -1, 1, SMOL_NO_FLAGS); s.x += 2;

    gen_vert_wedge (bg, src, &s, 1, -1, SMOL_DISABLE_ACCELERATION); s.x += 258;
    gen_vert_wedge (bg, src, &s, -1, -1, SMOL_DISABLE_ACCELERATION); s.x += 2;

    gen_vert_wedge (bg, src, &s, 1, 1, SMOL_DISABLE_SRGB_LINEARIZATION); s.x += 258;
    gen_vert_wedge (bg, src, &s, -1, 1, SMOL_DISABLE_SRGB_LINEARIZATION); s.x += 2;

    gen_vert_wedge (bg, src, &s, 1, -1, SMOL_DISABLE_SRGB_LINEARIZATION | SMOL_DISABLE_ACCELERATION); s.x += 258;
    gen_vert_wedge (bg, src, &s, -1, -1, SMOL_DISABLE_SRGB_LINEARIZATION | SMOL_DISABLE_ACCELERATION); s.x += 2;
}

static void
draw_clipped_color (Image *dest_img,
                    int x,
                    int y,
                    int width,
                    int height,
                    const uint8_t *color_pixel,
                    double color_x,
                    double color_y,
                    double color_width,
                    double color_height,
                    const uint8_t *clear_pixel)
{
    SmolScaleCtx *ctx;

    assert (width > 2);
    assert (height > 2);

    draw_frame (dest_img, x, y, width, height);

    ctx = smol_scale_new_full (
        /* Input */
        color_pixel,
        SMOL_PIXEL_RGBA8_UNASSOCIATED,
        1,
        1,
        4,
        /* BG pixel */
        clear_pixel,
        SMOL_PIXEL_RGBA8_UNASSOCIATED,
        /* Output */
        dest_img->data + (x + 1) * 4 + (y + 1) * dest_img->rowstride,
        dest_img->pixel_type,
        width - 2,
        height - 2,
        dest_img->rowstride,
        /* Placement */
        color_x * SMOL_SUBPIXEL_MUL,
        color_y * SMOL_SUBPIXEL_MUL,
        color_width * SMOL_SUBPIXEL_MUL,
        color_height * SMOL_SUBPIXEL_MUL,
        /* Extra parameters */
        SMOL_COMPOSITE_SRC_CLEAR_DEST,
        SMOL_NO_FLAGS,
        NULL,
        NULL);
    smol_scale_batch (ctx, 0, -1);
    smol_scale_destroy (ctx);
}

static void
draw_clip_boxes (Image *dest_img)
{
    const uint8_t pixels [] [4] =
    {
        { 0x7f, 0x7f, 0x7f, 0xff },
        { 0xff, 0x00, 0x00, 0xff },
        { 0x00, 0xff, 0x00, 0xff },
        { 0x00, 0x00, 0xff, 0xff },
    };

    draw_clipped_color (dest_img,
                        2, 2, 102, 102,
                        pixels [1],
                        -49.5, -49.5, 100, 100,
                        pixels [0]);

    draw_clipped_color (dest_img,
                        104, 2, 102, 102,
                        pixels [2],
                        49.5, -49.5, 100, 100,
                        pixels [0]);

    draw_clipped_color (dest_img,
                        2, 104, 102, 102,
                        pixels [2],
                        -49.5, 49.5, 100, 100,
                        pixels [0]);

    draw_clipped_color (dest_img,
                        104, 104, 102, 102,
                        pixels [1],
                        49.5, 49.5, 100, 100,
                        pixels [0]);
}

static void
gen_page (void)
{
    Image *bg, *solid_src, *alpha_src;
    const Rect r [] =
    {
        { .x = 2,  .y =   704, .width = 1523, .height = 256 },
        { .x = 1527,  .y =   475, .width = 256, .height = 1523 },
    };
    Rect s;

    bg = make_background (WIDTH, HEIGHT);
    solid_src = make_argb_row (128);
    alpha_src = make_premul_alpha_row (128);

    gen_horiz_wedges (bg, alpha_src, &r [0]);
    gen_vert_wedges (bg, alpha_src, &r [1]);
    draw_clip_boxes (bg);

    smoltest_save_image ("page", bg->data, WIDTH, HEIGHT);

    image_free (alpha_src);
    image_free (solid_src);
    image_free (bg);
}

/* --- Main --- */

int
main (int argc, char *argv [])
{
    gen_page ();
    return 0;
}
