/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019-2020 Hans Petter Jansson. See COPYING for details. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
verify_ordering (void)
{
    unsigned char input [65536];
    unsigned char output [65536];
    unsigned char expected_output [65536];
    int type_in_index, type_out_index;
    int result = 0;

    for (type_in_index = 0; pixel_info [type_in_index].type != SMOL_PIXEL_MAX; type_in_index++)
    {
        const PixelInfo *pinfo_in = &pixel_info [type_in_index];

        populate_pixels (input, pinfo_in->type, 65536);

        for (type_out_index = 0; pixel_info [type_out_index].type != SMOL_PIXEL_MAX; type_out_index++)
        {
            const PixelInfo *pinfo_out = &pixel_info [type_out_index];

            fprintf (stdout, "%s -> %s: ", pinfo_in->channels, pinfo_out->channels);
            fflush (stdout);

            populate_pixels (expected_output, pinfo_out->type, 65536);

            smol_scale_simple (pinfo_in->type, (const uint32_t *) input, 16384, 1, 16384 * pinfo_in->n_channels,
                               pinfo_out->type, (uint32_t *) output, 16383, 1, 16383 * pinfo_out->n_channels);

            if (fuzzy_compare_bytes (output, expected_output, 64, 2))
            {
                fprintf (stdout, "mismatch\n");
                print_bytes (expected_output, 64, pinfo_out->n_channels);
                print_bytes (output, 64, pinfo_out->n_channels);
                result = 1;
            }
            else
            {
                fprintf (stdout, "ok");
            }

            fputc ('\n', stdout);
        }
    }

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

        smol_scale_simple (SMOL_PIXEL_ARGB8_UNASSOCIATED, (const uint32_t *) input, 2, 1, 8,
                           SMOL_PIXEL_ARGB8_UNASSOCIATED, (uint32_t *) output, 1, 1, 4);

        if (fuzzy_compare_bytes (output, expected_output, 4,
                                 i < 0x0a ? 0x7f :
                                 i < 0x20 ? 0x16 :
                                 i < 0x30 ? 0x10 :
                                 i < 0x40 ? 0x08 :
                                 4))
        {
            fprintf (stdout, "mismatch\n");
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

        smol_scale_simple (SMOL_PIXEL_ARGB8_UNASSOCIATED, (const uint32_t *) input, 2, 1, 8,
                           SMOL_PIXEL_ARGB8_UNASSOCIATED, (uint32_t *) output, 1, 1, 4);

        if (fuzzy_compare_bytes (output, expected_output, 4, 1))
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

int
main (int argc, char *argv [])
{
    int result = 0;

    result += verify_ordering ();
    result += verify_unassociated_alpha ();

    return result;
}
