/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Copyright © 2019-2023 Hans Petter Jansson. See COPYING for details. */

#if 0
#include <stdio.h>
#endif

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc, free, alloca */
#include <string.h> /* memset */
#include <limits.h>
#include "smolscale-private.h"

/* ----------------------------------- *
 * sRGB/linear conversion: Shared code *
 * ----------------------------------- */

/* These tables are manually tweaked to be reversible without information
 * loss; _smol_to_srgb_lut [_smol_from_srgb_lut [i]] == i.
 *
 * As a side effect, the values in the lower range (first 35 indexes) are
 * off by < 2%. */

const uint16_t _smol_from_srgb_lut [256] =
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

const uint8_t _smol_to_srgb_lut [SRGB_LINEAR_MAX] =
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

/* ------------------------------ *
 * Premultiplication: Shared code *
 * ------------------------------ */

/* These tables are used to divide by an integer [1..255] using only a lookup,
 * multiplication and a shift. This is faster than plain division on most
 * architectures.
 *
 * The values are tuned to minimize the error and overhead when turning
 * premultiplied (8-bit, 11-bit, 16-bit, 19-bit) into 8-bit unassociated alpha. */

/* Lossy premultiplication: 8-bit * alpha -> 8-bit. Not perfectly reversible. */
const uint32_t _smol_inv_div_p8_lut [256] =
{
    0x00000000, 0x00181fff, 0x000e2fff, 0x0009f555, 0x0007a7ff, 0x00063333, 0x00052555, 0x00047999,
    0x0003ebff, 0x0003838e, 0x00032333, 0x0002e2e8, 0x0002a2aa, 0x0002713b, 0x00024249, 0x00021ccc,
    0x0001f924, 0x0001dd17, 0x0001c1c7, 0x0001ab4b, 0x000195e5, 0x0001830c, 0x000170c3, 0x00016164,
    0x0001537a, 0x0001450d, 0x0001390b, 0x00012de9, 0x00012249, 0x00011846, 0x00010eaa, 0x0001069e,
    0x0000fd70, 0x0000f6aa, 0x0000eedb, 0x0000e8f5, 0x0000e1c7, 0x0000db8e, 0x0000d638, 0x0000d069,
    0x0000cb7c, 0x0000c690, 0x0000c186, 0x0000bd2d, 0x0000b8f9, 0x0000b4f7, 0x0000b0ff, 0x0000ad65,
    0x0000a9ac, 0x0000a687, 0x0000a286, 0x00009f33, 0x00009c98, 0x000099b9, 0x000096f1, 0x00009414,
    0x00009147, 0x00008efa, 0x00008c59, 0x00008a0a, 0x000087b4, 0x0000856c, 0x00008341, 0x0000818c,
    0x00007f55, 0x00007d60, 0x00007b7f, 0x000079b2, 0x000077b9, 0x00007608, 0x0000743c, 0x000072b5,
    0x0000711a, 0x00006fac, 0x00006e1a, 0x00006cad, 0x00006b17, 0x000069e1, 0x00006864, 0x00006736,
    0x000065db, 0x000064b1, 0x00006357, 0x00006250, 0x000060c5, 0x00006060, 0x00005ec0, 0x00005da5,
    0x00005c9b, 0x00005b8b, 0x00005a93, 0x000059ab, 0x00005884, 0x00005799, 0x000056ae, 0x000055d5,
    0x000054e2, 0x0000540b, 0x00005343, 0x00005255, 0x0000517c, 0x000050a7, 0x00004fff, 0x00004f2c,
    0x00004e5e, 0x00004d9f, 0x00004cec, 0x00004c34, 0x00004b78, 0x00004adc, 0x00004a23, 0x00004981,
    0x000048ce, 0x00004836, 0x0000478c, 0x000046eb, 0x00004656, 0x000045b6, 0x00004524, 0x0000449c,
    0x000043ff, 0x00004370, 0x000042e2, 0x00004257, 0x000041ce, 0x00004147, 0x000040c3, 0x00004081,
    0x00003fff, 0x00003f57, 0x00003ed3, 0x00003e54, 0x00003dd9, 0x00003d60, 0x00003ced, 0x00003c78,
    0x00003c07, 0x00003b9a, 0x00003b26, 0x00003abf, 0x00003a4f, 0x000039e1, 0x0000397e, 0x00003917,
    0x000038af, 0x00003848, 0x000037ee, 0x00003787, 0x00003726, 0x000036c9, 0x0000366b, 0x0000360d,
    0x000035b0, 0x00003567, 0x00003503, 0x000034aa, 0x00003453, 0x000033ff, 0x000033a8, 0x0000335c,
    0x00003305, 0x000032b3, 0x00003266, 0x00003213, 0x000031c7, 0x00003178, 0x0000312b, 0x000030df,
    0x00003094, 0x00003049, 0x00003018, 0x00002fc0, 0x00002f76, 0x00002f2d, 0x00002ee8, 0x00002ea6,
    0x00002e5f, 0x00002e1c, 0x00002dd9, 0x00002d99, 0x00002d59, 0x00002d17, 0x00002cdf, 0x00002c9b,
    0x00002c5d, 0x00002c1c, 0x00002be1, 0x00002ba6, 0x00002b6a, 0x00002b2e, 0x00002af3, 0x00002ac7,
    0x00002a85, 0x00002a4a, 0x00002a11, 0x000029dc, 0x000029a6, 0x0000296e, 0x00002936, 0x00002904,
    0x000028cd, 0x0000289a, 0x00002866, 0x00002833, 0x0000280a, 0x000027d0, 0x0000279e, 0x0000276f,
    0x0000273c, 0x0000270d, 0x000026de, 0x000026ad, 0x0000267e, 0x00002652, 0x00002622, 0x000025f5,
    0x000025c9, 0x0000259b, 0x0000256f, 0x00002545, 0x00002518, 0x000024ef, 0x000024c3, 0x0000249c,
    0x0000246f, 0x00002446, 0x0000241c, 0x000023f4, 0x000023ca, 0x000023a2, 0x0000237b, 0x00002354,
    0x0000232e, 0x00002306, 0x000022e0, 0x000022b9, 0x00002294, 0x0000226f, 0x0000224b, 0x00002226,
    0x00002202, 0x000021dc, 0x000021b8, 0x00002195, 0x00002172, 0x0000214f, 0x0000212c, 0x0000210a,
    0x000020e7, 0x000020c5, 0x000020a4, 0x00002083, 0x00002061, 0x00002041, 0x00002020, 0x00002020
};

