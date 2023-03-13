#include <stdio.h>

#define FIXED_DIVIDEND (1 << 21)

int
main (int argc, char *argv [])
{
    int i;

    for (i = 0; i < 256; )
    {
        unsigned int t [8];
        int j;

        for (j = 0; j < 8; j++, i++)
        {
            unsigned int f;

            if (i == 0)
            {
                t [i] = 0;
                continue;
            }

            f = ((FIXED_DIVIDEND + i / 2) / i) - 100;

            while (((0xff * i * f) >> 21) < 0xff)
                f++;
            if (((0xff * i * f) >> 21) > 0xff)
                f--;

            t [j] = f;
        }

        printf ("    %6u, %6u, %6u, %6u, %6u, %6u, %6u, %6u,\n",
                t [0], t [1], t [2], t [3], t [4], t [5], t [6], t [7]);
    }

    return 0;
}
