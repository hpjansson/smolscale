/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019-2023 Hans Petter Jansson. See COPYING for details. */

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc, free, alloca */
#include <string.h> /* memset */
#include <limits.h>
#include "smolscale-private.h"

/* --- sRGB/linear conversion --- */

/* These tables are manually tweaked to be reversible without information
 * loss; smol_to_srgb_lut [smol_from_srgb_lut [i]] == i.
 *
 * As a side effect, the values in the lower range (first 35 indexes) are
 * off by < 2%. */

const uint16_t smol_from_srgb_lut [256] =
{
       0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11, 
      12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23, 
      24,   25,   26,   27,   28,   29,   30,   31,   32,   33,   34,   35, 
      36,   38,   40,   42,   44,   46,   48,   50,   52,   54,   56,   58, 
      61,   63,   66,   68,   71,   73,   76,   78,   81,   84,   87,   90, 
      93,   96,   99,  102,  105,  108,  112,  115,  118,  122,  125,  129, 
     133,  136,  140,  144,  148,  152,  156,  160,  164,  168,  173,  177, 
     181,  186,  190,  195,  200,  204,  209,  214,  219,  224,  229,  234, 
     239,  245,  250,  255,  261,  266,  272,  278,  283,  289,  295,  301, 
     307,  313,  319,  325,  332,  338,  344,  351,  358,  364,  371,  378, 
     384,  391,  398,  405,  413,  420,  427,  434,  442,  449,  457,  465, 
     472,  480,  488,  496,  504,  512,  520,  529,  537,  545,  554,  562, 
     571,  580,  588,  597,  606,  615,  624,  633,  643,  652,  661,  671, 
     681,  690,  700,  710,  720,  730,  740,  750,  760,  770,  781,  791, 
     802,  812,  823,  834,  844,  855,  866,  878,  889,  900,  911,  923, 
     934,  946,  958,  969,  981,  993, 1005, 1017, 1029, 1042, 1054, 1066, 
    1079, 1092, 1104, 1117, 1130, 1143, 1156, 1169, 1182, 1196, 1209, 1222, 
    1236, 1250, 1263, 1277, 1291, 1305, 1319, 1333, 1348, 1362, 1376, 1391, 
    1406, 1420, 1435, 1450, 1465, 1480, 1495, 1511, 1526, 1541, 1557, 1572, 
    1588, 1604, 1620, 1636, 1652, 1668, 1684, 1701, 1717, 1734, 1750, 1767, 
    1784, 1801, 1818, 1835, 1852, 1869, 1886, 1904, 1921, 1939, 1957, 1975, 
    1993, 2011, 2029, 2047, 
};

const uint8_t smol_to_srgb_lut [SRGB_LINEAR_MAX] =
{
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13, 
     14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27, 
     28,  29,  30,  31,  32,  33,  34,  35,  36,  36,  37,  37,  38,  38, 
     39,  39,  40,  40,  41,  41,  42,  42,  43,  43,  44,  44,  45,  45, 
     46,  46,  47,  47,  47,  48,  48,  49,  49,  49,  50,  50,  51,  51, 
     51,  52,  52,  53,  53,  53,  54,  54,  55,  55,  55,  56,  56,  56, 
     57,  57,  57,  58,  58,  58,  59,  59,  59,  60,  60,  60,  61,  61, 
     61,  62,  62,  62,  63,  63,  63,  64,  64,  64,  65,  65,  65,  65, 
     66,  66,  66,  67,  67,  67,  68,  68,  68,  68,  69,  69,  69,  70, 
     70,  70,  70,  71,  71,  71,  71,  72,  72,  72,  73,  73,  73,  73, 
     74,  74,  74,  74,  75,  75,  75,  75,  76,  76,  76,  76,  77,  77, 
     77,  77,  78,  78,  78,  78,  79,  79,  79,  79,  80,  80,  80,  80, 
     81,  81,  81,  81,  81,  82,  82,  82,  82,  83,  83,  83,  83,  84, 
     84,  84,  84,  84,  85,  85,  85,  85,  86,  86,  86,  86,  86,  87, 
     87,  87,  87,  88,  88,  88,  88,  88,  89,  89,  89,  89,  89,  90, 
     90,  90,  90,  90,  91,  91,  91,  91,  91,  92,  92,  92,  92,  92, 
     93,  93,  93,  93,  93,  94,  94,  94,  94,  94,  95,  95,  95,  95, 
     95,  96,  96,  96,  96,  96,  97,  97,  97,  97,  97,  98,  98,  98, 
     98,  98,  98,  99,  99,  99,  99,  99, 100, 100, 100, 100, 100, 100, 
    101, 101, 101, 101, 101, 102, 102, 102, 102, 102, 102, 103, 103, 103, 
    103, 103, 103, 104, 104, 104, 104, 104, 105, 105, 105, 105, 105, 105, 
    106, 106, 106, 106, 106, 106, 107, 107, 107, 107, 107, 107, 108, 108, 
    108, 108, 108, 108, 109, 109, 109, 109, 109, 109, 110, 110, 110, 110, 
    110, 110, 110, 111, 111, 111, 111, 111, 111, 112, 112, 112, 112, 112, 
    112, 113, 113, 113, 113, 113, 113, 113, 114, 114, 114, 114, 114, 114, 
    115, 115, 115, 115, 115, 115, 115, 116, 116, 116, 116, 116, 116, 117, 
    117, 117, 117, 117, 117, 117, 118, 118, 118, 118, 118, 118, 118, 119, 
    119, 119, 119, 119, 119, 120, 120, 120, 120, 120, 120, 120, 121, 121, 
    121, 121, 121, 121, 121, 122, 122, 122, 122, 122, 122, 122, 123, 123, 
    123, 123, 123, 123, 123, 124, 124, 124, 124, 124, 124, 124, 124, 125, 
    125, 125, 125, 125, 125, 125, 126, 126, 126, 126, 126, 126, 126, 127, 
    127, 127, 127, 127, 127, 127, 128, 128, 128, 128, 128, 128, 128, 128, 
    129, 129, 129, 129, 129, 129, 129, 129, 130, 130, 130, 130, 130, 130, 
    130, 131, 131, 131, 131, 131, 131, 131, 131, 132, 132, 132, 132, 132, 
    132, 132, 132, 133, 133, 133, 133, 133, 133, 133, 134, 134, 134, 134, 
    134, 134, 134, 134, 135, 135, 135, 135, 135, 135, 135, 135, 136, 136, 
    136, 136, 136, 136, 136, 136, 137, 137, 137, 137, 137, 137, 137, 137, 
    137, 138, 138, 138, 138, 138, 138, 138, 138, 139, 139, 139, 139, 139, 
    139, 139, 139, 140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 
    141, 141, 141, 141, 141, 141, 142, 142, 142, 142, 142, 142, 142, 142, 
    143, 143, 143, 143, 143, 143, 143, 143, 143, 144, 144, 144, 144, 144, 
    144, 144, 144, 144, 145, 145, 145, 145, 145, 145, 145, 145, 146, 146, 
    146, 146, 146, 146, 146, 146, 146, 147, 147, 147, 147, 147, 147, 147, 
    147, 147, 148, 148, 148, 148, 148, 148, 148, 148, 148, 149, 149, 149, 
    149, 149, 149, 149, 149, 149, 150, 150, 150, 150, 150, 150, 150, 150, 
    150, 151, 151, 151, 151, 151, 151, 151, 151, 151, 152, 152, 152, 152, 
    152, 152, 152, 152, 152, 152, 153, 153, 153, 153, 153, 153, 153, 153, 
    153, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 155, 155, 155, 
    155, 155, 155, 155, 155, 155, 156, 156, 156, 156, 156, 156, 156, 156, 
    156, 156, 157, 157, 157, 157, 157, 157, 157, 157, 157, 158, 158, 158, 
    158, 158, 158, 158, 158, 158, 158, 159, 159, 159, 159, 159, 159, 159, 
    159, 159, 159, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 161, 
    161, 161, 161, 161, 161, 161, 161, 161, 161, 162, 162, 162, 162, 162, 
    162, 162, 162, 162, 162, 163, 163, 163, 163, 163, 163, 163, 163, 163, 
    163, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 165, 165, 165, 
    165, 165, 165, 165, 165, 165, 165, 165, 166, 166, 166, 166, 166, 166, 
    166, 166, 166, 166, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 
    167, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 169, 169, 169, 
    169, 169, 169, 169, 169, 169, 169, 169, 170, 170, 170, 170, 170, 170, 
    170, 170, 170, 170, 170, 171, 171, 171, 171, 171, 171, 171, 171, 171, 
    171, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 173, 173, 
    173, 173, 173, 173, 173, 173, 173, 173, 173, 174, 174, 174, 174, 174, 
    174, 174, 174, 174, 174, 174, 175, 175, 175, 175, 175, 175, 175, 175, 
    175, 175, 175, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 
    176, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 178, 178, 
    178, 178, 178, 178, 178, 178, 178, 178, 178, 179, 179, 179, 179, 179, 
    179, 179, 179, 179, 179, 179, 179, 180, 180, 180, 180, 180, 180, 180, 
    180, 180, 180, 180, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 
    181, 181, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 
    183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 184, 184, 184, 
    184, 184, 184, 184, 184, 184, 184, 184, 184, 185, 185, 185, 185, 185, 
    185, 185, 185, 185, 185, 185, 185, 186, 186, 186, 186, 186, 186, 186, 
    186, 186, 186, 186, 186, 187, 187, 187, 187, 187, 187, 187, 187, 187, 
    187, 187, 187, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 
    188, 188, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 
    190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 191, 191, 
    191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 192, 192, 192, 
    192, 192, 192, 192, 192, 192, 192, 192, 192, 193, 193, 193, 193, 193, 
    193, 193, 193, 193, 193, 193, 193, 193, 194, 194, 194, 194, 194, 194, 
    194, 194, 194, 194, 194, 194, 194, 195, 195, 195, 195, 195, 195, 195, 
    195, 195, 195, 195, 195, 195, 196, 196, 196, 196, 196, 196, 196, 196, 
    196, 196, 196, 196, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 
    197, 197, 197, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 
    198, 198, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 
    199, 199, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 
    200, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 
    202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 
    203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 204, 
    204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 205, 
    205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 206, 206, 
    206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 207, 207, 
    207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 208, 208, 
    208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 209, 209, 
    209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 210, 210, 
    210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 211, 211, 
    211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 212, 212, 
    212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 213, 
    213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 214, 
    214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 215, 
    215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 
    216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 
    216, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 
    217, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 
    218, 218, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 
    219, 219, 219, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 
    220, 220, 220, 220, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 
    221, 221, 221, 221, 221, 222, 222, 222, 222, 222, 222, 222, 222, 222, 
    222, 222, 222, 222, 222, 222, 223, 223, 223, 223, 223, 223, 223, 223, 
    223, 223, 223, 223, 223, 223, 223, 223, 224, 224, 224, 224, 224, 224, 
    224, 224, 224, 224, 224, 224, 224, 224, 224, 225, 225, 225, 225, 225, 
    225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 226, 226, 226, 226, 
    226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 227, 227, 
    227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 
    228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 
    228, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 
    229, 229, 229, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 
    230, 230, 230, 230, 230, 231, 231, 231, 231, 231, 231, 231, 231, 231, 
    231, 231, 231, 231, 231, 231, 231, 232, 232, 232, 232, 232, 232, 232, 
    232, 232, 232, 232, 232, 232, 232, 232, 232, 233, 233, 233, 233, 233, 
    233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 234, 234, 234, 
    234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 
    235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 
    235, 235, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 
    236, 236, 236, 236, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 
    237, 237, 237, 237, 237, 237, 237, 238, 238, 238, 238, 238, 238, 238, 
    238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 239, 239, 239, 239, 
    239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 240, 240, 
    240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 
    240, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 
    241, 241, 241, 241, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 
    242, 242, 242, 242, 242, 242, 242, 243, 243, 243, 243, 243, 243, 243, 
    243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 244, 244, 244, 244, 
    244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 245, 
    245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 
    245, 245, 245, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 
    246, 246, 246, 246, 246, 246, 247, 247, 247, 247, 247, 247, 247, 247, 
    247, 247, 247, 247, 247, 247, 247, 247, 247, 248, 248, 248, 248, 248, 
    248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 249, 
    249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 
    249, 249, 249, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 
    250, 250, 250, 250, 250, 250, 251, 251, 251, 251, 251, 251, 251, 251, 
    251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 252, 252, 252, 252, 
    252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 
    253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 
    253, 253, 253, 253, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 
    254, 254, 254, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 
};

static void
from_srgb_pixel_axxx_128bpp (uint64_t * SMOL_RESTRICT pixel_inout)
{
    uint64_t part;

    SMOL_ASSUME_ALIGNED (pixel_inout, uint64_t *);

    part = *pixel_inout;
    *(pixel_inout++) =
        (part & 0xffffffff00000000)
        | smol_from_srgb_lut [part & 0xff];

    part = *pixel_inout;
    *pixel_inout =
        ((uint64_t) smol_from_srgb_lut [part >> 32] << 32)
        | smol_from_srgb_lut [part & 0xff];
}

static void
to_srgb_pixel_axxx_128bpp (uint64_t * SMOL_RESTRICT pixel_inout)
{
    uint64_t part;

    SMOL_ASSUME_ALIGNED (pixel_inout, uint64_t *);

    part = *pixel_inout;
    *(pixel_inout++) =
        (part & 0xffffffff00000000)
        | smol_to_srgb_lut [part & 0xffff];

    part = *pixel_inout;
    *pixel_inout =
        (((uint64_t) smol_to_srgb_lut [part >> 32]) << 32)
        | smol_to_srgb_lut [part & 0xffff];
}

static void
from_srgb_pixel_xxxa_128bpp (uint64_t * SMOL_RESTRICT pixel_inout)
{
    uint64_t part;

    SMOL_ASSUME_ALIGNED (pixel_inout, uint64_t *);

    part = *pixel_inout;
    *(pixel_inout++) =
        ((uint64_t) smol_from_srgb_lut [part >> 32] << 32)
        | smol_from_srgb_lut [part & 0xff];

    part = *pixel_inout;
    *pixel_inout =
        ((uint64_t) smol_from_srgb_lut [part >> 32] << 32)
        | (part & 0xffffffff);
}

static void
to_srgb_pixel_xxxa_128bpp (uint64_t * SMOL_RESTRICT pixel_inout)
{
    uint64_t part;

    SMOL_ASSUME_ALIGNED (pixel_inout, uint64_t *);

    part = *pixel_inout;
    *(pixel_inout++) =
        (((uint64_t) smol_to_srgb_lut [part >> 32]) << 32)
        | smol_to_srgb_lut [part & 0xffff];

    part = *pixel_inout;
    *pixel_inout =
        (((uint64_t) smol_to_srgb_lut [part >> 32]) << 32)
        | (part & 0xffffffff);
}

/* --- DEPRECATED --- */

/* FIXME: Fails with internally premultiplied (16-bit values) */
static void
from_srgb_row_128bpp (uint64_t * SMOL_RESTRICT row_parts_inout,
                      uint32_t n_pixels)
{
    uint64_t *row_parts_inout_max = row_parts_inout + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_parts_inout, uint64_t *);

    while (row_parts_inout != row_parts_inout_max)
    {
        uint64_t part = *row_parts_inout;
        *(row_parts_inout++) =
            (part & 0xffffffff00000000)
            | smol_from_srgb_lut [part & 0xff];

        part = *row_parts_inout;
        *(row_parts_inout++) =
            ((uint64_t) smol_from_srgb_lut [part >> 32] << 32)
            | smol_from_srgb_lut [part & 0xff];
    }
}