/* Lossy premultiplication: 11-bit * alpha -> 11-bit. Not perfectly reversible. */
const uint32_t _smol_inv_div_p8l_lut [256] =
{
    0x00000000, 0x00fedfff, 0x007f7fff, 0x00550aaa, 0x003fbfff, 0x00330666, 0x002a8555, 0x00247249,
    0x001fe3ff, 0x001c58e3, 0x00198333, 0x001730f0, 0x00154228, 0x00139fae, 0x00123924, 0x00110222,
    0x000ff1ff, 0x000f01e1, 0x000e2c71, 0x000d62b8, 0x000cb872, 0x000c1e4b, 0x000b9214, 0x000b11fd,
    0x000a9c84, 0x000a3065, 0x0009cc8e, 0x00097012, 0x00091a2b, 0x0008ca29, 0x00087f78, 0x00083993,
    0x0007f807, 0x0007ba71, 0x00078078, 0x000749cb, 0x00071616, 0x0006e519, 0x0006b694, 0x00068a8d,
    0x000660ba, 0x000638f9, 0x000612fd, 0x0005eee2, 0x0005cc58, 0x0005aae7, 0x00058bb3, 0x00056d7f,
    0x00055090, 0x000534ca, 0x00051a22, 0x000500a0, 0x0004e7e9, 0x0004d039, 0x0004b966, 0x0004a22c,
    0x00048d15, 0x000478bb, 0x00046514, 0x00045217, 0x00043fbc, 0x00042df9, 0x00041cc9, 0x00040c24,
    0x0003fc03, 0x0003ec62, 0x0003dd38, 0x0003ce83, 0x0003c03c, 0x0003b25e, 0x0003a4e5, 0x000397cd,
    0x00038b0c, 0x00037e9d, 0x00037289, 0x000366be, 0x00035b59, 0x00035024, 0x00034546, 0x00033aac,
    0x0003304f, 0x00032641, 0x00031c7c, 0x000312d4, 0x0003097e, 0x00030060, 0x0002f760, 0x0002eea6,
    0x0002e61d, 0x0002ddc7, 0x0002d5a1, 0x0002cda5, 0x0002c574, 0x0002bddb, 0x0002b66b, 0x0002af23,
    0x0002a802, 0x0002a106, 0x00029a2f, 0x0002937b, 0x00028ce9, 0x00028678, 0x00028028, 0x000279f6,
    0x000273e3, 0x00026dee, 0x00026815, 0x00026258, 0x00025cb4, 0x00025729, 0x000251b8, 0x00024c5d,
    0x00024720, 0x000241f6, 0x00023ce5, 0x000237eb, 0x00023306, 0x00022e36, 0x0002297b, 0x000224d7,
    0x00022044, 0x00021bc4, 0x00021758, 0x000212fd, 0x00020eb7, 0x00020a81, 0x0002065b, 0x00020246,
    0x0001fe4b, 0x0001fa11, 0x0001f631, 0x0001f25f, 0x0001ee9c, 0x0001eae8, 0x0001e741, 0x0001e3a9,
    0x0001e01e, 0x0001dca0, 0x0001d92f, 0x0001d5ca, 0x0001d272, 0x0001cf26, 0x0001cbe6, 0x0001c8b2,
    0x0001c589, 0x0001c26a, 0x0001bf54, 0x0001bc49, 0x0001b949, 0x0001b651, 0x0001b364, 0x0001b083,
    0x0001adaa, 0x0001aae0, 0x0001a815, 0x0001a557, 0x0001a2a4, 0x0001a006, 0x00019d58, 0x00019abd,
    0x0001982c, 0x000195aa, 0x00019321, 0x000190a9, 0x00018e3e, 0x00018bb0, 0x00018950, 0x000186f7,
    0x000184a6, 0x0001825b, 0x00018018, 0x00017ddb, 0x00017ba4, 0x00017975, 0x0001774b, 0x00017528,
    0x0001730b, 0x000170f5, 0x00016ee2, 0x00016cd6, 0x00016acd, 0x000168cd, 0x000166d2, 0x000164da,
    0x000162e9, 0x00016103, 0x00015f1b, 0x00015d38, 0x00015b5f, 0x00015988, 0x000157b7, 0x000155e8,
    0x00015422, 0x0001525f, 0x0001509f, 0x00014ee5, 0x00014d31, 0x00014b7f, 0x000149d9, 0x0001482a,
    0x00014688, 0x000144ed, 0x0001433c, 0x000141a6, 0x00014014, 0x00013e85, 0x00013cfb, 0x00013b74,
    0x000139f1, 0x00013872, 0x000136f7, 0x0001357f, 0x00013409, 0x00013298, 0x00013129, 0x00012fbd,
    0x00012e56, 0x00012cf0, 0x00012b94, 0x00012a33, 0x000128d6, 0x0001277c, 0x00012629, 0x000124da,
    0x0001238f, 0x0001223c, 0x000120f4, 0x00011faf, 0x00011e6c, 0x00011d2b, 0x00011bee, 0x00011ab4,
    0x0001197c, 0x00011847, 0x00011715, 0x000115e3, 0x000114b6, 0x0001138b, 0x0001125a, 0x00011135,
    0x00011013, 0x00010ef3, 0x00010dd5, 0x00010cba, 0x00010ba1, 0x00010a8a, 0x00010976, 0x00010864,
    0x00010751, 0x00010645, 0x0001053a, 0x0001042c, 0x00010325, 0x0001021f, 0x0001011b, 0x00010020
};

