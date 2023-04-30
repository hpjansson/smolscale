/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019-2020 Hans Petter Jansson. See COPYING for details. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smolscale.h"

#define N_MOD_STEPS 16
#define MOD_INCREMENT 4
#define N_CHANNELS_MAX 4

typedef struct
{
    SmolPixelType type;
    char channels [5];
    int n_channels;
}
PixelInfo;

static const PixelInfo pixel_info [] =
{
    { SMOL_PIXEL_RGBA8_PREMULTIPLIED,    "rgba",       4 },
    { SMOL_PIXEL_BGRA8_PREMULTIPLIED,    "bgra",       4 },
    { SMOL_PIXEL_ARGB8_PREMULTIPLIED,    "argb",       4 },
    { SMOL_PIXEL_ABGR8_PREMULTIPLIED,    "abgr",       4 },
    { SMOL_PIXEL_RGBA8_UNASSOCIATED,     "rgbA",       4 },
    { SMOL_PIXEL_BGRA8_UNASSOCIATED,     "bgrA",       4 },
    { SMOL_PIXEL_ARGB8_UNASSOCIATED,     "Argb",       4 },
    { SMOL_PIXEL_ABGR8_UNASSOCIATED,     "Abgr",       4 },
    { SMOL_PIXEL_RGB8,                   "rgb\0",      3 },
    { SMOL_PIXEL_BGR8,                   "bgr\0",      3 },

    { SMOL_PIXEL_MAX,                    "\0\0\0\0",   0 }
};

static const PixelInfo *
get_pixel_info (SmolPixelType type)
{
    const PixelInfo *pinfo = NULL;
    int i;

    for (i = 0; pixel_info [i].type != SMOL_PIXEL_MAX; i++)
    {
        pinfo = &pixel_info [i];
        if (pixel_info [i].type == type)
            break;
    }

    assert (pinfo != NULL);

    return pinfo;
}

static unsigned char
get_channel_value (char channel_letter, unsigned char mod)
{
    unsigned char v;

    switch (channel_letter)
    {
        case 'r': v = 0x20 + mod; break;
        case 'g': v = 0x60 + mod; break;
        case 'b': v = 0xa0 + mod; break;
        case 'a':
        case 'A': v = 0xff; break;
        default:  v = 0x00; break;
    }

    return v;
}

static int
populate_pixels (unsigned char *buf, SmolPixelType type, int n_bytes_max)
{
    const PixelInfo *pinfo;
    int mod_step = 0;
    int n;

    pinfo = get_pixel_info (type);

    for (n = 0; n + pinfo->n_channels <= n_bytes_max; )
    {
        int ch;

        for (ch = 0; ch < pinfo->n_channels; ch++)
        {
            buf [n++] = get_channel_value (pinfo->channels [ch], mod_step * MOD_INCREMENT);
        }

        mod_step++;
        mod_step %= N_MOD_STEPS;
    }

    return n;
}

static int
fuzzy_compare_bytes (const unsigned char *out, const unsigned char *verify, int n, int fuzz)
{
    int i;

    for (i = 0; i < n; i++)
    {
        int diff;

        diff = abs ((int) out [i] - (int) verify [i]);
        if (diff > fuzz)
            return 1;
    }

    return 0;
}

static void
print_bytes (const unsigned char *buf, int buf_size, int n_channels)
{
    int i;
    int ch = 0;

    fprintf (stdout, "  ");

    for (i = 0; i < buf_size; )
    {
        unsigned char c = buf [i];
        int u = c >> 4;
        int l = c & 0x0f;

        if (ch < n_channels)
        {
            fputc (u >= 10 ? 'a' + u - 10 : '0' + u, stdout);
            fputc (l >= 10 ? 'a' + l - 10 : '0' + l, stdout);
            i++;
        }
        else
        {
            fputc (' ', stdout);
            fputc (' ', stdout);
        }

        ch++;

        if (!(ch % 4))
        {
            fputc (' ', stdout);
            ch = 0;
        }
    }

    fputc ('\n', stdout);
}