static void
to_srgb_row_128bpp (uint64_t * SMOL_RESTRICT row_parts_inout,
                    uint32_t n_pixels)
{
    uint64_t *row_parts_inout_max = row_parts_inout + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_parts_inout, uint64_t *);

    while (row_parts_inout != row_parts_inout_max)
    {
        uint64_t part = *row_parts_inout;
        *(row_parts_inout++) =
            (part & 0xffffffff00000000)
            | smol_to_srgb_lut [part & 0xffff];

        part = *row_parts_inout;
        *(row_parts_inout++) =
            (((uint64_t) smol_to_srgb_lut [part >> 32]) << 32)
            | smol_to_srgb_lut [part & 0xffff];
    }
}

/* --- Premultiplication --- */

/* This table is used to divide by an integer [1..255] using only a lookup,
 * multiplication and a shift. This is faster than plain division on most
 * architectures.
 *
 * Each entry represents the integer 2097152 (1 << 21) divided by the index
 * of the entry. Consequently,
 *
 * (v / i) ~= (v * inverted_div_table [i] + (1 << 20)) >> 21
 *
 * (1 << 20) is added for nearest rounding. It would've been nice to keep
 * this table in uint16_t, but alas, we need the extra bits for sufficient
 * precision. */
const uint32_t inverted_div_lut [256] =
{
         0,2097152,1048576, 699051, 524288, 419430, 349525, 299593,
    262144, 233017, 209715, 190650, 174763, 161319, 149797, 139810,
    131072, 123362, 116508, 110376, 104858,  99864,  95325,  91181,
     87381,  83886,  80660,  77672,  74898,  72316,  69905,  67650,
     65536,  63550,  61681,  59919,  58254,  56680,  55188,  53773,
     52429,  51150,  49932,  48771,  47663,  46603,  45590,  44620,
     43691,  42799,  41943,  41121,  40330,  39569,  38836,  38130,
     37449,  36792,  36158,  35545,  34953,  34380,  33825,  33288,
     32768,  32264,  31775,  31301,  30840,  30394,  29959,  29537,
     29127,  28728,  28340,  27962,  27594,  27236,  26887,  26546,
     26214,  25891,  25575,  25267,  24966,  24672,  24385,  24105,
     23831,  23564,  23302,  23046,  22795,  22550,  22310,  22075,
     21845,  21620,  21400,  21183,  20972,  20764,  20560,  20361,
     20165,  19973,  19784,  19600,  19418,  19240,  19065,  18893,
     18725,  18559,  18396,  18236,  18079,  17924,  17772,  17623,
     17476,  17332,  17190,  17050,  16913,  16777,  16644,  16513,
     16384,  16257,  16132,  16009,  15888,  15768,  15650,  15534,
     15420,  15308,  15197,  15087,  14980,  14873,  14769,  14665,
     14564,  14463,  14364,  14266,  14170,  14075,  13981,  13888,
     13797,  13707,  13618,  13530,  13443,  13358,  13273,  13190,
     13107,  13026,  12945,  12866,  12788,  12710,  12633,  12558,
     12483,  12409,  12336,  12264,  12193,  12122,  12053,  11984,
     11916,  11848,  11782,  11716,  11651,  11586,  11523,  11460,
     11398,  11336,  11275,  11215,  11155,  11096,  11038,  10980,
     10923,  10866,  10810,  10755,  10700,  10645,  10592,  10538,
     10486,  10434,  10382,  10331,  10280,  10230,  10180,  10131,
     10082,  10034,   9986,   9939,   9892,   9846,   9800,   9754,
      9709,   9664,   9620,   9576,   9533,   9489,   9447,   9404,
      9362,   9321,   9279,   9239,   9198,   9158,   9118,   9079,
      9039,   9001,   8962,   8924,   8886,   8849,   8812,   8775,
      8738,   8702,   8666,   8630,   8595,   8560,   8525,   8490,
      8456,   8422,   8389,   8355,   8322,   8289,   8257,   8224,
};

/* Masking and shifting out the results is left to the caller. In
 * and out may not overlap. */
static SMOL_INLINE void
unpremul_i_to_u_128bpp (const uint64_t * SMOL_RESTRICT in,
                        uint64_t * SMOL_RESTRICT out,
                        uint8_t alpha)
{
    out [0] = ((in [0] * (uint64_t) inverted_div_lut [alpha]
                + INVERTED_DIV_ROUNDING_128BPP) >> INVERTED_DIV_SHIFT);
    out [1] = ((in [1] * (uint64_t) inverted_div_lut [alpha]
                + INVERTED_DIV_ROUNDING_128BPP) >> INVERTED_DIV_SHIFT);
}

static SMOL_INLINE void
unpremul_p_to_u_128bpp (const uint64_t * SMOL_RESTRICT in,
                        uint64_t * SMOL_RESTRICT out,
                        uint8_t alpha)
{
    out [0] = (((in [0] << 8) * (uint64_t) inverted_div_lut [alpha])
               >> INVERTED_DIV_SHIFT);
    out [1] = (((in [1] << 8) * (uint64_t) inverted_div_lut [alpha])
               >> INVERTED_DIV_SHIFT);
}

static SMOL_INLINE uint64_t
unpremul_p_to_u_64bpp (const uint64_t in,
                       uint8_t alpha)
{
    uint64_t in_128bpp [2];
    uint64_t out_128bpp [2];

    in_128bpp [0] = (in & 0x000000ff000000ff);
    in_128bpp [1] = (in & 0x00ff000000ff0000) >> 16;

    unpremul_p_to_u_128bpp (in_128bpp, out_128bpp, alpha);

    return (out_128bpp [0] & 0x000000ff000000ff)
           | ((out_128bpp [1] & 0x000000ff000000ff) << 16);
}

static SMOL_INLINE uint64_t
premul_u_to_p_64bpp (const uint64_t in,
                     uint8_t alpha)
{
    return ((in * ((uint16_t) alpha + 1)) >> 8) & 0x00ff00ff00ff00ff;
}

/* --- Packing --- */

/* It's nice to be able to shift by a negative amount */
#define SHIFT_S(in, s) ((s >= 0) ? (in) << (s) : (in) >> -(s))

#if 0
/* Currently unused */

/* This is kind of bulky (~13 x86 insns), but it's about the same as using
 * unions, and we don't have to worry about endianness. */
#define PACK_FROM_1234_64BPP(in, a, b, c, d)                  \
     ((SHIFT_S ((in), ((a) - 1) * 16 + 8 - 32) & 0xff000000)  \
    | (SHIFT_S ((in), ((b) - 1) * 16 + 8 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in), ((c) - 1) * 16 + 8 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in), ((d) - 1) * 16 + 8 - 56) & 0x000000ff))
#endif

#define PACK_FROM_1234_128BPP(in, a, b, c, d)                                         \
     ((SHIFT_S ((in [((a) - 1) >> 1]), (((a) - 1) & 1) * 32 + 24 - 32) & 0xff000000)  \
    | (SHIFT_S ((in [((b) - 1) >> 1]), (((b) - 1) & 1) * 32 + 24 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in [((c) - 1) >> 1]), (((c) - 1) & 1) * 32 + 24 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in [((d) - 1) >> 1]), (((d) - 1) & 1) * 32 + 24 - 56) & 0x000000ff))

#define SWAP_2_AND_3(n) ((n) == 2 ? 3 : (n) == 3 ? 2 : n)

#define PACK_FROM_1324_64BPP(in, a, b, c, d)                               \
     ((SHIFT_S ((in), (SWAP_2_AND_3 (a) - 1) * 16 + 8 - 32) & 0xff000000)  \
    | (SHIFT_S ((in), (SWAP_2_AND_3 (b) - 1) * 16 + 8 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in), (SWAP_2_AND_3 (c) - 1) * 16 + 8 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in), (SWAP_2_AND_3 (d) - 1) * 16 + 8 - 56) & 0x000000ff))

#if 0
/* Currently unused */

#define PACK_FROM_1324_128BPP(in, a, b, c, d)                               \
     ((SHIFT_S ((in [(SWAP_2_AND_3 (a) - 1) >> 1]),                         \
                ((SWAP_2_AND_3 (a) - 1) & 1) * 32 + 24 - 32) & 0xff000000)  \
    | (SHIFT_S ((in [(SWAP_2_AND_3 (b) - 1) >> 1]),                         \
                ((SWAP_2_AND_3 (b) - 1) & 1) * 32 + 24 - 40) & 0x00ff0000)  \
    | (SHIFT_S ((in [(SWAP_2_AND_3 (c) - 1) >> 1]),                         \
                ((SWAP_2_AND_3 (c) - 1) & 1) * 32 + 24 - 48) & 0x0000ff00)  \
    | (SHIFT_S ((in [(SWAP_2_AND_3 (d) - 1) >> 1]),                         \
                ((SWAP_2_AND_3 (d) - 1) & 1) * 32 + 24 - 56) & 0x000000ff))
#endif

/* Pack p -> p */

static SMOL_INLINE uint32_t
pack_pixel_1324_p_to_1234_p_64bpp (uint64_t in)
{
    return in | (in >> 24);
}

static void
pack_row_1324_p_to_1234_p_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint32_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_1324_p_to_1234_p_64bpp (*(row_in++));
    }
}

static void
pack_row_132a_p_to_123_p_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                uint8_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        /* FIXME: Would be faster to shift directly */
        uint32_t p = pack_pixel_1324_p_to_1234_p_64bpp (*(row_in++));
        *(row_out++) = p >> 24;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
    }
}

static void
pack_row_132a_p_to_321_p_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                uint8_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        /* FIXME: Would be faster to shift directly */
        uint32_t p = pack_pixel_1324_p_to_1234_p_64bpp (*(row_in++));
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 24;
    }
}

#define DEF_PACK_FROM_1324_P_TO_P_64BPP(a, b, c, d)                     \
static SMOL_INLINE uint32_t                                             \
pack_pixel_1324_p_to_##a##b##c##d##_p_64bpp (uint64_t in)               \
{                                                                       \
    return PACK_FROM_1324_64BPP (in, a, b, c, d);                       \
}                                                                       \
                                                                        \
static void                                                             \
pack_row_1324_p_to_##a##b##c##d##_p_64bpp (const uint64_t * SMOL_RESTRICT row_in, \
                                           uint32_t * SMOL_RESTRICT row_out, \
                                           uint32_t n_pixels)           \
{                                                                       \
    uint32_t *row_out_max = row_out + n_pixels;                         \
    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);                     \
    while (row_out != row_out_max)                                      \
        *(row_out++) = pack_pixel_1324_p_to_##a##b##c##d##_p_64bpp (*(row_in++)); \
}

DEF_PACK_FROM_1324_P_TO_P_64BPP (1, 4, 3, 2)
DEF_PACK_FROM_1324_P_TO_P_64BPP (2, 3, 4, 1)
DEF_PACK_FROM_1324_P_TO_P_64BPP (3, 2, 1, 4)
DEF_PACK_FROM_1324_P_TO_P_64BPP (4, 1, 2, 3)
DEF_PACK_FROM_1324_P_TO_P_64BPP (4, 3, 2, 1)

#if 0

static SMOL_INLINE uint32_t
pack_pixel_123a_p_to_123a_p_128bpp (const uint64_t *in)
{
    /* FIXME: Are masks needed? */
    return ((in [0] >> 8) & 0xff000000)
           | ((in [0] << 16) & 0x00ff0000)
           | ((in [1] >> 24) & 0x0000ff00)
           | (in [1] & 0x000000ff);
}

static void
pack_row_123a_p_to_123a_p_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                  uint32_t * SMOL_RESTRICT row_out,
                                  uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_123a_p_to_123a_p_128bpp (row_in);
        row_in += 2;
    }
}

#endif

#define DEF_PACK_FROM_123A_P_TO_P_128BPP(to, a, b, c, d)                \
static SMOL_INLINE uint32_t                                             \
pack_pixel_123a_p_to_##to##_p_128bpp (const uint64_t * SMOL_RESTRICT in) \
{                                                                       \
    return PACK_FROM_1234_128BPP (in, a, b, c, d);                      \
}                                                                       \
                                                                        \
static void                                                             \
pack_row_123a_p_to_##to##_p_128bpp (const uint64_t * SMOL_RESTRICT row_in, \
                                    uint32_t * SMOL_RESTRICT row_out,   \
                                    uint32_t n_pixels)                  \
{                                                                       \
    uint32_t *row_out_max = row_out + n_pixels;                         \
    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);                     \
    while (row_out != row_out_max)                                      \
    {                                                                   \
        *(row_out++) = pack_pixel_123a_p_to_##to##_p_128bpp (row_in);   \
        row_in += 2;                                                    \
    }                                                                   \
}

DEF_PACK_FROM_123A_P_TO_P_128BPP (123a, 1, 2, 3, 4)
DEF_PACK_FROM_123A_P_TO_P_128BPP (321a, 3, 2, 1, 4)
DEF_PACK_FROM_123A_P_TO_P_128BPP (a123, 4, 1, 2, 3)
DEF_PACK_FROM_123A_P_TO_P_128BPP (a321, 4, 3, 2, 1)

static void
pack_row_123a_p_to_123_p_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint8_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = *row_in >> 32;
        *(row_out++) = *(row_in++);
        *(row_out++) = *(row_in++) >> 32;
    }
}

static void
pack_row_123a_p_to_321_p_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint8_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = row_in [1] >> 32;
        *(row_out++) = row_in [0];
        *(row_out++) = row_in [0] >> 32;
        row_in += 2;
    }
}

/* Pack p (alpha last) -> u */

static SMOL_INLINE uint32_t
pack_pixel_132a_p_to_1234_u_64bpp (uint64_t in)
{
    uint8_t alpha = in;
    in = (unpremul_p_to_u_64bpp (in, alpha) & 0xffffffffffffff00) | alpha;
    return in | (in >> 24);
}

static void
pack_row_132a_p_to_1234_u_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint32_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_132a_p_to_1234_u_64bpp (*(row_in++));
    }
}

static void
pack_row_132a_p_to_123_u_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                uint8_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_132a_p_to_1234_u_64bpp (*(row_in++));
        *(row_out++) = p >> 24;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
    }
}

static void
pack_row_132a_p_to_321_u_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                uint8_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_132a_p_to_1234_u_64bpp (*(row_in++));
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 24;
    }
}

#define DEF_PACK_FROM_132A_P_TO_U_64BPP(a, b, c, d)                     \
static SMOL_INLINE uint32_t                                             \
pack_pixel_132a_p_to_##a##b##c##d##_u_64bpp (uint64_t in)               \
{                                                                       \
    uint8_t alpha = in;                                                 \
    in = (unpremul_p_to_u_64bpp (in, alpha) & 0xffffffffffffff00) | alpha; \
    return PACK_FROM_1324_64BPP (in, a, b, c, d);                       \
}                                                                       \
                                                                        \
static void                                                             \
pack_row_132a_p_to_##a##b##c##d##_u_64bpp (const uint64_t * SMOL_RESTRICT row_in, \
                                           uint32_t * SMOL_RESTRICT row_out, \
                                           uint32_t n_pixels)           \
{                                                                       \
    uint32_t *row_out_max = row_out + n_pixels;                         \
    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);                     \
    while (row_out != row_out_max)                                      \
        *(row_out++) = pack_pixel_132a_p_to_##a##b##c##d##_u_64bpp (*(row_in++)); \
}