/* Lossless premultiplication: 8-bit * alpha -> 16-bit. Reversible with this table. */
const uint32_t _smol_inv_div_p16_lut [256] =
{
    0x00000000, 0x00010000, 0x00008000, 0x00005556, 0x00004000, 0x00003334, 0x00002aab, 0x00002493,
    0x00002000, 0x00001c72, 0x0000199a, 0x00001746, 0x00001556, 0x000013b2, 0x0000124a, 0x00001112,
    0x00001000, 0x00000f10, 0x00000e39, 0x00000d7a, 0x00000ccd, 0x00000c31, 0x00000ba3, 0x00000b22,
    0x00000aab, 0x00000a3e, 0x000009d9, 0x0000097c, 0x00000925, 0x000008d4, 0x00000889, 0x00000843,
    0x00000800, 0x000007c2, 0x00000788, 0x00000751, 0x0000071d, 0x000006ec, 0x000006bd, 0x00000691,
    0x00000667, 0x0000063f, 0x00000619, 0x000005f5, 0x000005d2, 0x000005b1, 0x00000591, 0x00000573,
    0x00000556, 0x0000053a, 0x0000051f, 0x00000506, 0x000004ed, 0x000004d5, 0x000004be, 0x000004a8,
    0x00000493, 0x0000047e, 0x0000046a, 0x00000457, 0x00000445, 0x00000433, 0x00000422, 0x00000411,
    0x00000400, 0x000003f1, 0x000003e1, 0x000003d3, 0x000003c4, 0x000003b6, 0x000003a9, 0x0000039c,
    0x0000038f, 0x00000382, 0x00000376, 0x0000036a, 0x0000035f, 0x00000354, 0x00000349, 0x0000033e,
    0x00000334, 0x0000032a, 0x00000320, 0x00000316, 0x0000030d, 0x00000304, 0x000002fb, 0x000002f2,
    0x000002e9, 0x000002e1, 0x000002d9, 0x000002d1, 0x000002c9, 0x000002c1, 0x000002ba, 0x000002b2,
    0x000002ab, 0x000002a4, 0x0000029d, 0x00000296, 0x00000290, 0x00000289, 0x00000283, 0x0000027d,
    0x00000277, 0x00000271, 0x0000026b, 0x00000265, 0x0000025f, 0x0000025a, 0x00000254, 0x0000024f,
    0x0000024a, 0x00000244, 0x0000023f, 0x0000023a, 0x00000235, 0x00000231, 0x0000022c, 0x00000227,
    0x00000223, 0x0000021e, 0x0000021a, 0x00000215, 0x00000211, 0x0000020d, 0x00000209, 0x00000205,
    0x00000200, 0x000001fd, 0x000001f9, 0x000001f5, 0x000001f1, 0x000001ed, 0x000001ea, 0x000001e6,
    0x000001e2, 0x000001df, 0x000001db, 0x000001d8, 0x000001d5, 0x000001d1, 0x000001ce, 0x000001cb,
    0x000001c8, 0x000001c4, 0x000001c1, 0x000001be, 0x000001bb, 0x000001b8, 0x000001b5, 0x000001b3,
    0x000001b0, 0x000001ad, 0x000001aa, 0x000001a7, 0x000001a5, 0x000001a2, 0x0000019f, 0x0000019d,
    0x0000019a, 0x00000198, 0x00000195, 0x00000193, 0x00000190, 0x0000018e, 0x0000018b, 0x00000189,
    0x00000187, 0x00000184, 0x00000182, 0x00000180, 0x0000017e, 0x0000017b, 0x00000179, 0x00000177,
    0x00000175, 0x00000173, 0x00000171, 0x0000016f, 0x0000016d, 0x0000016b, 0x00000169, 0x00000167,
    0x00000165, 0x00000163, 0x00000161, 0x0000015f, 0x0000015d, 0x0000015b, 0x00000159, 0x00000158,
    0x00000156, 0x00000154, 0x00000152, 0x00000151, 0x0000014f, 0x0000014d, 0x0000014b, 0x0000014a,
    0x00000148, 0x00000147, 0x00000145, 0x00000143, 0x00000142, 0x00000140, 0x0000013f, 0x0000013d,
    0x0000013c, 0x0000013a, 0x00000139, 0x00000137, 0x00000136, 0x00000134, 0x00000133, 0x00000131,
    0x00000130, 0x0000012f, 0x0000012d, 0x0000012c, 0x0000012a, 0x00000129, 0x00000128, 0x00000126,
    0x00000125, 0x00000124, 0x00000122, 0x00000121, 0x00000120, 0x0000011f, 0x0000011d, 0x0000011c,
    0x0000011b, 0x0000011a, 0x00000119, 0x00000117, 0x00000116, 0x00000115, 0x00000114, 0x00000113,
    0x00000112, 0x00000110, 0x0000010f, 0x0000010e, 0x0000010d, 0x0000010c, 0x0000010b, 0x0000010a,
    0x00000109, 0x00000108, 0x00000107, 0x00000106, 0x00000105, 0x00000104, 0x00000103, 0x00000102
};