static int
verify_ordering_dir (const unsigned char *input, int n_in, const PixelInfo *pinfo_in,
                     unsigned char *output, int n_out, const PixelInfo *pinfo_out,
                     const unsigned char *expected_output,
                     int dir)
{
    int result = 0;

    if (dir)
        smol_scale_simple (input, pinfo_in->type, 1, n_in, pinfo_in->n_channels,
                           output, pinfo_out->type, 1, n_out, pinfo_out->n_channels,
                           0);
    else
        smol_scale_simple (input, pinfo_in->type, n_in, 1, n_in * pinfo_in->n_channels,
                           output, pinfo_out->type, n_out, 1, n_out * pinfo_out->n_channels,
                           0);

    if (fuzzy_compare_bytes (output, expected_output, 64, 2))
    {
        fprintf (stdout, "%c %s -> %s: ", dir ? 'V' : 'H', pinfo_in->channels, pinfo_out->channels);
        fprintf (stdout, "mismatch\n");
        print_bytes (expected_output, 64, pinfo_out->n_channels);
        print_bytes (output, 64, pinfo_out->n_channels);
        result = 1;
    }

    return result;
}

/* FIXME: This does not verify the 128bpp pathways. In order to do that, we have
 * to scale the image by a lot, and then we wouldn't be able to verify that the
 * pixels are assessed in the right order. The pixels may be out of order if
 * there's an error in SIMD code. */
static int
verify_ordering (void)
{
    unsigned char input [65536];
    unsigned char output [65536];
    unsigned char expected_output [65536];
    int type_in_index, type_out_index;
    int result = 0;

    fprintf (stdout, "Ordering: ");
    fflush (stdout);

    for (type_in_index = 0; pixel_info [type_in_index].type != SMOL_PIXEL_MAX; type_in_index++)
    {
        const PixelInfo *pinfo_in = &pixel_info [type_in_index];

        populate_pixels (input, pinfo_in->type, 65536);

        for (type_out_index = 0; pixel_info [type_out_index].type != SMOL_PIXEL_MAX; type_out_index++)
        {
            const PixelInfo *pinfo_out = &pixel_info [type_out_index];
            int dir;

            populate_pixels (expected_output, pinfo_out->type, 65536);

            for (dir = 0; dir <= 1; dir++)
                result |= verify_ordering_dir (input, 16384, pinfo_in,
                                               output, 16383, pinfo_out,
                                               expected_output,
                                               dir);
        }
    }

    if (!result)
        fprintf (stdout, "ok\n");

    return result;
}