DEF_PACK_FROM_132A_P_TO_U_64BPP (3, 2, 1, 4)
DEF_PACK_FROM_132A_P_TO_U_64BPP (4, 1, 2, 3)
DEF_PACK_FROM_132A_P_TO_U_64BPP (4, 3, 2, 1)

#define DEF_PACK_FROM_123A_P_TO_U_128BPP(to, a, b, c, d)                \
static SMOL_INLINE uint32_t                                             \
pack_pixel_123a_p_to_##to##_u_128bpp (const uint64_t * SMOL_RESTRICT in) \
{                                                                       \
    uint64_t t [2];                                                     \
    uint8_t alpha = in [1];                                             \
    unpremul_p_to_u_128bpp (in, t, alpha);                              \
    t [1] = (t [1] & 0xffffffff00000000) | alpha;                       \
    return PACK_FROM_1234_128BPP (t, a, b, c, d);                       \
}                                                                       \
                                                                        \
static void                                                             \
pack_row_123a_p_to_##to##_u_128bpp (const uint64_t * SMOL_RESTRICT row_in, \
                                    uint32_t * SMOL_RESTRICT row_out,   \
                                    uint32_t n_pixels)                  \
{                                                                       \
    uint32_t *row_out_max = row_out + n_pixels;                         \
    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);                     \
    while (row_out != row_out_max)                                      \
    {                                                                   \
        *(row_out++) = pack_pixel_123a_p_to_##to##_u_128bpp (row_in);   \
        row_in += 2;                                                    \
    }                                                                   \
}

DEF_PACK_FROM_123A_P_TO_U_128BPP (123a, 1, 2, 3, 4)
DEF_PACK_FROM_123A_P_TO_U_128BPP (321a, 3, 2, 1, 4)
DEF_PACK_FROM_123A_P_TO_U_128BPP (a123, 4, 1, 2, 3)
DEF_PACK_FROM_123A_P_TO_U_128BPP (a321, 4, 3, 2, 1)

static void
pack_row_123a_p_to_123_u_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint8_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_123a_p_to_123a_u_128bpp (row_in);
        row_in += 2;
        *(row_out++) = p >> 24;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
    }
}

static void
pack_row_123a_p_to_321_u_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint8_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_123a_p_to_123a_u_128bpp (row_in);
        row_in += 2;
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 24;
    }
}

/* Pack p (alpha first) -> u */

static SMOL_INLINE uint32_t
pack_pixel_a324_p_to_1234_u_64bpp (uint64_t in)
{
    uint8_t alpha = (in >> 48) & 0xff;  /* FIXME: May not need mask */
    in = (unpremul_p_to_u_64bpp (in, alpha) & 0x0000ffffffffffff) | ((uint64_t) alpha << 48);
    return in | (in >> 24);
}

static void
pack_row_a324_p_to_1234_u_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint32_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_a324_p_to_1234_u_64bpp (*(row_in++));
    }
}

static void
pack_row_a324_p_to_234_u_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                uint8_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_a324_p_to_1234_u_64bpp (*(row_in++));
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
        *(row_out++) = p;
    }
}

static void
pack_row_a324_p_to_432_u_64bpp (const uint64_t * SMOL_RESTRICT row_in,
                                uint8_t * SMOL_RESTRICT row_out,
                                uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_a324_p_to_1234_u_64bpp (*(row_in++));
        *(row_out++) = p;
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
    }
}

#define DEF_PACK_FROM_A324_P_TO_U_64BPP(a, b, c, d)                     \
static SMOL_INLINE uint32_t                                             \
pack_pixel_a324_p_to_##a##b##c##d##_u_64bpp (uint64_t in)               \
{                                                                       \
    uint8_t alpha = (in >> 48) & 0xff;  /* FIXME: May not need mask */  \
    in = (unpremul_p_to_u_64bpp (in, alpha) & 0x0000ffffffffffff) | ((uint64_t) alpha << 48); \
    return PACK_FROM_1324_64BPP (in, a, b, c, d);                       \
}                                                                       \
                                                                        \
static void                                                             \
pack_row_a324_p_to_##a##b##c##d##_u_64bpp (const uint64_t * SMOL_RESTRICT row_in, \
                                           uint32_t * SMOL_RESTRICT row_out, \
                                           uint32_t n_pixels)           \
{                                                                       \
    uint32_t *row_out_max = row_out + n_pixels;                         \
    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);                     \
    while (row_out != row_out_max)                                      \
        *(row_out++) = pack_pixel_a324_p_to_##a##b##c##d##_u_64bpp (*(row_in++)); \
}

DEF_PACK_FROM_A324_P_TO_U_64BPP (1, 4, 3, 2)
DEF_PACK_FROM_A324_P_TO_U_64BPP (2, 3, 4, 1)
DEF_PACK_FROM_A324_P_TO_U_64BPP (4, 3, 2, 1)

/* Pack i (alpha last) to u */

#if 0

static SMOL_INLINE uint32_t
pack_pixel_123a_i_to_123a_u_128bpp (const uint64_t * SMOL_RESTRICT in)
{
    uint8_t alpha = (in [1] >> 8) & 0xff;
    uint64_t t [2];

    unpremul_i_to_u_128bpp (in, t, alpha);

    return ((t [0] >> 8) & 0xff000000)
           | ((t [0] << 16) & 0x00ff0000)
           | ((t [1] >> 24) & 0x0000ff00)
           | alpha;
}

static void
pack_row_123a_i_to_123a_u_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                  uint32_t * SMOL_RESTRICT row_out,
                                  uint32_t n_pixels)
{
    uint32_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = pack_pixel_123a_i_to_123a_u_128bpp (row_in);
        row_in += 2;
    }
}

#endif

#define DEF_PACK_FROM_123A_I_TO_U_128BPP(to, a, b, c, d)                \
static SMOL_INLINE uint32_t                                             \
pack_pixel_123a_i_to_##to##_u_128bpp (const uint64_t * SMOL_RESTRICT in) \
{                                                                       \
    uint8_t alpha = (in [1] >> 8) & 0xff;                               \
    uint64_t t [2];                                                     \
    unpremul_i_to_u_128bpp (in, t, alpha);                              \
    t [1] = (t [1] & 0xffffffff00000000ULL) | alpha;                    \
    return PACK_FROM_1234_128BPP (t, a, b, c, d);                       \
}                                                                       \
                                                                        \
static void                                                             \
pack_row_123a_i_to_##to##_u_128bpp (const uint64_t * SMOL_RESTRICT row_in, \
                                            uint32_t * SMOL_RESTRICT row_out, \
                                            uint32_t n_pixels)          \
{                                                                       \
    uint32_t *row_out_max = row_out + n_pixels;                         \
    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);                     \
    while (row_out != row_out_max)                                      \
    {                                                                   \
        *(row_out++) = pack_pixel_123a_i_to_##to##_u_128bpp (row_in);   \
        row_in += 2;                                                    \
    }                                                                   \
}

DEF_PACK_FROM_123A_I_TO_U_128BPP (123a, 1, 2, 3, 4)
DEF_PACK_FROM_123A_I_TO_U_128BPP (321a, 3, 2, 1, 4)
DEF_PACK_FROM_123A_I_TO_U_128BPP (a123, 4, 1, 2, 3)
DEF_PACK_FROM_123A_I_TO_U_128BPP (a321, 4, 3, 2, 1)

static void
pack_row_123a_i_to_123_u_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint8_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_123a_i_to_123a_u_128bpp (row_in);
        row_in += 2;
        *(row_out++) = p >> 24;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 8;
    }
}

static void
pack_row_123a_i_to_321_u_128bpp (const uint64_t * SMOL_RESTRICT row_in,
                                 uint8_t * SMOL_RESTRICT row_out,
                                 uint32_t n_pixels)
{
    uint8_t *row_out_max = row_out + n_pixels * 3;

    SMOL_ASSUME_ALIGNED (row_in, const uint64_t *);

    while (row_out != row_out_max)
    {
        uint32_t p = pack_pixel_123a_i_to_123a_u_128bpp (row_in);
        row_in += 2;
        *(row_out++) = p >> 8;
        *(row_out++) = p >> 16;
        *(row_out++) = p >> 24;
    }
}

/* Unpack p -> p */

static SMOL_INLINE uint64_t
unpack_pixel_1234_p_to_1324_p_64bpp (uint32_t p)
{
    return (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff00ff);
}

/* AVX2 has a useful instruction for this: __m256i _mm256_cvtepu8_epi16 (__m128i a);
 * It results in a different channel ordering, so it'd be important to match with
 * the right kind of re-pack. */
static void
unpack_row_1234_p_to_1324_p_64bpp (const uint32_t * SMOL_RESTRICT row_in,
                                   uint64_t * SMOL_RESTRICT row_out,
                                   uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_1234_p_to_1324_p_64bpp (*(row_in++));
    }
}

static SMOL_INLINE uint64_t
unpack_pixel_123_p_to_132a_p_64bpp (const uint8_t *p)
{
    return ((uint64_t) p [0] << 48) | ((uint32_t) p [1] << 16)
        | ((uint64_t) p [2] << 32) | 0xff;
}

static void
unpack_row_123_p_to_132a_p_64bpp (const uint8_t * SMOL_RESTRICT row_in,
                                  uint64_t * SMOL_RESTRICT row_out,
                                  uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_123_p_to_132a_p_64bpp (row_in);
        row_in += 3;
    }
}

static SMOL_INLINE void
unpack_pixel_123a_p_to_123a_p_128bpp (uint32_t p,
                                      uint64_t *out)
{
    uint64_t p64 = p;
    out [0] = ((p64 & 0xff000000) << 8) | ((p64 & 0x00ff0000) >> 16);
    out [1] = ((p64 & 0x0000ff00) << 24) | (p64 & 0x000000ff);
}

static void
unpack_row_123a_p_to_123a_p_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                    uint64_t * SMOL_RESTRICT row_out,
                                    uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_123a_p_to_123a_p_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

static SMOL_INLINE void
unpack_pixel_a234_p_to_234a_p_128bpp (uint32_t p,
                                      uint64_t *out)
{
    uint64_t p64 = p;
    out [0] = ((p64 & 0x00ff0000) << 16) | ((p64 & 0x0000ff00) >> 8);
    out [1] = ((p64 & 0x000000ff) << 32) | ((p64 & 0xff000000) >> 24);
}

static void
unpack_row_a234_p_to_234a_p_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                    uint64_t * SMOL_RESTRICT row_out,
                                    uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_a234_p_to_234a_p_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

static SMOL_INLINE void
unpack_pixel_123_p_to_123a_p_128bpp (const uint8_t *in,
                                     uint64_t *out)
{
    out [0] = ((uint64_t) in [0] << 32) | in [1];
    out [1] = ((uint64_t) in [2] << 32) | 0xff;
}

static void
unpack_row_123_p_to_123a_p_128bpp (const uint8_t * SMOL_RESTRICT row_in,
                                   uint64_t * SMOL_RESTRICT row_out,
                                   uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_123_p_to_123a_p_128bpp (row_in, row_out);
        row_in += 3;
        row_out += 2;
    }
}

/* Unpack u (alpha first) -> p */

static SMOL_INLINE uint64_t
unpack_pixel_a234_u_to_a324_p_64bpp (uint32_t p)
{
    uint64_t p64 = (((uint64_t) p & 0x0000ff00) << 24) | (p & 0x00ff00ff);
    uint8_t alpha = p >> 24;

    return premul_u_to_p_64bpp (p64, alpha) | ((uint64_t) alpha << 48);
}

static void
unpack_row_a234_u_to_a324_p_64bpp (const uint32_t * SMOL_RESTRICT row_in,
                                   uint64_t * SMOL_RESTRICT row_out,
                                   uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_a234_u_to_a324_p_64bpp (*(row_in++));
    }
}

static SMOL_INLINE void
unpack_pixel_a234_u_to_234a_p_128bpp (uint32_t p,
                                      uint64_t *out)
{
    uint64_t p64 = (((uint64_t) p & 0x0000ff00) << 40) | (((uint64_t) p & 0x00ff00ff) << 16);
    uint8_t alpha = p >> 24;

    p64 = premul_u_to_p_64bpp (p64, alpha) | alpha;
    out [0] = (p64 >> 16) & 0x000000ff000000ff;
    out [1] = p64 & 0x000000ff000000ff;
}

static void
unpack_row_a234_u_to_234a_p_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                    uint64_t * SMOL_RESTRICT row_out,
                                    uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_a234_u_to_234a_p_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

/* Unpack u (alpha first) -> i */

static SMOL_INLINE void
unpack_pixel_a234_u_to_234a_i_128bpp (uint32_t p,
                                      uint64_t *out)
{
    uint64_t p64 = p;
    uint64_t alpha = p >> 24;

    out [0] = (((((p64 & 0x00ff0000) << 16) | ((p64 & 0x0000ff00) >> 8)) * alpha));
    out [1] = (((((p64 & 0x000000ff) << 32) * alpha))) | (alpha << 8) | 0x80;
}

static void
unpack_row_a234_u_to_234a_i_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                    uint64_t * SMOL_RESTRICT row_out,
                                    uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_a234_u_to_234a_i_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

/* Unpack u (alpha last) -> p */

static SMOL_INLINE uint64_t
unpack_pixel_123a_u_to_132a_p_64bpp (uint32_t p)
{
    uint64_t p64 = (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff0000);
    uint8_t alpha = p & 0xff;

    return premul_u_to_p_64bpp (p64, alpha) | ((uint64_t) alpha);
}

static void
unpack_row_123a_u_to_132a_p_64bpp (const uint32_t * SMOL_RESTRICT row_in,
                                   uint64_t * SMOL_RESTRICT row_out,
                                   uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        *(row_out++) = unpack_pixel_123a_u_to_132a_p_64bpp (*(row_in++));
    }
}

static SMOL_INLINE void
unpack_pixel_123a_u_to_123a_p_128bpp (uint32_t p,
                                      uint64_t *out)
{
    uint64_t p64 = (((uint64_t) p & 0xff00ff00) << 24) | (p & 0x00ff0000);
    uint8_t alpha = p & 0xff;

    p64 = premul_u_to_p_64bpp (p64, alpha) | ((uint64_t) alpha);
    out [0] = (p64 >> 16) & 0x000000ff000000ff;
    out [1] = p64 & 0x000000ff000000ff;
}

static void
unpack_row_123a_u_to_123a_p_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                    uint64_t * SMOL_RESTRICT row_out,
                                    uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_123a_u_to_123a_p_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

/* Unpack u (alpha last) -> i */

static SMOL_INLINE void
unpack_pixel_123a_u_to_123a_i_128bpp (uint32_t p,
                                      uint64_t *out)
{
    uint64_t p64 = p;
    uint64_t alpha = p & 0xff;

    out [0] = (((((p64 & 0xff000000) << 8) | ((p64 & 0x00ff0000) >> 16)) * alpha));
    out [1] = (((((p64 & 0x0000ff00) << 24) * alpha))) | (alpha << 8) | 0x80;
}