/* Lossless premultiplication: 11-bit * alpha -> 19-bit. Reversible with this table. */
const uint32_t _smol_inv_div_p16l_lut [256] =
{
    0x00000000, 0x00080000, 0x00040000, 0x0002aaab, 0x00020000, 0x0001999a, 0x00015556, 0x00012493,
    0x00010000, 0x0000e38f, 0x0000cccd, 0x0000ba2f, 0x0000aaab, 0x00009d8a, 0x0000924a, 0x00008889,
    0x00008000, 0x00007879, 0x000071c8, 0x00006bcb, 0x00006667, 0x00006187, 0x00005d18, 0x0000590c,
    0x00005556, 0x000051ec, 0x00004ec5, 0x00004bdb, 0x00004925, 0x0000469f, 0x00004445, 0x00004211,
    0x00004000, 0x00003e10, 0x00003c3d, 0x00003a84, 0x000038e4, 0x0000375a, 0x000035e6, 0x00003484,
    0x00003334, 0x000031f4, 0x000030c4, 0x00002fa1, 0x00002e8c, 0x00002d83, 0x00002c86, 0x00002b94,
    0x00002aab, 0x000029cc, 0x000028f6, 0x00002829, 0x00002763, 0x000026a5, 0x000025ee, 0x0000253d,
    0x00002493, 0x000023ef, 0x00002350, 0x000022b7, 0x00002223, 0x00002193, 0x00002109, 0x00002083,
    0x00002000, 0x00001f82, 0x00001f08, 0x00001e92, 0x00001e1f, 0x00001daf, 0x00001d42, 0x00001cd9,
    0x00001c72, 0x00001c0f, 0x00001bad, 0x00001b4f, 0x00001af3, 0x00001a99, 0x00001a42, 0x000019ed,
    0x0000199a, 0x00001949, 0x000018fa, 0x000018ad, 0x00001862, 0x00001819, 0x000017d1, 0x0000178b,
    0x00001746, 0x00001703, 0x000016c2, 0x00001682, 0x00001643, 0x00001606, 0x000015ca, 0x0000158f,
    0x00001556, 0x0000151e, 0x000014e6, 0x000014b0, 0x0000147b, 0x00001447, 0x00001415, 0x000013e3,
    0x000013b2, 0x00001382, 0x00001353, 0x00001324, 0x000012f7, 0x000012ca, 0x0000129f, 0x00001274,
    0x0000124a, 0x00001220, 0x000011f8, 0x000011d0, 0x000011a8, 0x00001182, 0x0000115c, 0x00001136,
    0x00001112, 0x000010ed, 0x000010ca, 0x000010a7, 0x00001085, 0x00001063, 0x00001042, 0x00001021,
    0x00001000, 0x00000fe1, 0x00000fc1, 0x00000fa3, 0x00000f84, 0x00000f67, 0x00000f49, 0x00000f2c,
    0x00000f10, 0x00000ef3, 0x00000ed8, 0x00000ebc, 0x00000ea1, 0x00000e87, 0x00000e6d, 0x00000e53,
    0x00000e39, 0x00000e20, 0x00000e08, 0x00000def, 0x00000dd7, 0x00000dbf, 0x00000da8, 0x00000d91,
    0x00000d7a, 0x00000d63, 0x00000d4d, 0x00000d37, 0x00000d21, 0x00000d0c, 0x00000cf7, 0x00000ce2,
    0x00000ccd, 0x00000cb9, 0x00000ca5, 0x00000c91, 0x00000c7d, 0x00000c6a, 0x00000c57, 0x00000c44,
    0x00000c31, 0x00000c1f, 0x00000c0d, 0x00000bfb, 0x00000be9, 0x00000bd7, 0x00000bc6, 0x00000bb4,
    0x00000ba3, 0x00000b93, 0x00000b82, 0x00000b71, 0x00000b61, 0x00000b51, 0x00000b41, 0x00000b31,
    0x00000b22, 0x00000b12, 0x00000b03, 0x00000af4, 0x00000ae5, 0x00000ad7, 0x00000ac8, 0x00000ab9,
    0x00000aab, 0x00000a9d, 0x00000a8f, 0x00000a81, 0x00000a73, 0x00000a66, 0x00000a58, 0x00000a4b,
    0x00000a3e, 0x00000a31, 0x00000a24, 0x00000a17, 0x00000a0b, 0x000009fe, 0x000009f2, 0x000009e5,
    0x000009d9, 0x000009cd, 0x000009c1, 0x000009b5, 0x000009aa, 0x0000099e, 0x00000992, 0x00000987,
    0x0000097c, 0x00000971, 0x00000965, 0x0000095b, 0x00000950, 0x00000945, 0x0000093a, 0x00000930,
    0x00000925, 0x0000091b, 0x00000910, 0x00000906, 0x000008fc, 0x000008f2, 0x000008e8, 0x000008de,
    0x000008d4, 0x000008cb, 0x000008c1, 0x000008b8, 0x000008ae, 0x000008a5, 0x0000089b, 0x00000892,
    0x00000889, 0x00000880, 0x00000877, 0x0000086e, 0x00000865, 0x0000085c, 0x00000854, 0x0000084b,
    0x00000843, 0x0000083a, 0x00000832, 0x00000829, 0x00000821, 0x00000819, 0x00000811, 0x00000809
};

