#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define LINEAR_BITS 8
#define LINEAR_MAX (1 << (LINEAR_BITS))

int
main (int argc, char *argv [])
{
    int i;

    printf ("static const uint16_t from_srgb_lut [256] =\n{\n    ");

    for (i = 0; i < 256; i++)
    {
        double f = i / 255.0;

        if (i != 0 && !(i % 12))
            printf ("\n    ");

#if 0
        f = pow (f, 2.2);
#else
        f = f <= 0.04045 ? f / 12.92 : pow ((f + 0.055) / 1.055, 2.4);
#endif

        printf ("%4d, ", (int) (f * (double) (LINEAR_MAX - 1) + 0.5));
    }

    printf ("\n};\n\n");
    printf ("static const uint8_t to_srgb_lut [%d] =\n{\n    ",
            LINEAR_MAX);

    for (i = 0; i < LINEAR_MAX; i++)
    {
        double f = i / (double) (LINEAR_MAX - 1);
        
        if (i != 0 && !(i % 14))
            printf ("\n    ");

#if 0
        f = pow (f, 1.0 / 2.2);
#else
        f = f <= 0.0031308 ? f * 12.92 : pow (f, 1.0 / 2.4) * 1.055 - 0.055;
#endif

        printf ("%3d, ", (int) (f * 255.5));
    }

    printf ("\n};\n\n");

    return 0;
}