static void
unpack_row_123a_u_to_123a_i_128bpp (const uint32_t * SMOL_RESTRICT row_in,
                                    uint64_t * SMOL_RESTRICT row_out,
                                    uint32_t n_pixels)
{
    uint64_t *row_out_max = row_out + n_pixels * 2;

    SMOL_ASSUME_ALIGNED (row_out, uint64_t *);

    while (row_out != row_out_max)
    {
        unpack_pixel_123a_u_to_123a_i_128bpp (*(row_in++), row_out);
        row_out += 2;
    }
}

/* --- Filter helpers --- */

static SMOL_INLINE const char *
inrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx,
                      uint32_t inrow_ofs)
{
    return scale_ctx->pixels_in + scale_ctx->rowstride_in * inrow_ofs;
}

static SMOL_INLINE char *
outrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx,
                       uint32_t outrow_ofs)
{
    return scale_ctx->pixels_out + scale_ctx->rowstride_out * outrow_ofs;
}

static SMOL_INLINE uint64_t
weight_pixel_64bpp (uint64_t p,
                    uint16_t w)
{
    return ((p * w) >> 8) & 0x00ff00ff00ff00ff;
}

/* p and out may be the same address */
static SMOL_INLINE void
weight_pixel_128bpp (uint64_t *p,
                     uint64_t *out,
                     uint16_t w)
{
    out [0] = ((p [0] * w) >> 8) & 0x00ffffff00ffffffULL;
    out [1] = ((p [1] * w) >> 8) & 0x00ffffff00ffffffULL;
}

static SMOL_INLINE void
sum_parts_64bpp (const uint64_t ** SMOL_RESTRICT parts_in,
                 uint64_t * SMOL_RESTRICT accum,
                 uint32_t n)
{
    const uint64_t *pp_end;
    const uint64_t * SMOL_RESTRICT pp = *parts_in;

    SMOL_ASSUME_ALIGNED_TO (pp, const uint64_t *, sizeof (uint64_t));

    for (pp_end = pp + n; pp < pp_end; pp++)
    {
        *accum += *pp;
    }

    *parts_in = pp;
}

static SMOL_INLINE void
sum_parts_128bpp (const uint64_t ** SMOL_RESTRICT parts_in,
                  uint64_t * SMOL_RESTRICT accum,
                  uint32_t n)
{
    const uint64_t *pp_end;
    const uint64_t * SMOL_RESTRICT pp = *parts_in;

    SMOL_ASSUME_ALIGNED_TO (pp, const uint64_t *, sizeof (uint64_t) * 2);

    for (pp_end = pp + n * 2; pp < pp_end; )
    {
        accum [0] += *(pp++);
        accum [1] += *(pp++);
    }

    *parts_in = pp;
}

static SMOL_INLINE uint64_t
scale_64bpp (uint64_t accum,
             uint64_t multiplier)
{
    uint64_t a, b;

    /* Average the inputs */
    a = ((accum & 0x0000ffff0000ffffULL) * multiplier
         + (SMOL_BOXES_MULTIPLIER / 2) + ((SMOL_BOXES_MULTIPLIER / 2) << 32)) / SMOL_BOXES_MULTIPLIER;
    b = (((accum & 0xffff0000ffff0000ULL) >> 16) * multiplier
         + (SMOL_BOXES_MULTIPLIER / 2) + ((SMOL_BOXES_MULTIPLIER / 2) << 32)) / SMOL_BOXES_MULTIPLIER;

    /* Return pixel */
    return (a & 0x000000ff000000ffULL) | ((b & 0x000000ff000000ffULL) << 16);
}

static SMOL_INLINE uint64_t
scale_128bpp_half (uint64_t accum,
                   uint64_t multiplier)
{
    uint64_t a, b;

    a = accum & 0x00000000ffffffffULL;
    a = (a * multiplier + SMOL_BOXES_MULTIPLIER / 2) / SMOL_BOXES_MULTIPLIER;

    b = (accum & 0xffffffff00000000ULL) >> 32;
    b = (b * multiplier + SMOL_BOXES_MULTIPLIER / 2) / SMOL_BOXES_MULTIPLIER;

    return (a & 0x000000000000ffffULL)
           | ((b & 0x000000000000ffffULL) << 32);
}

static SMOL_INLINE void
scale_and_store_128bpp (const uint64_t * SMOL_RESTRICT accum,
                        uint64_t multiplier,
                        uint64_t ** SMOL_RESTRICT row_parts_out)
{
    *(*row_parts_out)++ = scale_128bpp_half (accum [0], multiplier);
    *(*row_parts_out)++ = scale_128bpp_half (accum [1], multiplier);
}

static void
add_parts (const uint64_t * SMOL_RESTRICT parts_in,
           uint64_t * SMOL_RESTRICT parts_acc_out,
           uint32_t n)
{
    const uint64_t *parts_in_max = parts_in + n;

    SMOL_ASSUME_ALIGNED (parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_acc_out, uint64_t *);

    while (parts_in < parts_in_max)
        *(parts_acc_out++) += *(parts_in++);
}

/* --- Precalculation --- */

static void
pick_filter_params (uint32_t dim_in,
                    uint32_t dim_out,
                    uint32_t *halvings_out,
                    uint32_t *dim_bilin_out,
                    SmolFilterType *filter_out,
                    SmolStorageType *storage_out,
                    uint8_t with_srgb)
{
    *dim_bilin_out = dim_out;
    *storage_out = with_srgb ? SMOL_STORAGE_128BPP : SMOL_STORAGE_64BPP;

    /* The box algorithms are only sufficiently precise when
     * dim_in > dim_out * 5. box_64bpp typically starts outperforming
     * bilinear+halving at dim_in > dim_out * 8. */

    if (dim_in > dim_out * 255)
    {
        *filter_out = SMOL_FILTER_BOX;
        *storage_out = SMOL_STORAGE_128BPP;
    }
    else if (dim_in > dim_out * 8)
    {
        *filter_out = SMOL_FILTER_BOX;
    }
    else if (dim_in == 1)
    {
        *filter_out = SMOL_FILTER_ONE;
    }
    else if (dim_in == dim_out)
    {
        *filter_out = SMOL_FILTER_COPY;
    }
    else
    {
        uint32_t n_halvings = 0;
        uint32_t d = dim_out;

        for (;;)
        {
            d *= 2;
            if (d >= dim_in)
                break;
            n_halvings++;
        }

        dim_out <<= n_halvings;
        *dim_bilin_out = dim_out;
        *filter_out = SMOL_FILTER_BILINEAR_0H + n_halvings;
        *halvings_out = n_halvings;
    }
}

static void
precalc_bilinear_array (uint16_t *array,
                        uint32_t dim_in,
                        uint32_t dim_out,
                        unsigned int make_absolute_offsets)
{
    uint64_t ofs_stepF, fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t last_ofs = 0;

    if (dim_in > dim_out)
    {
        /* Minification */
        frac_stepF = ofs_stepF = (dim_in * SMOL_BILIN_MULTIPLIER) / dim_out;
        fracF = (frac_stepF - SMOL_BILIN_MULTIPLIER) / 2;
    }
    else
    {
        /* Magnification */
        frac_stepF = ofs_stepF = ((dim_in - 1) * SMOL_BILIN_MULTIPLIER) / (dim_out > 1 ? (dim_out - 1) : 1);
        fracF = 0;
    }

    do
    {
        uint16_t ofs = fracF / SMOL_BILIN_MULTIPLIER;

        /* We sample ofs and its neighbor -- prevent out of bounds access
         * for the latter. */
        if (ofs >= dim_in - 1)
            break;

        *(pu16++) = make_absolute_offsets ? ofs : ofs - last_ofs;
        *(pu16++) = SMOL_SMALL_MUL - ((fracF / (SMOL_BILIN_MULTIPLIER / SMOL_SMALL_MUL)) % SMOL_SMALL_MUL);
        fracF += frac_stepF;

        last_ofs = ofs;
    }
    while (--dim_out);

    /* Instead of going out of bounds, sample the final pair of pixels with a 100%
     * bias towards the last pixel */
    while (dim_out)
    {
        *(pu16++) = make_absolute_offsets ? dim_in - 2 : (dim_in - 2) - last_ofs;
        *(pu16++) = 0;
        dim_out--;

        last_ofs = dim_in - 2;
    }
}

static void
precalc_boxes_array (uint16_t *array,
                     uint32_t *span_mul,
                     uint32_t dim_in,
                     uint32_t dim_out,
                     unsigned int make_absolute_offsets)
{
    uint64_t fracF, frac_stepF;
    uint16_t *pu16 = array;
    uint16_t ofs, next_ofs;
    uint64_t f;
    uint64_t stride;
    uint64_t a, b;

    frac_stepF = ((uint64_t) dim_in * SMOL_BIG_MUL) / (uint64_t) dim_out;
    fracF = 0;
    ofs = 0;

    stride = frac_stepF / (uint64_t) SMOL_BIG_MUL;
    f = (frac_stepF / SMOL_SMALL_MUL) % SMOL_SMALL_MUL;

    a = (SMOL_BOXES_MULTIPLIER * 255);
    b = ((stride * 255) + ((f * 255) / 256));
    *span_mul = (a + (b / 2)) / b;

    do
    {
        fracF += frac_stepF;
        next_ofs = (uint64_t) fracF / ((uint64_t) SMOL_BIG_MUL);

        /* Prevent out of bounds access */
        if (ofs >= dim_in - 1)
            break;

        if (next_ofs > dim_in)
        {
            next_ofs = dim_in;
            if (next_ofs <= ofs)
                break;
        }

        stride = next_ofs - ofs - 1;
        f = (fracF / SMOL_SMALL_MUL) % SMOL_SMALL_MUL;

        /* Fraction is the other way around, since left pixel of each span
         * comes first, and it's on the right side of the fractional sample. */
        *(pu16++) = make_absolute_offsets ? ofs : stride;
        *(pu16++) = f;

        ofs = next_ofs;
    }
    while (--dim_out);

    /* Instead of going out of bounds, sample the final pair of pixels with a 100%
     * bias towards the last pixel */
    while (dim_out)
    {
        *(pu16++) = make_absolute_offsets ? ofs : 0;
        *(pu16++) = 0;
        dim_out--;
    }

    *(pu16++) = make_absolute_offsets ? ofs : 0;
    *(pu16++) = 0;
}

/* --- Horizontal scaling --- */

#define DEF_INTERP_HORIZONTAL_BILINEAR(n_halvings)                      \
static void                                                             \
interp_horizontal_bilinear_##n_halvings##h_64bpp (const SmolScaleCtx *scale_ctx, \
                                                  const uint64_t * SMOL_RESTRICT row_parts_in, \
                                                  uint64_t * SMOL_RESTRICT row_parts_out) \
{                                                                       \
    uint64_t p, q;                                                      \
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t F;                                                         \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out; \
    int i;                                                              \
                                                                        \
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);               \
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);                    \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t accum = 0;                                             \
                                                                        \
        for (i = 0; i < (1 << (n_halvings)); i++)                       \
        {                                                               \
            row_parts_in += *(ofs_x++);                                 \
            F = *(ofs_x++);                                             \
                                                                        \
            p = *row_parts_in;                                          \
            q = *(row_parts_in + 1);                                    \
                                                                        \
            accum += ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL; \
        }                                                               \
        *(row_parts_out++) = ((accum) >> (n_halvings)) & 0x00ff00ff00ff00ffULL; \
    }                                                                   \
    while (row_parts_out != row_parts_out_max);                         \
}                                                                       \
                                                                        \
static void                                                             \
interp_horizontal_bilinear_##n_halvings##h_128bpp (const SmolScaleCtx *scale_ctx, \
                                                   const uint64_t * SMOL_RESTRICT row_parts_in, \
                                                   uint64_t * SMOL_RESTRICT row_parts_out) \
{                                                                       \
    uint64_t p, q;                                                      \
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;        \
    uint64_t F;                                                         \
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out * 2; \
    int i;                                                              \
                                                                        \
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);               \
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);                    \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t accum [2] = { 0 };                                     \
                                                                        \
        for (i = 0; i < (1 << (n_halvings)); i++)                       \
        {                                                               \
            row_parts_in += *(ofs_x++) * 2;                             \
            F = *(ofs_x++);                                             \
                                                                        \
            p = row_parts_in [0];                                       \
            q = row_parts_in [2];                                       \
                                                                        \
            accum [0] += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL; \
                                                                        \
            p = row_parts_in [1];                                       \
            q = row_parts_in [3];                                       \
                                                                        \
            accum [1] += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL; \
        }                                                               \
        *(row_parts_out++) = ((accum [0]) >> (n_halvings)) & 0x00ffffff00ffffffULL; \
        *(row_parts_out++) = ((accum [1]) >> (n_halvings)) & 0x00ffffff00ffffffULL; \
    }                                                                   \
    while (row_parts_out != row_parts_out_max);                         \
}