/* -------------- *
 * Precalculation *
 * -------------- */

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

/* ------------------- *
 * Scaling: Outer loop *
 * ------------------- */

static SMOL_INLINE char *
outrow_ofs_to_pointer (const SmolScaleCtx *scale_ctx,
                       uint32_t outrow_ofs)
{
    return scale_ctx->pixels_out + scale_ctx->rowstride_out * outrow_ofs;
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

/* ----------------- *
 * Conversion tables *
 * ----------------- */

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
    { { 1, 2, 3, 4 }, { 2, 4, 3, 1 } },
    { { 1, 2, 3, 4 }, { 4, 1, 3, 2 } },
    { { 1, 2, 3, 4 }, { 4, 2, 3, 1 } },
    { { 1, 2, 3, 4 }, { 1, 3, 2, 0 } },
    { { 1, 2, 3, 4 }, { 2, 3, 1, 0 } },
    { { 1, 2, 3, 0 }, { 1, 3, 2, 4 } },

    { { 1, 2, 3, 4 }, { 3, 2, 4, 0 } },
    { { 1, 2, 3, 4 }, { 4, 2, 3, 0 } },

    { { 1, 2, 3, 4 }, { 1, 4, 2, 3 } },
    { { 1, 2, 3, 4 }, { 3, 2, 4, 1 } }
};

