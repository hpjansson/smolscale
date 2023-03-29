#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define LINEAR_BITS 11
#define LINEAR_MAX (1 << (LINEAR_BITS))

static int
to_srgb (int v)
{
    double f = v / (double) (LINEAR_MAX - 1);

    f = f <= 0.0031308 ? f * 12.92 : pow (f, 1.0 / 2.4) * 1.055 - 0.055;
    return f * 255.5;
}

int
main (int argc, char *argv [])
{
    int last_v = -1;
    int i;

    printf ("const uint16_t _smol_from_srgb_lut [256] =\n{\n    ");

    for (i = 0; i < 256; i++)
    {
        double f = i / 255.0;
        int v;

        f = f <= 0.04045 ? f / 12.92 : pow ((f + 0.055) / 1.055, 2.4);
        v = f * (double) (LINEAR_MAX - 1) + 0.5;

        /* Make sure it's reversible */
        while (i > to_srgb (v))
            v++;

        if (v <= last_v)
        {
            /* Make sure the lower, linear part of the curve maps to discrete
             * indexes so they can be reversed. */
            v = last_v + 1;
        }
        else
        {
            /* If there's a range of reversible values, avoid the lowest one.
             * This improves precision with lossy alpha premultiplication. */
            if (to_srgb (v) == to_srgb (v + 1))
                v++;
        }

        /* Don't go out of bounds */
        if (v > 2047)
            v = 2047;

        if (i != 0 && !(i % 12))
            printf ("\n    ");
        printf ("%4d, ", v);

        last_v = v;
    }

    printf ("\n};\n\n");
    printf ("const uint8_t _smol_to_srgb_lut [%d] =\n{\n    ",
            LINEAR_MAX);

    for (i = 0; i < LINEAR_MAX; i++)
    {
        int v;

        v = to_srgb (i);
        if (v > last_v + 1)
            v = last_v + 1;

        if (i != 0 && !(i % 14))
            printf ("\n    ");
        printf ("%3d, ", v);

        last_v = v;
    }

    printf ("\n};\n\n");

    return 0;
}