static void
interp_horizontal_bilinear_0h_64bpp (const SmolScaleCtx *scale_ctx,
                                     const uint64_t * SMOL_RESTRICT row_parts_in,
                                     uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t p, q;
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t F;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out;

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    do
    {
        row_parts_in += *(ofs_x++);
        F = *(ofs_x++);

        p = *row_parts_in;
        q = *(row_parts_in + 1);

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (row_parts_out != row_parts_out_max);
}

static void
interp_horizontal_bilinear_0h_128bpp (const SmolScaleCtx *scale_ctx,
                                      const uint64_t * SMOL_RESTRICT row_parts_in,
                                      uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t p, q;
    const uint16_t * SMOL_RESTRICT ofs_x = scale_ctx->offsets_x;
    uint64_t F;
    uint64_t * SMOL_RESTRICT row_parts_out_max = row_parts_out + scale_ctx->width_out * 2;

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    do
    {
        row_parts_in += *(ofs_x++) * 2;
        F = *(ofs_x++);

        p = row_parts_in [0];
        q = row_parts_in [2];

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;

        p = row_parts_in [1];
        q = row_parts_in [3];

        *(row_parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
    while (row_parts_out != row_parts_out_max);
}

DEF_INTERP_HORIZONTAL_BILINEAR(1)
DEF_INTERP_HORIZONTAL_BILINEAR(2)
DEF_INTERP_HORIZONTAL_BILINEAR(3)
DEF_INTERP_HORIZONTAL_BILINEAR(4)
DEF_INTERP_HORIZONTAL_BILINEAR(5)
DEF_INTERP_HORIZONTAL_BILINEAR(6)

static void
interp_horizontal_boxes_64bpp (const SmolScaleCtx *scale_ctx,
                               const uint64_t *row_parts_in,
                               uint64_t * SMOL_RESTRICT row_parts_out)
{
    const uint64_t * SMOL_RESTRICT pp;
    const uint16_t *ofs_x = scale_ctx->offsets_x;
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out - 1;
    uint64_t accum = 0;
    uint64_t p, q, r, s;
    uint32_t n;
    uint64_t F;

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    pp = row_parts_in;
    p = weight_pixel_64bpp (*(pp++), 256);
    n = *(ofs_x++);

    while (row_parts_out != row_parts_out_max)
    {
        sum_parts_64bpp ((const uint64_t ** SMOL_RESTRICT) &pp, &accum, n);

        F = *(ofs_x++);
        n = *(ofs_x++);

        r = *(pp++);
        s = r * F;

        q = (s >> 8) & 0x00ff00ff00ff00ffULL;

        accum += p + q;

        /* (255 * r) - (F * r) */
        p = (((r << 8) - r - s) >> 8) & 0x00ff00ff00ff00ffULL;

        *(row_parts_out++) = scale_64bpp (accum, scale_ctx->span_mul_x);
        accum = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_parts_64bpp ((const uint64_t ** SMOL_RESTRICT) &pp, &accum, n);

    q = 0;
    F = *(ofs_x);
    if (F > 0)
        q = weight_pixel_64bpp (*(pp), F);

    accum += p + q;
    *(row_parts_out++) = scale_64bpp (accum, scale_ctx->span_mul_x);
}

static void
interp_horizontal_boxes_128bpp (const SmolScaleCtx *scale_ctx,
                                const uint64_t *row_parts_in,
                                uint64_t * SMOL_RESTRICT row_parts_out)
{
    const uint64_t * SMOL_RESTRICT pp;
    const uint16_t *ofs_x = scale_ctx->offsets_x;
    uint64_t *row_parts_out_max = row_parts_out + (scale_ctx->width_out - /* 2 */ 1) * 2;
    uint64_t accum [2] = { 0, 0 };
    uint64_t p [2], q [2], r [2], s [2];
    uint32_t n;
    uint64_t F;

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    pp = row_parts_in;

    p [0] = *(pp++);
    p [1] = *(pp++);
    weight_pixel_128bpp (p, p, 256);

    n = *(ofs_x++);

    while (row_parts_out != row_parts_out_max)
    {
        sum_parts_128bpp ((const uint64_t ** SMOL_RESTRICT) &pp, accum, n);

        F = *(ofs_x++);
        n = *(ofs_x++);

        r [0] = *(pp++);
        r [1] = *(pp++);

        s [0] = r [0] * F;
        s [1] = r [1] * F;

        q [0] = (s [0] >> 8) & 0x00ffffff00ffffff;
        q [1] = (s [1] >> 8) & 0x00ffffff00ffffff;

        accum [0] += p [0] + q [0];
        accum [1] += p [1] + q [1];

        p [0] = (((r [0] << 8) - r [0] - s [0]) >> 8) & 0x00ffffff00ffffff;
        p [1] = (((r [1] << 8) - r [1] - s [1]) >> 8) & 0x00ffffff00ffffff;

        scale_and_store_128bpp (accum,
                                scale_ctx->span_mul_x,
                                (uint64_t ** SMOL_RESTRICT) &row_parts_out);

        accum [0] = 0;
        accum [1] = 0;
    }

    /* Final box optionally features the rightmost fractional pixel */

    sum_parts_128bpp ((const uint64_t ** SMOL_RESTRICT) &pp, accum, n);

    q [0] = 0;
    q [1] = 0;

    F = *(ofs_x);
    if (F > 0)
    {
        q [0] = *(pp++);
        q [1] = *(pp++);
        weight_pixel_128bpp (q, q, F);
    }

    accum [0] += p [0] + q [0];
    accum [1] += p [1] + q [1];

    scale_and_store_128bpp (accum,
                            scale_ctx->span_mul_x,
                            (uint64_t ** SMOL_RESTRICT) &row_parts_out);
}

static void
interp_horizontal_one_64bpp (const SmolScaleCtx *scale_ctx,
                             const uint64_t * SMOL_RESTRICT row_parts_in,
                             uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out;
    uint64_t part;

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    part = *row_parts_in;
    while (row_parts_out != row_parts_out_max)
        *(row_parts_out++) = part;
}

static void
interp_horizontal_one_128bpp (const SmolScaleCtx *scale_ctx,
                              const uint64_t * SMOL_RESTRICT row_parts_in,
                              uint64_t * SMOL_RESTRICT row_parts_out)
{
    uint64_t *row_parts_out_max = row_parts_out + scale_ctx->width_out * 2;

    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    while (row_parts_out != row_parts_out_max)
    {
        *(row_parts_out++) = row_parts_in [0];
        *(row_parts_out++) = row_parts_in [1];
    }
}

static void
interp_horizontal_copy_64bpp (const SmolScaleCtx *scale_ctx,
                              const uint64_t * SMOL_RESTRICT row_parts_in,
                              uint64_t * SMOL_RESTRICT row_parts_out)
{
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    memcpy (row_parts_out, row_parts_in, scale_ctx->width_out * sizeof (uint64_t));
}

static void
interp_horizontal_copy_128bpp (const SmolScaleCtx *scale_ctx,
                               const uint64_t * SMOL_RESTRICT row_parts_in,
                               uint64_t * SMOL_RESTRICT row_parts_out)
{
    SMOL_ASSUME_ALIGNED (row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (row_parts_out, uint64_t *);

    memcpy (row_parts_out, row_parts_in, scale_ctx->width_out * 2 * sizeof (uint64_t));
}

static void
scale_horizontal (const SmolScaleCtx *scale_ctx,
                  SmolVerticalCtx *vertical_ctx,
                  const char *row_in,
                  uint64_t *row_parts_out)
{
    uint64_t * SMOL_RESTRICT unpacked_in;

    unpacked_in = vertical_ctx->parts_row [3];

    /* 32-bit unpackers need 32-bit alignment */
    if ((((uintptr_t) row_in) & 3)
        && scale_ctx->pixel_type_in != SMOL_PIXEL_RGB8
        && scale_ctx->pixel_type_in != SMOL_PIXEL_BGR8)
    {
        if (!vertical_ctx->in_aligned)
            vertical_ctx->in_aligned =
                smol_alloc_aligned (scale_ctx->width_in * sizeof (uint32_t),
                                    &vertical_ctx->in_aligned_storage);
        memcpy (vertical_ctx->in_aligned, row_in, scale_ctx->width_in * sizeof (uint32_t));
        row_in = (const char *) vertical_ctx->in_aligned;
    }

    scale_ctx->unpack_row_func ((const uint32_t *) row_in,
                                unpacked_in,
                                scale_ctx->width_in);
    if (scale_ctx->with_srgb)
        from_srgb_row_128bpp (unpacked_in, scale_ctx->width_in);
    scale_ctx->hfilter_func (scale_ctx,
                             unpacked_in,
                             row_parts_out);
}

/* --- Vertical scaling --- */

static void
update_vertical_ctx_bilinear (const SmolScaleCtx *scale_ctx,
                              SmolVerticalCtx *vertical_ctx,
                              uint32_t outrow_index)
{
    uint32_t new_in_ofs = scale_ctx->offsets_y [outrow_index * 2];

    if (new_in_ofs == vertical_ctx->in_ofs)
        return;

    if (new_in_ofs == vertical_ctx->in_ofs + 1)
    {
        uint64_t *t = vertical_ctx->parts_row [0];
        vertical_ctx->parts_row [0] = vertical_ctx->parts_row [1];
        vertical_ctx->parts_row [1] = t;

        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                          vertical_ctx->parts_row [1]);
    }
    else
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs),
                          vertical_ctx->parts_row [0]);
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, new_in_ofs + 1),
                          vertical_ctx->parts_row [1]);
    }

    vertical_ctx->in_ofs = new_in_ofs;
}

static void
interp_vertical_bilinear_store_64bpp (uint64_t F,
                                      const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                      const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                      uint64_t * SMOL_RESTRICT parts_out,
                                      uint32_t width)
{
    uint64_t *parts_out_last = parts_out + width;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (parts_out != parts_out_last);
}

static void
interp_vertical_bilinear_add_64bpp (uint64_t F,
                                    const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                    const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                    uint64_t * SMOL_RESTRICT accum_out,
                                    uint32_t width)
{
    uint64_t *accum_out_last = accum_out + width;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (accum_out, uint64_t *);

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(accum_out++) += ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;
    }
    while (accum_out != accum_out_last);
}

static void
interp_vertical_bilinear_store_128bpp (uint64_t F,
                                       const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                       const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                       uint64_t * SMOL_RESTRICT parts_out,
                                       uint32_t width)
{
    uint64_t *parts_out_last = parts_out + width;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(parts_out++) = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
    while (parts_out != parts_out_last);
}

static void
interp_vertical_bilinear_add_128bpp (uint64_t F,
                                     const uint64_t * SMOL_RESTRICT top_row_parts_in,
                                     const uint64_t * SMOL_RESTRICT bottom_row_parts_in,
                                     uint64_t * SMOL_RESTRICT accum_out,
                                     uint32_t width)
{
    uint64_t *accum_out_last = accum_out + width;

    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);
    SMOL_ASSUME_ALIGNED (accum_out, uint64_t *);

    do
    {
        uint64_t p, q;

        p = *(top_row_parts_in++);
        q = *(bottom_row_parts_in++);

        *(accum_out++) += ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;
    }
    while (accum_out != accum_out_last);
}

#define DEF_INTERP_VERTICAL_BILINEAR_FINAL(n_halvings)                  \
static void                                                             \
interp_vertical_bilinear_final_##n_halvings##h_64bpp (uint64_t F,                \
                                                      const uint64_t * SMOL_RESTRICT top_row_parts_in, \
                                                      const uint64_t * SMOL_RESTRICT bottom_row_parts_in, \
                                                      uint64_t * SMOL_RESTRICT accum_inout, \
                                                      uint32_t width)   \
{                                                                       \
    uint64_t *accum_inout_last = accum_inout + width;                   \
                                                                        \
    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);           \
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);        \
    SMOL_ASSUME_ALIGNED (accum_inout, uint64_t *);                      \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t p, q;                                                  \
                                                                        \
        p = *(top_row_parts_in++);                                      \
        q = *(bottom_row_parts_in++);                                   \
                                                                        \
        p = ((((p - q) * F) >> 8) + q) & 0x00ff00ff00ff00ffULL;         \
        p = ((p + *accum_inout) >> n_halvings) & 0x00ff00ff00ff00ffULL; \
                                                                        \
        *(accum_inout++) = p;                                           \
    }                                                                   \
    while (accum_inout != accum_inout_last);                            \
}                                                                       \
                                                                        \
static void                                                             \
interp_vertical_bilinear_final_##n_halvings##h_128bpp (uint64_t F,      \
                                                       const uint64_t * SMOL_RESTRICT top_row_parts_in, \
                                                       const uint64_t * SMOL_RESTRICT bottom_row_parts_in, \
                                                       uint64_t * SMOL_RESTRICT accum_inout, \
                                                       uint32_t width)  \
{                                                                       \
    uint64_t *accum_inout_last = accum_inout + width;                   \
                                                                        \
    SMOL_ASSUME_ALIGNED (top_row_parts_in, const uint64_t *);           \
    SMOL_ASSUME_ALIGNED (bottom_row_parts_in, const uint64_t *);        \
    SMOL_ASSUME_ALIGNED (accum_inout, uint64_t *);                      \
                                                                        \
    do                                                                  \
    {                                                                   \
        uint64_t p, q;                                                  \
                                                                        \
        p = *(top_row_parts_in++);                                      \
        q = *(bottom_row_parts_in++);                                   \
                                                                        \
        p = ((((p - q) * F) >> 8) + q) & 0x00ffffff00ffffffULL;         \
        p = ((p + *accum_inout) >> n_halvings) & 0x00ffffff00ffffffULL; \
                                                                        \
        *(accum_inout++) = p;                                           \
    }                                                                   \
    while (accum_inout != accum_inout_last);                            \
}

#define DEF_SCALE_OUTROW_BILINEAR(n_halvings)                           \
static void                                                             \
scale_outrow_bilinear_##n_halvings##h_64bpp (const SmolScaleCtx *scale_ctx, \
                                             SmolVerticalCtx *vertical_ctx, \
                                             uint32_t outrow_index,     \
                                             uint32_t *row_out)         \
{                                                                       \
    uint32_t bilin_index = outrow_index << (n_halvings);                \
    unsigned int i;                                                     \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_store_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                          vertical_ctx->parts_row [0],  \
                                          vertical_ctx->parts_row [1],  \
                                          vertical_ctx->parts_row [2],  \
                                          scale_ctx->width_out);        \
    bilin_index++;                                                      \
                                                                        \
    for (i = 0; i < (1 << (n_halvings)) - 2; i++)                       \
    {                                                                   \
        update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
        interp_vertical_bilinear_add_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                            vertical_ctx->parts_row [0], \
                                            vertical_ctx->parts_row [1], \
                                            vertical_ctx->parts_row [2], \
                                            scale_ctx->width_out);      \
        bilin_index++;                                                  \
    }                                                                   \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_final_##n_halvings##h_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                                          vertical_ctx->parts_row [0], \
                                                          vertical_ctx->parts_row [1], \
                                                          vertical_ctx->parts_row [2], \
                                                          scale_ctx->width_out); \
                                                                        \
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out); \
}                                                                       \
                                                                        \
static void                                                             \
scale_outrow_bilinear_##n_halvings##h_128bpp (const SmolScaleCtx *scale_ctx, \
                                              SmolVerticalCtx *vertical_ctx, \
                                              uint32_t outrow_index,    \
                                              uint32_t *row_out)        \
{                                                                       \
    uint32_t bilin_index = outrow_index << (n_halvings);                \
    unsigned int i;                                                     \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_store_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                           vertical_ctx->parts_row [0], \
                                           vertical_ctx->parts_row [1], \
                                           vertical_ctx->parts_row [2], \
                                           scale_ctx->width_out * 2);   \
    bilin_index++;                                                      \
                                                                        \
    for (i = 0; i < (1 << (n_halvings)) - 2; i++)                       \
    {                                                                   \
        update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
        interp_vertical_bilinear_add_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                             vertical_ctx->parts_row [0], \
                                             vertical_ctx->parts_row [1], \
                                             vertical_ctx->parts_row [2], \
                                             scale_ctx->width_out * 2); \
        bilin_index++;                                                  \
    }                                                                   \
                                                                        \
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index); \
    interp_vertical_bilinear_final_##n_halvings##h_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1], \
                                                           vertical_ctx->parts_row [0], \
                                                           vertical_ctx->parts_row [1], \
                                                           vertical_ctx->parts_row [2], \
                                                           scale_ctx->width_out * 2); \
                                                                        \
    if (scale_ctx->with_srgb)                                           \
        to_srgb_row_128bpp (vertical_ctx->parts_row [2], scale_ctx->width_out); \
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out); \
}