/* Keep in sync with the public SmolPixelType enum */
static const SmolPixelTypeMeta pixel_type_meta [SMOL_PIXEL_MAX] =
{
    /* RGBA = 1, 2, 3, 4 */
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMUL8,      { 1, 2, 3, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMUL8,      { 3, 2, 1, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMUL8,      { 4, 1, 2, 3 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_PREMUL8,      { 4, 3, 2, 1 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED, { 1, 2, 3, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED, { 3, 2, 1, 4 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED, { 4, 1, 2, 3 } },
    { SMOL_STORAGE_32BPP, SMOL_ALPHA_UNASSOCIATED, { 4, 3, 2, 1 } },
    { SMOL_STORAGE_24BPP, SMOL_ALPHA_PREMUL8,      { 1, 2, 3, 0 } },
    { SMOL_STORAGE_24BPP, SMOL_ALPHA_PREMUL8,      { 3, 2, 1, 0 } }
};

/* Channel ordering corrected for little endian. Only applies when fetching
 * entire pixels as dwords (i.e. u32), so 3-byte variants don't require any
 * correction. Keep in sync with the public SmolPixelType enum */
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
    if (host_is_little_endian ())
        return pixel_type_u32_le [pixel_type];

    return pixel_type;
}

static const SmolRepackMeta *
find_repack_match (const SmolRepackMeta *meta, uint16_t sig, uint16_t mask)
{
    sig &= mask;

    for (;; meta++)
    {
        if (!meta->repack_row_func)
        {
            meta = NULL;
            break;
        }

        if (sig == (meta->signature & mask))
            break;
    }

    return meta;
}

static void
do_reorder (const uint8_t *order_in, uint8_t *order_out, const uint8_t *reorder)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        uint8_t r = reorder [i];
        uint8_t o;

        if (r == 0)
        {
            o = 0;
        }
        else
        {
            o = order_in [r - 1];
            if (o == 0)
                o = i + 1;
        }

        order_out [i] = o;
    }
}

#if 0
static void
print_order (const uint8_t *order)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        fputc ('0' + order [i], stdout);
    }
}
#endif

