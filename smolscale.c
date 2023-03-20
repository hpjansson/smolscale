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
const uint32_t _smol_inverted_div_lut [256] =
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