static void
scale_outrow_bilinear_0h_64bpp (const SmolScaleCtx *scale_ctx,
                                SmolVerticalCtx *vertical_ctx,
                                uint32_t outrow_index,
                                uint32_t *row_out)
{
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, outrow_index);
    interp_vertical_bilinear_store_64bpp (scale_ctx->offsets_y [outrow_index * 2 + 1],
                                          vertical_ctx->parts_row [0],
                                          vertical_ctx->parts_row [1],
                                          vertical_ctx->parts_row [2],
                                          scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

static void
scale_outrow_bilinear_0h_128bpp (const SmolScaleCtx *scale_ctx,
                                 SmolVerticalCtx *vertical_ctx,
                                 uint32_t outrow_index,
                                 uint32_t *row_out)
{
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, outrow_index);
    interp_vertical_bilinear_store_128bpp (scale_ctx->offsets_y [outrow_index * 2 + 1],
                                           vertical_ctx->parts_row [0],
                                           vertical_ctx->parts_row [1],
                                           vertical_ctx->parts_row [2],
                                           scale_ctx->width_out * 2);
    if (scale_ctx->with_srgb)
        to_srgb_row_128bpp (vertical_ctx->parts_row [2], scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

DEF_INTERP_VERTICAL_BILINEAR_FINAL(1)

static void
scale_outrow_bilinear_1h_64bpp (const SmolScaleCtx *scale_ctx,
                                SmolVerticalCtx *vertical_ctx,
                                uint32_t outrow_index,
                                uint32_t *row_out)
{
    uint32_t bilin_index = outrow_index << 1;

    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_store_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                          vertical_ctx->parts_row [0],
                                          vertical_ctx->parts_row [1],
                                          vertical_ctx->parts_row [2],
                                          scale_ctx->width_out);
    bilin_index++;
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_final_1h_64bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                             vertical_ctx->parts_row [0],
                                             vertical_ctx->parts_row [1],
                                             vertical_ctx->parts_row [2],
                                             scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

static void
scale_outrow_bilinear_1h_128bpp (const SmolScaleCtx *scale_ctx,
                                 SmolVerticalCtx *vertical_ctx,
                                 uint32_t outrow_index,
                                 uint32_t *row_out)
{
    uint32_t bilin_index = outrow_index << 1;

    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_store_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                           vertical_ctx->parts_row [0],
                                           vertical_ctx->parts_row [1],
                                           vertical_ctx->parts_row [2],
                                           scale_ctx->width_out * 2);
    bilin_index++;
    update_vertical_ctx_bilinear (scale_ctx, vertical_ctx, bilin_index);
    interp_vertical_bilinear_final_1h_128bpp (scale_ctx->offsets_y [bilin_index * 2 + 1],
                                              vertical_ctx->parts_row [0],
                                              vertical_ctx->parts_row [1],
                                              vertical_ctx->parts_row [2],
                                              scale_ctx->width_out * 2);
    if (scale_ctx->with_srgb)
        to_srgb_row_128bpp (vertical_ctx->parts_row [2], scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [2], row_out, scale_ctx->width_out);
}

DEF_INTERP_VERTICAL_BILINEAR_FINAL(2)
DEF_SCALE_OUTROW_BILINEAR(2)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(3)
DEF_SCALE_OUTROW_BILINEAR(3)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(4)
DEF_SCALE_OUTROW_BILINEAR(4)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(5)
DEF_SCALE_OUTROW_BILINEAR(5)
DEF_INTERP_VERTICAL_BILINEAR_FINAL(6)
DEF_SCALE_OUTROW_BILINEAR(6)

static void
finalize_vertical_64bpp (const uint64_t * SMOL_RESTRICT accums,
                         uint64_t multiplier,
                         uint64_t * SMOL_RESTRICT parts_out,
                         uint32_t n)
{
    uint64_t *parts_out_max = parts_out + n;

    SMOL_ASSUME_ALIGNED (accums, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

    while (parts_out != parts_out_max)
    {
        *(parts_out++) = scale_64bpp (*(accums++), multiplier);
    }
}

static void
weight_edge_row_64bpp (uint64_t *row,
                       uint16_t w,
                       uint32_t n)
{
    uint64_t *row_max = row + n;

    SMOL_ASSUME_ALIGNED (row, uint64_t *);

    while (row != row_max)
    {
        *row = ((*row * w) >> 8) & 0x00ff00ff00ff00ffULL;
        row++;
    }
}

static void
scale_and_weight_edge_rows_box_64bpp (const uint64_t * SMOL_RESTRICT first_row,
                                      uint64_t * SMOL_RESTRICT last_row,
                                      uint64_t * SMOL_RESTRICT accum,
                                      uint16_t w2,
                                      uint32_t n)
{
    const uint64_t *first_row_max = first_row + n;

    SMOL_ASSUME_ALIGNED (first_row, const uint64_t *);
    SMOL_ASSUME_ALIGNED (last_row, uint64_t *);
    SMOL_ASSUME_ALIGNED (accum, uint64_t *);

    while (first_row != first_row_max)
    {
        uint64_t r, s, p, q;

        p = *(first_row++);

        r = *(last_row);
        s = r * w2;
        q = (s >> 8) & 0x00ff00ff00ff00ffULL;
        /* (255 * r) - (F * r) */
        *(last_row++) = (((r << 8) - r - s) >> 8) & 0x00ff00ff00ff00ffULL;

        *(accum++) = p + q;
    }
}

static void
update_vertical_ctx_box_64bpp (const SmolScaleCtx *scale_ctx,
                             SmolVerticalCtx *vertical_ctx,
                             uint32_t ofs_y,
                             uint32_t ofs_y_max,
                             uint16_t w1,
                             uint16_t w2)
{
    /* Old in_ofs is the previous max */
    if (ofs_y == vertical_ctx->in_ofs)
    {
        uint64_t *t = vertical_ctx->parts_row [0];
        vertical_ctx->parts_row [0] = vertical_ctx->parts_row [1];
        vertical_ctx->parts_row [1] = t;
    }
    else
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [0]);
        weight_edge_row_64bpp (vertical_ctx->parts_row [0], w1, scale_ctx->width_out);
    }

    /* When w2 == 0, the final inrow may be out of bounds. Don't try to access it in
     * that case. */
    if (w2 || ofs_y_max < scale_ctx->height_in)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y_max),
                          vertical_ctx->parts_row [1]);
    }
    else
    {
        memset (vertical_ctx->parts_row [1], 0, scale_ctx->width_out * sizeof (uint64_t));
    }

    vertical_ctx->in_ofs = ofs_y_max;
}

static void
scale_outrow_box_64bpp (const SmolScaleCtx *scale_ctx,
                        SmolVerticalCtx *vertical_ctx,
                        uint32_t outrow_index,
                        uint32_t *row_out)
{
    uint32_t ofs_y, ofs_y_max;
    uint16_t w1, w2;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first and last rows, weight them and store in accumulator */

    w1 = (outrow_index == 0) ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1];
    w2 = scale_ctx->offsets_y [outrow_index * 2 + 1];

    update_vertical_ctx_box_64bpp (scale_ctx, vertical_ctx, ofs_y, ofs_y_max, w1, w2);

    scale_and_weight_edge_rows_box_64bpp (vertical_ctx->parts_row [0],
                                          vertical_ctx->parts_row [1],
                                          vertical_ctx->parts_row [2],
                                          w2,
                                          scale_ctx->width_out);

    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [0]);
        add_parts (vertical_ctx->parts_row [0],
                   vertical_ctx->parts_row [2],
                   scale_ctx->width_out);

        ofs_y++;
    }

    finalize_vertical_64bpp (vertical_ctx->parts_row [2],
                             scale_ctx->span_mul_y,
                             vertical_ctx->parts_row [0],
                             scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
finalize_vertical_128bpp (const uint64_t * SMOL_RESTRICT accums,
                          uint64_t multiplier,
                          uint64_t * SMOL_RESTRICT parts_out,
                          uint32_t n)
{
    uint64_t *parts_out_max = parts_out + n * 2;

    SMOL_ASSUME_ALIGNED (accums, const uint64_t *);
    SMOL_ASSUME_ALIGNED (parts_out, uint64_t *);

    while (parts_out != parts_out_max)
    {
        *(parts_out++) = scale_128bpp_half (*(accums++), multiplier);
        *(parts_out++) = scale_128bpp_half (*(accums++), multiplier);
    }
}

static void
weight_row_128bpp (uint64_t *row,
                   uint16_t w,
                   uint32_t n)
{
    uint64_t *row_max = row + (n * 2);

    SMOL_ASSUME_ALIGNED (row, uint64_t *);

    while (row != row_max)
    {
        row [0] = ((row [0] * w) >> 8) & 0x00ffffff00ffffffULL;
        row [1] = ((row [1] * w) >> 8) & 0x00ffffff00ffffffULL;
        row += 2;
    }
}

static void
scale_outrow_box_128bpp (const SmolScaleCtx *scale_ctx,
                         SmolVerticalCtx *vertical_ctx,
                         uint32_t outrow_index,
                         uint32_t *row_out)
{
    uint32_t ofs_y, ofs_y_max;
    uint16_t w;

    /* Get the inrow range for this outrow: [ofs_y .. ofs_y_max> */

    ofs_y = scale_ctx->offsets_y [outrow_index * 2];
    ofs_y_max = scale_ctx->offsets_y [(outrow_index + 1) * 2];

    /* Scale the first inrow and store it */

    scale_horizontal (scale_ctx,
                      vertical_ctx,
                      inrow_ofs_to_pointer (scale_ctx, ofs_y),
                      vertical_ctx->parts_row [0]);
    weight_row_128bpp (vertical_ctx->parts_row [0],
                       outrow_index == 0 ? 256 : 255 - scale_ctx->offsets_y [outrow_index * 2 - 1],
                       scale_ctx->width_out);
    ofs_y++;

    /* Add up whole rows */

    while (ofs_y < ofs_y_max)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [1]);
        add_parts (vertical_ctx->parts_row [1],
                   vertical_ctx->parts_row [0],
                   scale_ctx->width_out * 2);

        ofs_y++;
    }

    /* Final row is optional; if this is the bottommost outrow it could be out of bounds */

    w = scale_ctx->offsets_y [outrow_index * 2 + 1];
    if (w > 0)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, ofs_y),
                          vertical_ctx->parts_row [1]);
        weight_row_128bpp (vertical_ctx->parts_row [1],
                           w - 1,  /* Subtract 1 to avoid overflow */
                           scale_ctx->width_out);
        add_parts (vertical_ctx->parts_row [1],
                   vertical_ctx->parts_row [0],
                   scale_ctx->width_out * 2);
    }

    finalize_vertical_128bpp (vertical_ctx->parts_row [0],
                              scale_ctx->span_mul_y,
                              vertical_ctx->parts_row [1],
                              scale_ctx->width_out);
    if (scale_ctx->with_srgb)
        to_srgb_row_128bpp (vertical_ctx->parts_row [1], scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [1], row_out, scale_ctx->width_out);
}