static void
find_repacks (const SmolImplementation **implementations,
              SmolStorageType storage_in, SmolStorageType storage_mid, SmolStorageType storage_out,
              SmolAlphaType alpha_in, SmolAlphaType alpha_mid, SmolAlphaType alpha_out,
              SmolGammaType gamma_in, SmolGammaType gamma_mid, SmolGammaType gamma_out,
              const SmolPixelTypeMeta *pmeta_in, const SmolPixelTypeMeta *pmeta_out,
              const SmolRepackMeta **repack_in, const SmolRepackMeta **repack_out)
{
    int impl_in, impl_out;
    const SmolRepackMeta *meta_in, *meta_out = NULL;
    uint16_t sig_in_to_mid, sig_mid_to_out;
    uint16_t sig_mask;
    int reorder_dest_alpha_ch;

    sig_mask = SMOL_REPACK_SIGNATURE_ANY_ORDER_MASK (1, 1, 1, 1, 1, 1);
    sig_in_to_mid = SMOL_MAKE_REPACK_SIGNATURE_ANY_ORDER (storage_in, alpha_in, gamma_in,
                                                          storage_mid, alpha_mid, gamma_mid);
    sig_mid_to_out = SMOL_MAKE_REPACK_SIGNATURE_ANY_ORDER (storage_mid, alpha_mid, gamma_mid,
                                                           storage_out, alpha_out, gamma_out);

    /* The initial conversion must always leave alpha in position #4, so further
     * processing knows where to find it. The order of the other channels
     * doesn't matter, as long as there's a repack chain that ultimately
     * produces the desired result. */
    reorder_dest_alpha_ch = pmeta_in->order [0] == 4 ? 1 : 4;

    for (impl_in = 0; implementations [impl_in]; impl_in++)
    {
        meta_in = &implementations [impl_in]->repack_meta [0];

        for (;; meta_in++)
        {
            uint8_t order_mid [4];

#if 0
            fputc ('.', stdout);
#endif

            meta_in = find_repack_match (meta_in, sig_in_to_mid, sig_mask);
            if (!meta_in)
                break;

            if (reorder_meta [SMOL_REPACK_SIGNATURE_GET_REORDER (meta_in->signature)].dest [3] != reorder_dest_alpha_ch)
                continue;

            do_reorder (pmeta_in->order, order_mid,
                        reorder_meta [SMOL_REPACK_SIGNATURE_GET_REORDER (meta_in->signature)].dest);

#if 0
            printf ("In: "); print_order (pmeta_in->order); printf (" via "); print_order (reorder_meta [SMOL_REPACK_SIGNATURE_GET_REORDER (meta_in->signature)].dest); printf (" -> "); print_order (order_mid);
            fputc ('\n', stdout);
            fflush (stdout);
#endif

            for (impl_out = 0; implementations [impl_out]; impl_out++)
            {
                meta_out = &implementations [impl_out]->repack_meta [0];

                for (;; meta_out++)
                {
                    uint8_t order_out [4];

#if 0
                    fputc ('*', stdout);
#endif

                    meta_out = find_repack_match (meta_out, sig_mid_to_out, sig_mask);
                    if (!meta_out)
                        break;

                    do_reorder (order_mid, order_out,
                                reorder_meta [SMOL_REPACK_SIGNATURE_GET_REORDER (meta_out->signature)].dest);

#if 0
                    printf ("Out: "); print_order (order_mid); printf (" via "); print_order (reorder_meta [SMOL_REPACK_SIGNATURE_GET_REORDER (meta_out->signature)].dest); printf (" -> "); print_order (order_out); printf (" / want "); print_order (pmeta_out->order);
                    fputc ('\n', stdout);
                    fflush (stdout);
#endif

                    if (*((uint32_t *) order_out) == *((uint32_t *) pmeta_out->order))
                    {
                        /* Success */
                        goto out;
                    }
                }
            }
        }
    }

out:
    *repack_in = meta_in;
    *repack_out = meta_out;
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

#define IMPLEMENTATION_MAX 8

/* scale_ctx->storage_type must be initialized first by pick_filter_params() */
static void
get_implementations (SmolScaleCtx *scale_ctx)
{
    SmolPixelType ptype_in, ptype_out;
    const SmolPixelTypeMeta *pmeta_in, *pmeta_out;
    const SmolRepackMeta *rmeta_in, *rmeta_out;
    SmolAlphaType internal_alpha = SMOL_ALPHA_PREMUL8;
    const SmolImplementation *implementations [IMPLEMENTATION_MAX];
    int i = 0;

    /* Enumerate implementations, preferred first */

#ifdef SMOL_WITH_AVX2
    if (have_avx2 ())
        implementations [i++] = _smol_get_avx2_implementation ();
#endif
    implementations [i++] = _smol_get_generic_implementation ();
    implementations [i] = NULL;

    /* Install unpacker and packer */

    ptype_in = get_host_pixel_type (scale_ctx->pixel_type_in);
    ptype_out = get_host_pixel_type (scale_ctx->pixel_type_out);

    pmeta_in = &pixel_type_meta [ptype_in];
    pmeta_out = &pixel_type_meta [ptype_out];

    if (pmeta_in->alpha == SMOL_ALPHA_UNASSOCIATED
        && pmeta_out->alpha == SMOL_ALPHA_UNASSOCIATED)
    {
        /* In order to preserve the color range in transparent pixels when going
         * from unassociated to unassociated, we use 16 bits per channel internally. */
        internal_alpha = SMOL_ALPHA_PREMUL16;
        scale_ctx->storage_type = SMOL_STORAGE_128BPP;
    }
#if 0
    else if (pmeta_out->alpha == SMOL_ALPHA_UNASSOCIATED)
    {
        scale_ctx->storage_type = SMOL_STORAGE_128BPP;
    }
#endif

    if (scale_ctx->width_in > scale_ctx->width_out * 8191
        || scale_ctx->height_in > scale_ctx->height_out * 8191)
    {
        /* Even with 128bpp, there's only enough bits to store 11-bit linearized
         * times 13 bits of summed pixels plus 8 bits of scratch space for
         * multiplying with an 8-bit weight -> 32 bits total per channel.
         *
         * For now, just turn off sRGB linearization if the input is bigger
         * than the output by a factor of 2^13 or more. */
        scale_ctx->gamma_type = SMOL_GAMMA_SRGB_COMPRESSED;
    }

    find_repacks (implementations,
                  pmeta_in->storage, scale_ctx->storage_type, pmeta_out->storage,
                  pmeta_in->alpha, internal_alpha, pmeta_out->alpha,
                  SMOL_GAMMA_SRGB_COMPRESSED, scale_ctx->gamma_type, SMOL_GAMMA_SRGB_COMPRESSED,
                  pmeta_in, pmeta_out,
                  &rmeta_in, &rmeta_out);

    if (!rmeta_in || !rmeta_out)
        abort ();

    scale_ctx->unpack_row_func = rmeta_in->repack_row_func;
    scale_ctx->pack_row_func = rmeta_out->repack_row_func;

    /* Install filters */

    scale_ctx->hfilter_func = NULL;
    scale_ctx->vfilter_func = NULL;

    for (i = 0; implementations [i]; i++)
    {
        SmolHFilterFunc *hfilter_func =
            implementations [i]->hfilter_funcs [scale_ctx->storage_type] [scale_ctx->filter_h];
        SmolVFilterFunc *vfilter_func =
            implementations [i]->vfilter_funcs [scale_ctx->storage_type] [scale_ctx->filter_v];

        if (!scale_ctx->hfilter_func && hfilter_func)
            scale_ctx->hfilter_func = hfilter_func;
        if (!scale_ctx->vfilter_func && vfilter_func)
            scale_ctx->vfilter_func = vfilter_func;
    }

    if (!scale_ctx->hfilter_func || !scale_ctx->vfilter_func)
        abort ();
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
    scale_ctx->gamma_type = with_srgb ? SMOL_GAMMA_SRGB_LINEAR : SMOL_GAMMA_SRGB_COMPRESSED;

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

/* ---------- *
 * Public API *
 * ---------- */

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