static int
verify_unassociated_alpha (void)
{
    unsigned char input [8] =
    {
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char expected_output [4] =
    {
        0x7f, 0xff, 0xff, 0xff
    };
    unsigned char output [4];
    int result = 0;
    int i;

    fprintf (stdout, "Unassociated alpha: ");
    fflush (stdout);

    for (i = 0; i < 256; i++)
    {
        input [0] = i;
        expected_output [0] = i / 2;

        if (expected_output [0] == 0)
            expected_output [1] = expected_output [2] = expected_output [3] = 0x00;
        else
            expected_output [1] = expected_output [2] = expected_output [3] = 0xff;

        smol_scale_simple ((const uint32_t *) input, SMOL_PIXEL_ARGB8_UNASSOCIATED, 2, 1, 8,
                           (uint32_t *) output, SMOL_PIXEL_ARGB8_UNASSOCIATED, 1, 1, 4,
                           0);

        /* Version 2 produced better results in this test, but it had other
         * issues. Here are its tolerances:
         *
         * i < 0x0a ? 0x7f :
         * i < 0x20 ? 0x16 :
         * i < 0x30 ? 0x10 :
         * i < 0x40 ? 0x08 : 4
         */

        if (fuzzy_compare_bytes (output, expected_output, 4,
                                 i < 0x0a ? 0x7f :
                                 i < 0x19 ? 0x26 :
                                 i < 0x20 ? 0x16 :
                                 i < 0x31 ? 0x11 :
                                 i < 0x40 ? 0x10 :
                                 8))
        {
            fprintf (stdout, "mismatch, i=%d\n", i);
            fprintf (stdout, "in:   "); print_bytes (input, 8, 4);
            fprintf (stdout, "want: "); print_bytes (expected_output, 4, 4);
            fprintf (stdout, "out:  "); print_bytes (output, 4, 4);
            result = 1;
        }
    }

    input [0] = 0xff;

    for (i = 0; i < 256; i++)
    {
        input [4] = i;
        expected_output [0] = (0xff + i) / 2;
        expected_output [1] = expected_output [2] = expected_output [3] = (0xff * 0xff) / (0xff + i);

        smol_scale_simple ((const uint32_t *) input, SMOL_PIXEL_ARGB8_UNASSOCIATED, 2, 1, 8,
                           (uint32_t *) output, SMOL_PIXEL_ARGB8_UNASSOCIATED, 1, 1, 4,
                           0);

        /* Version 2 had a tolerance of 1, which is better. We may want to
         * revisit the conversion to see if we can improve. */

        if (fuzzy_compare_bytes (output, expected_output, 4, 3))
        {
            fprintf (stdout, "mismatch\n");
            fprintf (stdout, "in:   "); print_bytes (input, 8, 4);
            fprintf (stdout, "want: "); print_bytes (expected_output, 4, 4);
            fprintf (stdout, "out:  "); print_bytes (output, 4, 4);
            result = 1;
        }
    }

    if (!result)
        fprintf (stdout, "ok\n");

    return result;
}

static int
verify_saturation_dir (const unsigned char *input, int n_in, const PixelInfo *pinfo_in,
                       unsigned char *output, int n_out, const PixelInfo *pinfo_out,
                       int dir, int with_srgb)
{
    int result = 0;
    unsigned char c = 0xff;
    int i;

    if (dir)
        smol_scale_simple (input, pinfo_in->type, 1, n_in, pinfo_in->n_channels,
                           output, pinfo_out->type, 1, n_out, pinfo_out->n_channels,
                           with_srgb ? SMOL_LINEARIZE_SRGB : SMOL_NO_FLAGS);
    else
        smol_scale_simple (input, pinfo_in->type, n_in, 1, n_in * pinfo_in->n_channels,
                           output, pinfo_out->type, n_out, 1, n_out * pinfo_out->n_channels,
                           with_srgb ? SMOL_LINEARIZE_SRGB : SMOL_NO_FLAGS);

    for (i = 0; i < n_out * pinfo_out->n_channels; i++)
    {
        c = output [i];

        if (c != 0xff)
        {
            fprintf (stdout, "%c %s%s (%d) -> %s (%d): ",
                     dir ? 'V' : 'H',
                     with_srgb ? "sRGB " : " ",
                     pinfo_in->channels, n_in,
                     pinfo_out->channels, n_out);
            fprintf (stdout, "sat mismatch, chan %d is %02x (want 0xff)\n",
                     i % pinfo_out->n_channels,
                     c);
            result = 1;
        }
    }


    return result;
}

static int
verify_saturation (void)
{
    unsigned char input [65536 * 4];
    unsigned char output [65536 * 4];
    int type_in_index, type_out_index;
    int result = 0;

    fprintf (stdout, "Saturation: ");
    fflush (stdout);

    memset (input, 0xff, sizeof (input));

    for (type_in_index = 0; pixel_info [type_in_index].type != SMOL_PIXEL_MAX; type_in_index++)
    {
        const PixelInfo *pinfo_in = &pixel_info [type_in_index];

        for (type_out_index = 0; pixel_info [type_out_index].type != SMOL_PIXEL_MAX; type_out_index++)
        {
            const PixelInfo *pinfo_out = &pixel_info [type_out_index];
            int dir;

            for (dir = 0; dir <= 1; dir++)
            {
                int with_srgb;

                for (with_srgb = 0; with_srgb <= 1; with_srgb++)
                {
                    result |= verify_saturation_dir (input, 1, pinfo_in,
                                                     output, 65535, pinfo_out,
                                                     dir, with_srgb);
                    result |= verify_saturation_dir (input, 2, pinfo_in,
                                                     output, 65535, pinfo_out,
                                                     dir, with_srgb);
                    result |= verify_saturation_dir (input, 65534, pinfo_in,
                                                     output, 65535, pinfo_out,
                                                     dir, with_srgb);
                    result |= verify_saturation_dir (input, 65535, pinfo_in,
                                                     output, 1, pinfo_out,
                                                     dir, with_srgb);
                    result |= verify_saturation_dir (input, 65535, pinfo_in,
                                                     output, 65534, pinfo_out,
                                                     dir, with_srgb);
                }
            }
        }
    }

    if (!result)
        fprintf (stdout, "ok\n");

    return result;
}

static int
verify_preunmul_dir (const unsigned char *input, int n_in,
                     unsigned char *output, int n_out,
                     uint8_t alpha,
                     int dir, int with_srgb)
{
    int result = 0;
    unsigned char c;
    int i;

    if (dir)
        smol_scale_simple (input, SMOL_PIXEL_ARGB8_PREMULTIPLIED, 1, n_in, 4,
                           output, SMOL_PIXEL_ARGB8_UNASSOCIATED, 1, n_out, 4,
                           with_srgb ? SMOL_LINEARIZE_SRGB : SMOL_NO_FLAGS);
    else
        smol_scale_simple (input, SMOL_PIXEL_ARGB8_PREMULTIPLIED, n_in, 1, n_in * 4,
                           output, SMOL_PIXEL_ARGB8_UNASSOCIATED, n_out, 1, n_out * 4,
                           with_srgb ? SMOL_LINEARIZE_SRGB : SMOL_NO_FLAGS);

    for (i = 0; i < n_out * 4; i += 4)
    {
        int j;

        c = output [i];
        if (fuzzy_compare_bytes (&c, &alpha, 1, 
                                 alpha < 0x0a ? 0x3f :
                                 alpha < 0x20 ? 0x16 :
                                 alpha < 0x30 ? 0x10 :
                                 alpha < 0x40 ? 0x08 : 4))
        {
            fprintf (stdout, "%c %s(%d) -> (%d): ",
                     dir ? 'V' : 'H',
                     with_srgb ? "sRGB " : " ",
                     n_in,
                     n_out);
            fprintf (stdout, "preunmul mismatch, alpha is %02x (want 0x%02x)\n",
                     c, alpha);
        }

        for (j = 1; j < 4; j++)
        {
            unsigned char ff = 0xff;

            c = output [i + j];

            if (fuzzy_compare_bytes (&c, &ff, 1, 
                                     alpha < 0x0a ? 0x7f :
                                     alpha < 0x20 ? 0x16 :
                                     alpha < 0x30 ? 0x10 :
                                     alpha < 0x40 ? 0x08 : 4))
            {
                fprintf (stdout, "%c %s(%d) -> (%d): ",
                         dir ? 'V' : 'H',
                         with_srgb ? "sRGB " : " ",
                         n_in,
                         n_out);
                fprintf (stdout, "preunmul mismatch, chan %d is %02x (want 0xff), alpha=0x%02x\n",
                         j, c, alpha);
                result = 1;
            }
        }
    }

    return result;
}

static void
memset_4x (void *dest, uint32_t src, int n)
{
    uint32_t *dest_u32 = dest;
    int i;

    for (i = 0; i < n; i++)
        *(dest_u32++) = src;
}

static int
verify_preunmul (void)
{
    unsigned char input [65536 * 4];
    unsigned char output [65536 * 4];
    uint8_t pixel_in [4] = { 0xff, 0xff, 0xff, 0xff };
    int result = 0;
    int i;

    fprintf (stdout, "Pre/unmul: ");
    fflush (stdout);

    for (i = 1; i <= 0xff; i++)
    {
        int dir;

        pixel_in [0] = i;
        pixel_in [1] = i;
        pixel_in [2] = i;
        pixel_in [3] = i;
        memset_4x (input, *((uint32_t *) pixel_in), 65536);

        for (dir = 0; dir <= 1; dir++)
        {
            int with_srgb;

            for (with_srgb = 0; with_srgb <= 1; with_srgb++)
            {
                result |= verify_preunmul_dir (input, 1,
                                               output, 65535,
                                               i, dir, with_srgb);
                result |= verify_preunmul_dir (input, 2,
                                               output, 65535,
                                               i, dir, with_srgb);
                result |= verify_preunmul_dir (input, 65534,
                                               output, 65535,
                                               i, dir, with_srgb);
                result |= verify_preunmul_dir (input, 65535,
                                               output, 1,
                                               i, dir, with_srgb);
                result |= verify_preunmul_dir (input, 65535,
                                               output, 65534,
                                               i, dir, with_srgb);
                result |= verify_preunmul_dir (input, 65535,
                                               output, 8191,
                                               i, dir, with_srgb);
            }
        }
    }

    if (!result)
        fprintf (stdout, "ok\n");

    return result;
}

int
main (int argc, char *argv [])
{
    int result = 0;

    result += verify_ordering ();
    result += verify_unassociated_alpha ();
    result += verify_saturation ();
    result += verify_preunmul ();

    return result;
}