static void
scale_outrow_one_64bpp (const SmolScaleCtx *scale_ctx,
                        SmolVerticalCtx *vertical_ctx,
                        uint32_t row_index,
                        uint32_t *row_out)
{
    SMOL_UNUSED (row_index);

    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, 0),
                          vertical_ctx->parts_row [0]);
        vertical_ctx->in_ofs = 0;
    }

    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow_one_128bpp (const SmolScaleCtx *scale_ctx,
                        SmolVerticalCtx *vertical_ctx,
                        uint32_t row_index,
                        uint32_t *row_out)
{
    SMOL_UNUSED (row_index);

    /* Scale the row and store it */

    if (vertical_ctx->in_ofs != 0)
    {
        scale_horizontal (scale_ctx,
                          vertical_ctx,
                          inrow_ofs_to_pointer (scale_ctx, 0),
                          vertical_ctx->parts_row [0]);
        if (scale_ctx->with_srgb)
            to_srgb_row_128bpp (vertical_ctx->parts_row [0], scale_ctx->width_out);
        vertical_ctx->in_ofs = 0;
    }

    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow_copy (const SmolScaleCtx *scale_ctx,
                   SmolVerticalCtx *vertical_ctx,
                   uint32_t row_index,
                   uint32_t *row_out)
{
    scale_horizontal (scale_ctx,
                      vertical_ctx,
                      inrow_ofs_to_pointer (scale_ctx, row_index),
                      vertical_ctx->parts_row [0]);

    if (scale_ctx->with_srgb)
        to_srgb_row_128bpp (vertical_ctx->parts_row [0], scale_ctx->width_out);
    scale_ctx->pack_row_func (vertical_ctx->parts_row [0], row_out, scale_ctx->width_out);
}

static void
scale_outrow (const SmolScaleCtx *scale_ctx,
              SmolVerticalCtx *vertical_ctx,
              uint32_t outrow_index,
              uint32_t *row_out)
{
    scale_ctx->vfilter_func (scale_ctx,
                             vertical_ctx,
                             outrow_index,
                             row_out);

    if (scale_ctx->post_row_func)
        scale_ctx->post_row_func (row_out, scale_ctx->width_out, scale_ctx->user_data);
}

static void
do_rows (const SmolScaleCtx *scale_ctx,
         void *outrows_dest,
         uint32_t row_out_index,
         uint32_t n_rows)
{
    SmolVerticalCtx vertical_ctx = { 0 };
    uint32_t n_parts_per_pixel = 1;
    uint32_t n_stored_rows = 4;
    uint32_t i;

    if (scale_ctx->storage_type == SMOL_STORAGE_128BPP)
        n_parts_per_pixel = 2;

    /* Must be one less, or this test in update_vertical_ctx() will wrap around:
     * if (new_in_ofs == vertical_ctx->in_ofs + 1) { ... } */
    vertical_ctx.in_ofs = UINT_MAX - 1;

    for (i = 0; i < n_stored_rows; i++)
    {
        vertical_ctx.parts_row [i] =
            smol_alloc_aligned (MAX (scale_ctx->width_in, scale_ctx->width_out)
                                * n_parts_per_pixel * sizeof (uint64_t),
                                &vertical_ctx.row_storage [i]);
    }

    for (i = row_out_index; i < row_out_index + n_rows; i++)
    {
        scale_outrow (scale_ctx, &vertical_ctx, i, outrows_dest);
        outrows_dest = (char *) outrows_dest + scale_ctx->rowstride_out;
    }

    for (i = 0; i < n_stored_rows; i++)
    {
        smol_free (vertical_ctx.row_storage [i]);
    }

    /* Used to align row data if needed. May be allocated in scale_horizontal(). */
    if (vertical_ctx.in_aligned)
        smol_free (vertical_ctx.in_aligned_storage);
}

/* --- Conversion tables --- */

static const SmolConversionTable generic_conversions =
{
{ {
    /* Conversions where accumulators must hold the sum of fewer than
     * 256 pixels. This can be done in 64bpp, but 128bpp may be used
     * e.g. for 16 bits per channel internally premultiplied data. */

    /* RGBA8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 1234, p, 64),
        /* BGRA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 3214, p, 64),
        /* ARGB8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 4123, p, 64),
        /* ABGR8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 4321, p, 64),
        /* RGBA8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 1234, u, 64),
        /* BGRA8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 3214, u, 64),
        /* ARGB8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 4123, u, 64),
        /* ABGR8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 4321, u, 64),
        /* RGB8      */ SMOL_CONV (1234, p, 1324, p, 132a, p, 123, u, 64),
        /* BGR8      */ SMOL_CONV (1234, p, 1324, p, 132a, p, 321, u, 64),
    },
    /* BGRA8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 3214, p, 64),
        /* BGRA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 1234, p, 64),
        /* ARGB8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 4321, p, 64),
        /* ABGR8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 4123, p, 64),
        /* RGBA8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 3214, u, 64),
        /* BGRA8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 1234, u, 64),
        /* ARGB8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 4321, u, 64),
        /* ABGR8 un  */ SMOL_CONV (1234, p, 1324, p, 132a, p, 4123, u, 64),
        /* RGB8      */ SMOL_CONV (1234, p, 1324, p, 132a, p, 321, u, 64),
        /* BGR8      */ SMOL_CONV (1234, p, 1324, p, 132a, p, 123, u, 64),
    },
    /* ARGB8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 2341, p, 64),
        /* BGRA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 4321, p, 64),
        /* ARGB8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 1234, p, 64),
        /* ABGR8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 1432, p, 64),
        /* RGBA8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 2341, u, 64),
        /* BGRA8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 4321, u, 64),
        /* ARGB8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 1234, u, 64),
        /* ABGR8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 1432, u, 64),
        /* RGB8      */ SMOL_CONV (1234, p, 1324, p, a324, p, 234, u, 64),
        /* BGR8      */ SMOL_CONV (1234, p, 1324, p, a324, p, 432, u, 64),
    },
    /* ABGR8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 4321, p, 64),
        /* BGRA8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 2341, p, 64),
        /* ARGB8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 1432, p, 64),
        /* ABGR8 pre */ SMOL_CONV (1234, p, 1324, p, 1324, p, 1234, p, 64),
        /* RGBA8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 4321, u, 64),
        /* BGRA8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 2341, u, 64),
        /* ARGB8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 1432, u, 64),
        /* ABGR8 un  */ SMOL_CONV (1234, p, 1324, p, a324, p, 1234, u, 64),
        /* RGB8      */ SMOL_CONV (1234, p, 1324, p, a324, p, 432, u, 64),
        /* BGR8      */ SMOL_CONV (1234, p, 1324, p, a324, p, 234, u, 64),
    },
    /* RGBA8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 1234, p, 64),
        /* BGRA8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 3214, p, 64),
        /* ARGB8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 4123, p, 64),
        /* ABGR8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 4321, p, 64),
        /* RGBA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a123, u, 128),
        /* ABGR8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a321, u, 128),
        /* RGB8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123, u, 128),
        /* BGR8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321, u, 128),
    },
    /* BGRA8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 3214, p, 64),
        /* BGRA8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 1234, p, 64),
        /* ARGB8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 4321, p, 64),
        /* ABGR8 pre */ SMOL_CONV (123a, u, 132a, p, 1324, p, 4123, p, 64),
        /* RGBA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a321, u, 128),
        /* ABGR8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a123, u, 128),
        /* RGB8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321, u, 128),
        /* BGR8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123, u, 128),
    },
    /* ARGB8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 2341, p, 64),
        /* BGRA8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 4321, p, 64),
        /* ARGB8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 1234, p, 64),
        /* ABGR8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 1432, p, 64),
        /* RGBA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a123, u, 128),
        /* ABGR8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a321, u, 128),
        /* RGB8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123, u, 128),
        /* BGR8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321, u, 128),
    },
    /* ABGR8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 4321, p, 64),
        /* BGRA8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 2341, p, 64),
        /* ARGB8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 1432, p, 64),
        /* ABGR8 pre */ SMOL_CONV (a234, u, a324, p, 1324, p, 1234, p, 64),
        /* RGBA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a321, u, 128),
        /* ABGR8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a123, u, 128),
        /* RGB8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321, u, 128),
        /* BGR8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123, u, 128),
    },
    /* RGB8 -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 1234, p, 64),
        /* BGRA8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 3214, p, 64),
        /* ARGB8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 4123, p, 64),
        /* ABGR8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 4321, p, 64),
        /* RGBA8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 1234, p, 64),
        /* BGRA8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 3214, p, 64),
        /* ARGB8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 4123, p, 64),
        /* ABGR8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 4321, p, 64),
        /* RGB8      */ SMOL_CONV (123, p, 132a, p, 132a, p, 123, p, 64),
        /* BGR8      */ SMOL_CONV (123, p, 132a, p, 132a, p, 321, p, 64),
    },
    /* BGR8 -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 3214, p, 64),
        /* BGRA8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 1234, p, 64),
        /* ARGB8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 4321, p, 64),
        /* ABGR8 pre */ SMOL_CONV (123, p, 132a, p, 1324, p, 4123, p, 64),
        /* RGBA8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 3214, p, 64),
        /* BGRA8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 1234, p, 64),
        /* ARGB8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 4321, p, 64),
        /* ABGR8 un  */ SMOL_CONV (123, p, 132a, p, 1324, p, 4123, p, 64),
        /* RGB8      */ SMOL_CONV (123, p, 132a, p, 132a, p, 321, p, 64),
        /* BGR8      */ SMOL_CONV (123, p, 132a, p, 132a, p, 123, p, 64),
    }
    },

    {
    /* Conversions where accumulators must hold the sum of up to
     * 65535 pixels. We need 128bpp for this. */

    /* RGBA8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, 123a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, 321a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, a123, p, 128),
        /* ABGR8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, a321, p, 128),
        /* RGBA8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, 123a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, 321a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, a123, u, 128),
        /* ABGR8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, a321, u, 128),
        /* RGB8      */ SMOL_CONV (123a, p, 123a, p, 123a, p, 123, u, 128),
        /* BGR8      */ SMOL_CONV (123a, p, 123a, p, 123a, p, 321, u, 128),
    },
    /* BGRA8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, 321a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, 123a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, a321, p, 128),
        /* ABGR8 pre */ SMOL_CONV (123a, p, 123a, p, 123a, p, a123, p, 128),
        /* RGBA8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, 321a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, 123a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, a321, u, 128),
        /* ABGR8 un  */ SMOL_CONV (123a, p, 123a, p, 123a, p, a123, u, 128),
        /* RGB8      */ SMOL_CONV (123a, p, 123a, p, 123a, p, 321, u, 128),
        /* BGR8      */ SMOL_CONV (123a, p, 123a, p, 123a, p, 123, u, 128),
    },
    /* ARGB8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, 123a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, 321a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, a123, p, 128),
        /* ABGR8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, a321, p, 128),
        /* RGBA8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, 123a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, 321a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, a123, u, 128),
        /* ABGR8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, a321, u, 128),
        /* RGB8      */ SMOL_CONV (a234, p, 234a, p, 123a, p, 123, u, 128),
        /* BGR8      */ SMOL_CONV (a234, p, 234a, p, 123a, p, 321, u, 128),
    },
    /* ABGR8 pre -> */
    {
        /* RGBA8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, 321a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, 123a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, a321, p, 128),
        /* ABGR8 pre */ SMOL_CONV (a234, p, 234a, p, 123a, p, a123, p, 128),
        /* RGBA8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, 321a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, 123a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, a321, u, 128),
        /* ABGR8 un  */ SMOL_CONV (a234, p, 234a, p, 123a, p, a123, u, 128),
        /* RGB8      */ SMOL_CONV (a234, p, 234a, p, 123a, p, 321, u, 128),
        /* BGR8      */ SMOL_CONV (a234, p, 234a, p, 123a, p, 123, u, 128),
    },
    /* RGBA8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, 123a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, 321a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, a123, p, 128),
        /* ABGR8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, a321, p, 128),
        /* RGBA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a123, u, 128),
        /* ABGR8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a321, u, 128),
        /* RGB8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123, u, 128),
        /* BGR8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321, u, 128),
    },
    /* BGRA8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, 321a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, 123a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, a321, p, 128),
        /* ABGR8 pre */ SMOL_CONV (123a, u, 123a, p, 123a, p, a123, p, 128),
        /* RGBA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a321, u, 128),
        /* ABGR8 un  */ SMOL_CONV (123a, u, 123a, i, 123a, i, a123, u, 128),
        /* RGB8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 321, u, 128),
        /* BGR8      */ SMOL_CONV (123a, u, 123a, i, 123a, i, 123, u, 128),
    },
    /* ARGB8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, 123a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, 321a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, a123, p, 128),
        /* ABGR8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, a321, p, 128),
        /* RGBA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a123, u, 128),
        /* ABGR8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a321, u, 128),
        /* RGB8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123, u, 128),
        /* BGR8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321, u, 128),
    },
    /* ABGR8 un -> */
    {
        /* RGBA8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, 321a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, 123a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, a321, p, 128),
        /* ABGR8 pre */ SMOL_CONV (a234, u, 234a, p, 123a, p, a123, p, 128),
        /* RGBA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321a, u, 128),
        /* BGRA8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123a, u, 128),
        /* ARGB8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a321, u, 128),
        /* ABGR8 un  */ SMOL_CONV (a234, u, 234a, i, 123a, i, a123, u, 128),
        /* RGB8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 321, u, 128),
        /* BGR8      */ SMOL_CONV (a234, u, 234a, i, 123a, i, 123, u, 128),
    },
    /* RGB8 -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, 123a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, 321a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, a123, p, 128),
        /* ABGR8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, a321, p, 128),
        /* RGBA8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, 123a, p, 128),
        /* BGRA8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, 321a, p, 128),
        /* ARGB8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, a123, p, 128),
        /* ABGR8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, a321, p, 128),
        /* RGB8      */ SMOL_CONV (123, p, 123a, p, 123a, p, 123, p, 128),
        /* BGR8      */ SMOL_CONV (123, p, 123a, p, 123a, p, 321, p, 128),
    },
    /* BGR8 -> */
    {
        /* RGBA8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, 321a, p, 128),
        /* BGRA8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, 123a, p, 128),
        /* ARGB8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, a321, p, 128),
        /* ABGR8 pre */ SMOL_CONV (123, p, 123a, p, 123a, p, a123, p, 128),
        /* RGBA8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, 321a, p, 128),
        /* BGRA8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, 123a, p, 128),
        /* ARGB8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, a321, p, 128),
        /* ABGR8 un  */ SMOL_CONV (123, p, 123a, p, 123a, p, a123, p, 128),
        /* RGB8      */ SMOL_CONV (123, p, 123a, p, 123a, p, 321, p, 128),
        /* BGR8      */ SMOL_CONV (123, p, 123a, p, 123a, p, 123, p, 128),
    }
} }
};

/* Keep in sync with the private SmolReorderType enum */
static const SmolReorderMeta reorder_meta [SMOL_REORDER_MAX] =
{
    { { 1, 2, 3, 4 }, { 1, 2, 3, 4 } },

    { { 1, 2, 3, 4 }, { 2, 3, 4, 1 } },
    { { 1, 2, 3, 4 }, { 3, 2, 1, 4 } },
    { { 1, 2, 3, 4 }, { 4, 1, 2, 3 } },
    { { 1, 2, 3, 4 }, { 4, 3, 2, 1 } },
    { { 1, 2, 3, 4 }, { 1, 2, 3, 0 } },
    { { 1, 2, 3, 4 }, { 3, 2, 1, 0 } },
    { { 1, 2, 3, 0 }, { 1, 2, 3, 4 } },

    { { 1, 2, 3, 4 }, { 1, 3, 2, 4 } },
    { { 1, 2, 3, 4 }, { 2, 3, 1, 4 } },
    { { 1, 2, 3, 4 }, { 4, 1, 3, 2 } },
    { { 1, 2, 3, 4 }, { 4, 2, 3, 1 } },
    { { 1, 2, 3, 4 }, { 1, 3, 2, 0 } },
    { { 1, 2, 3, 4 }, { 2, 3, 1, 0 } },
    { { 1, 2, 3, 0 }, { 1, 3, 2, 4 } },
};

#define R SMOL_REPACK_META

static const SmolRepackMeta generic_repack_meta [] =
{
    R (123,   24, PREMULTIPLIED, COMPRESSED, 1324,  64, PREMULTIPLIED, COMPRESSED),

    R (123,   24, PREMULTIPLIED, COMPRESSED, 1234, 128, PREMULTIPLIED, COMPRESSED),

    R (1234,  32, PREMULTIPLIED, COMPRESSED, 1324,  64, PREMULTIPLIED, COMPRESSED),
    R (1234,  32, UNASSOCIATED,  COMPRESSED, 1324,  64, PREMULTIPLIED, COMPRESSED),

    R (1234,  32, PREMULTIPLIED, COMPRESSED, 1234, 128, PREMULTIPLIED, COMPRESSED),
    R (1234,  32, UNASSOCIATED,  COMPRESSED, 1234, 128, PREMULTIPLIED, COMPRESSED),
    R (1234,  32, UNASSOCIATED,  COMPRESSED, 2341, 128, PREMULTIPLIED, COMPRESSED),
    R (1234,  32, UNASSOCIATED,  COMPRESSED, 1234, 128, UPMULTIPLIED,  COMPRESSED),
    R (1234,  32, UNASSOCIATED,  COMPRESSED, 2341, 128, UPMULTIPLIED,  COMPRESSED),

    R (1234,  64, PREMULTIPLIED, COMPRESSED, 132,   24, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 231,   24, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 132,   24, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 231,   24, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 324,   24, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 423,   24, UNASSOCIATED,  COMPRESSED),

    R (1234,  64, PREMULTIPLIED, COMPRESSED, 1324,  32, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 1423,  32, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 2314,  32, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 3241,  32, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 4132,  32, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 4231,  32, PREMULTIPLIED, COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 1324,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 1423,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 2314,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 3241,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 4132,  32, UNASSOCIATED,  COMPRESSED),
    R (1234,  64, PREMULTIPLIED, COMPRESSED, 4231,  32, UNASSOCIATED,  COMPRESSED),

    R (1234, 128, PREMULTIPLIED, COMPRESSED, 123,   24, PREMULTIPLIED, COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 321,   24, PREMULTIPLIED, COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 123,   24, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 321,   24, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, UPMULTIPLIED,  COMPRESSED, 123,   24, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, UPMULTIPLIED,  COMPRESSED, 321,   24, UNASSOCIATED,  COMPRESSED),

    R (1234, 128, PREMULTIPLIED, COMPRESSED, 1234,  32, PREMULTIPLIED, COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 3214,  32, PREMULTIPLIED, COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 4123,  32, PREMULTIPLIED, COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 4321,  32, PREMULTIPLIED, COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 1234,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 3214,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 4123,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, PREMULTIPLIED, COMPRESSED, 4321,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, UPMULTIPLIED,  COMPRESSED, 1234,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, UPMULTIPLIED,  COMPRESSED, 3214,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, UPMULTIPLIED,  COMPRESSED, 4123,  32, UNASSOCIATED,  COMPRESSED),
    R (1234, 128, UPMULTIPLIED,  COMPRESSED, 4321,  32, UNASSOCIATED,  COMPRESSED),

    SMOL_REPACK_META_LAST
};

/* Keep in sync with the public SmolPixelType enum */
static const SmolPixelTypeMeta pixel_type_meta [SMOL_PIXEL_MAX] =
{
    /* RGBA = 1, 2, 3, 4 */
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMULTIPLIED, { 1, 2, 3, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMULTIPLIED, { 3, 2, 1, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMULTIPLIED, { 4, 1, 2, 3 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMULTIPLIED, { 4, 3, 2, 1 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED,  { 1, 2, 3, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED,  { 3, 2, 1, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED,  { 4, 1, 2, 3 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED,  { 4, 3, 2, 1 } },
    { SMOL_STORAGE_24BPP, SMOL_ALPHA_PREMULTIPLIED, { 1, 2, 3, 0 } },
    { SMOL_STORAGE_24BPP, SMOL_ALPHA_PREMULTIPLIED, { 3, 2, 1, 0 } }
};

/* Channel ordering corrected for little endian. Only applies when fetching
 * entire pixels as dwords (i.e. u32). Keep in sync with the public
 * SmolPixelType enum */
static const SmolPixelType pixel_type_u32_le [SMOL_PIXEL_MAX] =
{
    SMOL_PIXEL_ABGR8_PREMULTIPLIED,
    SMOL_PIXEL_ARGB8_PREMULTIPLIED,
    SMOL_PIXEL_BGRA8_PREMULTIPLIED,
    SMOL_PIXEL_RGBA8_PREMULTIPLIED,
    SMOL_PIXEL_ABGR8_UNASSOCIATED,
    SMOL_PIXEL_ARGB8_UNASSOCIATED,
    SMOL_PIXEL_BGRA8_UNASSOCIATED,
    SMOL_PIXEL_RGBA8_UNASSOCIATED,
    SMOL_PIXEL_RGB8,
    SMOL_PIXEL_BGR8
};

static const SmolImplementation generic_implementation =
{
    {
        /* Horizontal filters */
        {
            /* 64bpp */
            interp_horizontal_copy_64bpp,
            interp_horizontal_one_64bpp,
            interp_horizontal_bilinear_0h_64bpp,
            interp_horizontal_bilinear_1h_64bpp,
            interp_horizontal_bilinear_2h_64bpp,
            interp_horizontal_bilinear_3h_64bpp,
            interp_horizontal_bilinear_4h_64bpp,
            interp_horizontal_bilinear_5h_64bpp,
            interp_horizontal_bilinear_6h_64bpp,
            interp_horizontal_boxes_64bpp
        },
        {
            /* 128bpp */
            interp_horizontal_copy_128bpp,
            interp_horizontal_one_128bpp,
            interp_horizontal_bilinear_0h_128bpp,
            interp_horizontal_bilinear_1h_128bpp,
            interp_horizontal_bilinear_2h_128bpp,
            interp_horizontal_bilinear_3h_128bpp,
            interp_horizontal_bilinear_4h_128bpp,
            interp_horizontal_bilinear_5h_128bpp,
            interp_horizontal_bilinear_6h_128bpp,
            interp_horizontal_boxes_128bpp
        }
    },
    {
        /* Vertical filters */
        {
            /* 64bpp */
            scale_outrow_copy,
            scale_outrow_one_64bpp,
            scale_outrow_bilinear_0h_64bpp,
            scale_outrow_bilinear_1h_64bpp,
            scale_outrow_bilinear_2h_64bpp,
            scale_outrow_bilinear_3h_64bpp,
            scale_outrow_bilinear_4h_64bpp,
            scale_outrow_bilinear_5h_64bpp,
            scale_outrow_bilinear_6h_64bpp,
            scale_outrow_box_64bpp
        },
        {
            /* 128bpp */
            scale_outrow_copy,
            scale_outrow_one_128bpp,
            scale_outrow_bilinear_0h_128bpp,
            scale_outrow_bilinear_1h_128bpp,
            scale_outrow_bilinear_2h_128bpp,
            scale_outrow_bilinear_3h_128bpp,
            scale_outrow_bilinear_4h_128bpp,
            scale_outrow_bilinear_5h_128bpp,
            scale_outrow_bilinear_6h_128bpp,
            scale_outrow_box_128bpp
        }
    },
    &generic_conversions
};

/* In the absence of a proper build system, runtime detection is more
   portable than compiler macros. WFM. */
static SmolBool
host_is_little_endian (void)
{
    static const union
    {
        uint8_t u8 [4];
        uint32_t u32;
    }
    host_bytes = { { 0, 1, 2, 3 } };

    if (host_bytes.u32 == 0x03020100UL)
        return TRUE;

    return FALSE;
}

/* The generic unpack/pack functions fetch and store pixels as u32.
 * This means the byte order will be reversed on little endian, with
 * consequences for the alpha channel and reordering logic. We deal
 * with this by using the apparent byte order internally. */
static SmolPixelType
get_host_pixel_type (SmolPixelType pixel_type)
{
    SmolPixelType host_pixel_type = SMOL_PIXEL_MAX;

    if (!host_is_little_endian ())
        return pixel_type;

    /* We use a switch for this so the compiler can remind us
     * to keep it in sync with the SmolPixelType enum. */
    switch (pixel_type)
    {
        case SMOL_PIXEL_RGBA8_PREMULTIPLIED:
            host_pixel_type = SMOL_PIXEL_ABGR8_PREMULTIPLIED; break;
        case SMOL_PIXEL_BGRA8_PREMULTIPLIED:
            host_pixel_type = SMOL_PIXEL_ARGB8_PREMULTIPLIED; break;
        case SMOL_PIXEL_ARGB8_PREMULTIPLIED:
            host_pixel_type = SMOL_PIXEL_BGRA8_PREMULTIPLIED; break;
        case SMOL_PIXEL_ABGR8_PREMULTIPLIED:
            host_pixel_type = SMOL_PIXEL_RGBA8_PREMULTIPLIED; break;
        case SMOL_PIXEL_RGBA8_UNASSOCIATED:
            host_pixel_type = SMOL_PIXEL_ABGR8_UNASSOCIATED; break;
        case SMOL_PIXEL_BGRA8_UNASSOCIATED:
            host_pixel_type = SMOL_PIXEL_ARGB8_UNASSOCIATED; break;
        case SMOL_PIXEL_ARGB8_UNASSOCIATED:
            host_pixel_type = SMOL_PIXEL_BGRA8_UNASSOCIATED; break;
        case SMOL_PIXEL_ABGR8_UNASSOCIATED:
            host_pixel_type = SMOL_PIXEL_RGBA8_UNASSOCIATED; break;
        case SMOL_PIXEL_RGB8:
            host_pixel_type = SMOL_PIXEL_RGB8; break;
        case SMOL_PIXEL_BGR8:
            host_pixel_type = SMOL_PIXEL_BGR8; break;
        case SMOL_PIXEL_MAX:
            host_pixel_type = SMOL_PIXEL_MAX; break;
    }

    return host_pixel_type;
}

#ifdef SMOL_WITH_AVX2

static SmolBool
have_avx2 (void)
{
    __builtin_cpu_init ();

    if (__builtin_cpu_supports ("avx2"))
        return TRUE;

    return FALSE;
}

#endif

static void
try_override_conversion (SmolScaleCtx *scale_ctx,
                         const SmolImplementation *impl,
                         SmolPixelType ptype_in,
                         SmolPixelType ptype_out,
                         uint8_t *n_bytes_per_pixel)
{
    const SmolConversion *conv;

    conv = &impl->ctab->conversions
        [scale_ctx->storage_type] [ptype_in] [ptype_out];

    if (conv->unpack_row_func && conv->pack_row_func)
    {
        *n_bytes_per_pixel = conv->n_bytes_per_pixel;
        scale_ctx->unpack_row_func = conv->unpack_row_func;
        scale_ctx->pack_row_func = conv->pack_row_func;
    }
}

static void
try_override_filters (SmolScaleCtx *scale_ctx,
                      const SmolImplementation *impl)
{
    SmolHFilterFunc *hfilter_func;
    SmolVFilterFunc *vfilter_func;

    hfilter_func = impl->hfilter_funcs
        [scale_ctx->storage_type] [scale_ctx->filter_h];
    vfilter_func = impl->vfilter_funcs
        [scale_ctx->storage_type] [scale_ctx->filter_v];

    if (hfilter_func)
        scale_ctx->hfilter_func = hfilter_func;
    if (vfilter_func)
        scale_ctx->vfilter_func = vfilter_func;
}

static void
get_implementations (SmolScaleCtx *scale_ctx)
{
    const SmolConversion *conv;
    SmolPixelType ptype_in, ptype_out;
    uint8_t n_bytes_per_pixel;
    const SmolImplementation *avx2_impl = NULL;

#ifdef SMOL_WITH_AVX2
    if (have_avx2 ())
        avx2_impl = _smol_get_avx2_implementation ();
#endif

    ptype_in = get_host_pixel_type (scale_ctx->pixel_type_in);
    ptype_out = get_host_pixel_type (scale_ctx->pixel_type_out);

    /* Install generic unpack()/pack() */

    conv = &generic_implementation.ctab->conversions
        [scale_ctx->storage_type] [ptype_in] [ptype_out];

    n_bytes_per_pixel = conv->n_bytes_per_pixel;
    scale_ctx->unpack_row_func = conv->unpack_row_func;
    scale_ctx->pack_row_func = conv->pack_row_func;

    /* Try to override with better unpack()/pack() implementations */

    if (avx2_impl)
        try_override_conversion (scale_ctx, avx2_impl,
                                 ptype_in, ptype_out,
                                 &n_bytes_per_pixel);

    /* Some conversions require extra precision. This can only ever
     * upgrade the storage from 64bpp to 128bpp, but we handle both
     * cases here for clarity. */
    if (n_bytes_per_pixel == 8)
        scale_ctx->storage_type = SMOL_STORAGE_64BPP;
    else if (n_bytes_per_pixel == 16)
        scale_ctx->storage_type = SMOL_STORAGE_128BPP;
    else
    {
        assert (n_bytes_per_pixel == 8 || n_bytes_per_pixel == 16);
    }

    /* Install generic filters */

    scale_ctx->hfilter_func = generic_implementation.hfilter_funcs
        [scale_ctx->storage_type] [scale_ctx->filter_h];
    scale_ctx->vfilter_func = generic_implementation.vfilter_funcs
        [scale_ctx->storage_type] [scale_ctx->filter_v];

    /* Try to override with better filter implementations */

    if (avx2_impl)
        try_override_filters (scale_ctx, avx2_impl);
}

static void
smol_scale_init (SmolScaleCtx *scale_ctx,
                 const void *pixels_in,
                 SmolPixelType pixel_type_in,
                 uint32_t width_in,
                 uint32_t height_in,
                 uint32_t rowstride_in,
                 void *pixels_out,
                 SmolPixelType pixel_type_out,
                 uint32_t width_out,
                 uint32_t height_out,
                 uint32_t rowstride_out,
                 uint8_t with_srgb,
                 SmolPostRowFunc post_row_func,
                 void *user_data)
{
    SmolStorageType storage_type [2];

    scale_ctx->pixels_in = pixels_in;
    scale_ctx->pixel_type_in = pixel_type_in;
    scale_ctx->width_in = width_in;
    scale_ctx->height_in = height_in;
    scale_ctx->rowstride_in = rowstride_in;
    scale_ctx->pixels_out = pixels_out;
    scale_ctx->pixel_type_out = pixel_type_out;
    scale_ctx->width_out = width_out;
    scale_ctx->height_out = height_out;
    scale_ctx->rowstride_out = rowstride_out;
    scale_ctx->with_srgb = with_srgb;

    scale_ctx->post_row_func = post_row_func;
    scale_ctx->user_data = user_data;

    pick_filter_params (width_in, width_out,
                        &scale_ctx->width_halvings,
                        &scale_ctx->width_bilin_out,
                        &scale_ctx->filter_h,
                        &storage_type [0],
                        with_srgb);
    pick_filter_params (height_in, height_out,
                        &scale_ctx->height_halvings,
                        &scale_ctx->height_bilin_out,
                        &scale_ctx->filter_v,
                        &storage_type [1],
                        with_srgb);

    scale_ctx->storage_type = MAX (storage_type [0], storage_type [1]);

    scale_ctx->offsets_x = malloc (((scale_ctx->width_bilin_out + 1) * 2
                                    + (scale_ctx->height_bilin_out + 1) * 2) * sizeof (uint16_t));
    scale_ctx->offsets_y = scale_ctx->offsets_x + (scale_ctx->width_bilin_out + 1) * 2;

    if (scale_ctx->filter_h == SMOL_FILTER_ONE
        || scale_ctx->filter_h == SMOL_FILTER_COPY)
    {
    }
    else if (scale_ctx->filter_h == SMOL_FILTER_BOX)
    {
        precalc_boxes_array (scale_ctx->offsets_x, &scale_ctx->span_mul_x,
                             width_in, scale_ctx->width_out, FALSE);
    }
    else /* SMOL_FILTER_BILINEAR_?H */
    {
        precalc_bilinear_array (scale_ctx->offsets_x,
                                width_in, scale_ctx->width_bilin_out, FALSE);
    }

    if (scale_ctx->filter_v == SMOL_FILTER_ONE
        || scale_ctx->filter_v == SMOL_FILTER_COPY)
    {
    }
    else if (scale_ctx->filter_v == SMOL_FILTER_BOX)
    {
        precalc_boxes_array (scale_ctx->offsets_y, &scale_ctx->span_mul_y,
                             height_in, scale_ctx->height_out, TRUE);
    }
    else /* SMOL_FILTER_BILINEAR_?H */
    {
        precalc_bilinear_array (scale_ctx->offsets_y,
                                height_in, scale_ctx->height_bilin_out, TRUE);
    }

    get_implementations (scale_ctx);
}

static void
smol_scale_finalize (SmolScaleCtx *scale_ctx)
{
    free (scale_ctx->offsets_x);
}

/* --- Public API --- */

SmolScaleCtx *
smol_scale_new (const void *pixels_in,
                SmolPixelType pixel_type_in,
                uint32_t width_in,
                uint32_t height_in,
                uint32_t rowstride_in,
                void *pixels_out,
                SmolPixelType pixel_type_out,
                uint32_t width_out,
                uint32_t height_out,
                uint32_t rowstride_out,
                uint8_t with_srgb)
{
    SmolScaleCtx *scale_ctx;

    scale_ctx = calloc (sizeof (SmolScaleCtx), 1);
    smol_scale_init (scale_ctx,
                     pixels_in,
                     pixel_type_in,
                     width_in,
                     height_in,
                     rowstride_in,
                     pixels_out,
                     pixel_type_out,
                     width_out,
                     height_out,
                     rowstride_out,
                     with_srgb,
                     NULL,
                     NULL);
    return scale_ctx;
}

SmolScaleCtx *
smol_scale_new_full (const void *pixels_in,
                     SmolPixelType pixel_type_in,
                     uint32_t width_in,
                     uint32_t height_in,
                     uint32_t rowstride_in,
                     void *pixels_out,
                     SmolPixelType pixel_type_out,
                     uint32_t width_out,
                     uint32_t height_out,
                     uint32_t rowstride_out,
                     uint8_t with_srgb,
                     SmolPostRowFunc post_row_func,
                     void *user_data)
{
    SmolScaleCtx *scale_ctx;

    scale_ctx = calloc (sizeof (SmolScaleCtx), 1);
    smol_scale_init (scale_ctx,
                     pixels_in,
                     pixel_type_in,
                     width_in,
                     height_in,
                     rowstride_in,
                     pixels_out,
                     pixel_type_out,
                     width_out,
                     height_out,
                     rowstride_out,
                     with_srgb,
                     post_row_func,
                     user_data);
    return scale_ctx;
}

void
smol_scale_destroy (SmolScaleCtx *scale_ctx)
{
    smol_scale_finalize (scale_ctx);
    free (scale_ctx);
}

void
smol_scale_simple (const void *pixels_in,
                   SmolPixelType pixel_type_in,
                   uint32_t width_in,
                   uint32_t height_in,
                   uint32_t rowstride_in,
                   void *pixels_out,
                   SmolPixelType pixel_type_out,
                   uint32_t width_out,
                   uint32_t height_out,
                   uint32_t rowstride_out,
                   uint8_t with_srgb)

{
    SmolScaleCtx scale_ctx;

    smol_scale_init (&scale_ctx,
                     pixels_in, pixel_type_in,
                     width_in, height_in, rowstride_in,
                     pixels_out, pixel_type_out,
                     width_out, height_out, rowstride_out,
                     with_srgb,
                     NULL, NULL);
    do_rows (&scale_ctx,
             outrow_ofs_to_pointer (&scale_ctx, 0),
             0,
             scale_ctx.height_out);
    smol_scale_finalize (&scale_ctx);
}

void
smol_scale_batch (const SmolScaleCtx *scale_ctx,
                  uint32_t first_out_row,
                  uint32_t n_out_rows)
{
    do_rows (scale_ctx,
             outrow_ofs_to_pointer (scale_ctx, first_out_row),
             first_out_row,
             n_out_rows);
}

void
smol_scale_batch_full (const SmolScaleCtx *scale_ctx,
                       void *outrows_dest,
                       uint32_t first_out_row,
                       uint32_t n_out_rows)
{
    do_rows (scale_ctx,
             outrows_dest,
             first_out_row,
             n_out_rows);
}
