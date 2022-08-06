#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=122 unique=79 flat=1432 nested=2064
static const uint8_t g_emit_buffer_0[179] = {
    48,  48,  48,  49,  48,  50,  48,  97,  48,  99,  48,  101, 48,  105, 48,
    111, 48,  115, 48,  116, 49,  49,  49,  50,  49,  97,  49,  99,  49,  101,
    49,  105, 49,  111, 49,  115, 49,  116, 50,  50,  50,  97,  50,  99,  50,
    101, 50,  105, 50,  111, 50,  115, 50,  116, 97,  97,  97,  99,  97,  101,
    97,  105, 97,  111, 97,  115, 97,  116, 99,  99,  99,  101, 99,  105, 99,
    111, 99,  115, 99,  116, 101, 101, 101, 105, 101, 111, 101, 115, 101, 116,
    105, 105, 105, 111, 105, 115, 105, 116, 111, 111, 111, 115, 111, 116, 115,
    115, 115, 116, 116, 48,  32,  37,  45,  46,  47,  51,  52,  53,  54,  55,
    56,  57,  61,  65,  95,  98,  100, 102, 103, 104, 108, 109, 110, 112, 114,
    117, 58,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,
    79,  80,  81,  82,  83,  84,  85,  86,  87,  89,  106, 107, 113, 118, 119,
    120, 121, 122, 38,  42,  44,  59,  88,  90,  33,  34,  40,  41,  63};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=8920 unique=182 flat=16384 nested=11104
// monotonic increasing
static const uint8_t g_emit_op_0_outer[1024] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  10,  10,  10,  10,
    10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
    10,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  21,  21,
    21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,
    21,  21,  21,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,
    32,  32,  32,  32,  32,  32,  32,  32,  32,  32,  32,  32,  32,  32,  32,
    32,  32,  32,  32,  32,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,
    42,  43,  43,  43,  43,  43,  43,  43,  43,  43,  43,  43,  43,  43,  43,
    43,  43,  43,  43,  43,  43,  43,  43,  44,  45,  46,  47,  48,  49,  50,
    51,  52,  53,  54,  54,  54,  54,  54,  54,  54,  54,  54,  54,  54,  54,
    54,  54,  54,  54,  54,  54,  54,  54,  54,  54,  55,  56,  57,  58,  59,
    60,  61,  62,  63,  64,  65,  65,  65,  65,  65,  65,  65,  65,  65,  65,
    65,  65,  65,  65,  65,  65,  65,  65,  65,  65,  65,  65,  66,  67,  68,
    69,  70,  71,  72,  73,  74,  75,  76,  76,  76,  76,  76,  76,  76,  76,
    76,  76,  76,  76,  76,  76,  76,  76,  76,  76,  76,  76,  76,  76,  77,
    78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  87,  87,  87,  87,  87,
    87,  87,  87,  87,  87,  87,  87,  87,  87,  87,  87,  87,  87,  87,  87,
    87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  98,  98,  98,
    98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  98,
    98,  98,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 109,
    109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109,
    109, 109, 109, 109, 109, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110,
    110, 110, 110, 110, 110, 110, 111, 111, 111, 111, 111, 111, 111, 111, 111,
    111, 111, 111, 111, 111, 111, 111, 112, 112, 112, 112, 112, 112, 112, 112,
    112, 112, 112, 112, 112, 112, 112, 112, 113, 113, 113, 113, 113, 113, 113,
    113, 113, 113, 113, 113, 113, 113, 113, 113, 114, 114, 114, 114, 114, 114,
    114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 115, 115, 115, 115, 115,
    115, 115, 115, 115, 115, 115, 115, 115, 115, 115, 115, 116, 116, 116, 116,
    116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 117, 117, 117,
    117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 118, 118,
    118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 119,
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119,
    120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120,
    120, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121,
    121, 121, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
    122, 122, 122, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123,
    123, 123, 123, 123, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124,
    124, 124, 124, 124, 124, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 126, 126, 126, 126, 126, 126, 126, 126, 126,
    126, 126, 126, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 127,
    127, 127, 127, 127, 127, 127, 127, 127, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 129, 129, 129, 129, 129, 129,
    129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 130, 130, 130, 130, 130,
    130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 131, 131, 131, 131,
    131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 132, 132, 132,
    132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 133, 133,
    133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 134,
    134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134,
    135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135,
    135, 136, 136, 136, 136, 136, 136, 136, 136, 137, 137, 137, 137, 137, 137,
    137, 137, 138, 138, 138, 138, 138, 138, 138, 138, 139, 139, 139, 139, 139,
    139, 139, 139, 140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 141,
    141, 141, 141, 141, 142, 142, 142, 142, 142, 142, 142, 142, 143, 143, 143,
    143, 143, 143, 143, 143, 144, 144, 144, 144, 144, 144, 144, 144, 145, 145,
    145, 145, 145, 145, 145, 145, 146, 146, 146, 146, 146, 146, 146, 146, 147,
    147, 147, 147, 147, 147, 147, 147, 148, 148, 148, 148, 148, 148, 148, 148,
    149, 149, 149, 149, 149, 149, 149, 149, 150, 150, 150, 150, 150, 150, 150,
    150, 151, 151, 151, 151, 151, 151, 151, 151, 152, 152, 152, 152, 152, 152,
    152, 152, 153, 153, 153, 153, 153, 153, 153, 153, 154, 154, 154, 154, 154,
    154, 154, 154, 155, 155, 155, 155, 155, 155, 155, 155, 156, 156, 156, 156,
    156, 156, 156, 156, 157, 157, 157, 157, 157, 157, 157, 157, 158, 158, 158,
    158, 158, 158, 158, 158, 159, 159, 159, 159, 159, 159, 159, 159, 160, 160,
    160, 160, 160, 160, 160, 160, 161, 161, 161, 161, 161, 161, 161, 161, 162,
    162, 162, 162, 162, 162, 162, 162, 163, 163, 163, 163, 163, 163, 163, 163,
    164, 164, 164, 164, 164, 164, 164, 164, 165, 165, 165, 165, 165, 165, 165,
    165, 166, 166, 166, 166, 166, 166, 166, 166, 167, 167, 167, 167, 167, 167,
    167, 167, 168, 168, 168, 168, 169, 169, 169, 169, 170, 170, 170, 170, 171,
    171, 171, 171, 172, 172, 172, 172, 173, 173, 173, 173, 174, 175, 176, 177,
    178, 179, 180, 181};
static const uint16_t g_emit_op_0_inner[182] = {
    10,   110,  210,  310,  410,  510,  610,  710,  810,  910,  15,   160,
    1010, 1110, 1210, 1310, 1410, 1510, 1610, 1710, 1810, 165,  260,  1160,
    1910, 2010, 2110, 2210, 2310, 2410, 2510, 2610, 265,  360,  1260, 2060,
    2710, 2810, 2910, 3010, 3110, 3210, 3310, 365,  460,  1360, 2160, 2860,
    3410, 3510, 3610, 3710, 3810, 3910, 465,  560,  1460, 2260, 2960, 3560,
    4010, 4110, 4210, 4310, 4410, 565,  660,  1560, 2360, 3060, 3660, 4160,
    4510, 4610, 4710, 4810, 665,  760,  1660, 2460, 3160, 3760, 4260, 4660,
    4910, 5010, 5110, 765,  860,  1760, 2560, 3260, 3860, 4360, 4760, 5060,
    5210, 5310, 865,  5410, 960,  1860, 2660, 3360, 3960, 4460, 4860, 5160,
    5360, 965,  5516, 5566, 5616, 5666, 5716, 5766, 5816, 5866, 5916, 5966,
    6016, 6066, 6116, 6166, 6216, 6266, 6316, 6366, 6416, 6466, 6516, 6566,
    6616, 6666, 6716, 6766, 6817, 6867, 6917, 6967, 7017, 7067, 7117, 7167,
    7217, 7267, 7317, 7367, 7417, 7467, 7517, 7567, 7617, 7667, 7717, 7767,
    7817, 7867, 7917, 7967, 8017, 8067, 8117, 8167, 8217, 8267, 8317, 8367,
    8418, 8468, 8518, 8568, 8618, 8668, 8720, 8770, 8820, 8870, 8920, 20,
    30,   40};
inline uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=43 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_1[2] = {39, 43};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=12 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_1[2] = {11, 12};
inline uint8_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=226 unique=22 flat=176 nested=352
static const uint8_t g_emit_buffer_2[22] = {
    0,  36,  64,  91,  93,  126, 94,  125, 60,  96,  123,
    92, 195, 208, 128, 130, 131, 162, 184, 194, 224, 226};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=4010 unique=40 flat=16384 nested=8832
// monotonic increasing
static const uint8_t g_emit_op_2_outer[1024] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
static const uint16_t g_emit_op_2_inner[40] = {
    13,   203,  393,  583,  773,  963,  1154, 1344, 1535, 1725,
    1915, 2109, 2299, 2489, 2680, 2870, 3060, 3250, 3440, 3630,
    3820, 4010, 10,   20,   30,   40,   50,   60,   70,   80,
    90,   100,  110,  120,  130,  140,  150,  160,  170,  180};
inline uint16_t GetEmitOp2(size_t i) {
  return g_emit_op_2_inner[g_emit_op_2_outer[i]];
}
// max=161 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {153, 161};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {21, 22};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=172 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {167, 172};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_4[2] = {21, 22};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=177 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {176, 177};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_5[2] = {21, 22};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=209 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {179, 209};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {21, 22};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=217 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {216, 217};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_7[2] = {21, 22};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=229 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {227, 229};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {21, 22};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=146 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_9[4] = {133, 134, 136, 146};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_9[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=163 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_10[4] = {154, 156, 160, 163};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_10[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=173 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_11[4] = {164, 169, 170, 173};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_11[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=186 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_12[4] = {178, 181, 185, 186};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_12[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=196 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_13[4] = {187, 189, 190, 196};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_13[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=233 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_14[4] = {198, 228, 232, 233};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_14[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=143 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_15[8] = {1,   135, 137, 138,
                                            139, 140, 141, 143};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_15[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=158 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_16[8] = {147, 149, 150, 151,
                                            152, 155, 157, 158};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_16[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=183 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_17[8] = {165, 166, 168, 174,
                                            175, 180, 182, 183};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_17[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=230 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_18[3] = {230, 129, 132};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=26 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_18[4] = {21, 21, 24, 26};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=239 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_19[11] = {188, 191, 197, 231, 239, 9,
                                             142, 144, 145, 148, 159};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=64 unique=11 flat=128 nested=216
// monotonic increasing
static const uint8_t g_emit_op_19[16] = {23, 23, 27, 27, 31, 31, 35, 35,
                                         39, 39, 44, 48, 52, 56, 60, 64};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=256 unique=77 flat=1232 nested=1848
static const uint16_t g_emit_buffer_20[77] = {
    171, 206, 215, 225, 236, 237, 199, 207, 234, 235, 192, 193, 200,
    201, 202, 205, 210, 213, 218, 219, 238, 240, 242, 243, 255, 203,
    204, 211, 212, 214, 221, 222, 223, 241, 244, 245, 246, 247, 248,
    250, 251, 252, 253, 254, 2,   3,   4,   5,   6,   7,   8,   11,
    12,  14,  15,  16,  17,  18,  19,  20,  21,  23,  24,  25,  26,
    27,  28,  29,  30,  31,  127, 220, 249, 10,  13,  22,  256};
inline uint16_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=790 unique=77 flat=16384 nested=9424
// monotonic increasing
static const uint8_t g_emit_op_20_outer[1024] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25,
    26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28,
    28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30,
    30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 32, 32, 33,
    33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 34, 35, 35, 35, 35,
    35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37,
    37, 38, 38, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 40, 40,
    40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42,
    42, 42, 42, 43, 43, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 45, 45, 45, 45,
    46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 49, 49, 49, 49, 50, 50, 50,
    50, 51, 51, 51, 51, 52, 52, 52, 52, 53, 53, 53, 53, 54, 54, 54, 54, 55, 55,
    55, 55, 56, 56, 56, 56, 57, 57, 57, 57, 58, 58, 58, 58, 59, 59, 59, 59, 60,
    60, 60, 60, 61, 61, 61, 61, 62, 62, 62, 62, 63, 63, 63, 63, 64, 64, 64, 64,
    65, 65, 65, 65, 66, 66, 66, 66, 67, 67, 67, 67, 68, 68, 68, 68, 69, 69, 69,
    69, 70, 70, 70, 70, 71, 71, 71, 71, 72, 72, 72, 72, 73, 74, 75, 76};
// monotonic increasing
static const uint16_t g_emit_op_20_inner[77] = {
    24,  34,  44,  54,  64,  74,  85,  95,  105, 115, 126, 136, 146,
    156, 166, 176, 186, 196, 206, 216, 226, 236, 246, 256, 266, 277,
    287, 297, 307, 317, 327, 337, 347, 357, 367, 377, 387, 397, 407,
    417, 427, 437, 447, 457, 468, 478, 488, 498, 508, 518, 528, 538,
    548, 558, 568, 578, 588, 598, 608, 618, 628, 638, 648, 658, 668,
    678, 688, 698, 708, 718, 728, 738, 748, 760, 770, 780, 790};
inline uint16_t GetEmitOp20(size_t i) {
  return g_emit_op_20_inner[g_emit_op_20_outer[i]];
}
// max=124 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_21[3] = {124, 35, 62};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=16 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_21[4] = {11, 11, 14, 16};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
template <typename F>
bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {
  uint64_t buffer = 0;
  uint64_t index;
  size_t emit_ofs;
  int buffer_len = 0;
  uint64_t op;
refill:
  while (buffer_len < 10) {
    if (begin == end) return buffer_len == 0;
    buffer <<= 8;
    buffer |= static_cast<uint64_t>(*begin++);
    buffer_len += 8;
  }
  index = buffer >> (buffer_len - 10);
  op = GetEmitOp0(index);
  buffer_len -= op % 10;
  op /= 10;
  emit_ofs = op / 5;
  switch (op % 5) {
    case 4: {
      // 0:0/3,1:1fd8/13,2:3ffe2/18,3:3ffe3/18,4:3ffe4/18,5:3ffe5/18,6:3ffe6/18,7:3ffe7/18,8:3ffe8/18,9:3fea/14,10:ffffc/20,11:3ffe9/18,12:3ffea/18,13:ffffd/20,14:3ffeb/18,15:3ffec/18,16:3ffed/18,17:3ffee/18,18:3ffef/18,19:3fff0/18,20:3fff1/18,21:3fff2/18,22:ffffe/20,23:3fff3/18,24:3fff4/18,25:3fff5/18,26:3fff6/18,27:3fff7/18,28:3fff8/18,29:3fff9/18,30:3fffa/18,31:3fffb/18,36:1/3,60:1c/5,64:2/3,91:3/3,92:1f0/9,93:4/3,94:c/4,96:1d/5,123:1e/5,125:d/4,126:5/3,127:3fffc/18,128:3e6/10,129:fd2/12,130:3e7/10,131:3e8/10,132:fd3/12,133:fd4/12,134:fd5/12,135:1fd9/13,136:fd6/12,137:1fda/13,138:1fdb/13,139:1fdc/13,140:1fdd/13,141:1fde/13,142:3feb/14,143:1fdf/13,144:3fec/14,145:3fed/14,146:fd7/12,147:1fe0/13,148:3fee/14,149:1fe1/13,150:1fe2/13,151:1fe3/13,152:1fe4/13,153:7dc/11,154:fd8/12,155:1fe5/13,156:fd9/12,157:1fe6/13,158:1fe7/13,159:3fef/14,160:fda/12,161:7dd/11,162:3e9/10,163:fdb/12,164:fdc/12,165:1fe8/13,166:1fe9/13,167:7de/11,168:1fea/13,169:fdd/12,170:fde/12,171:3ff0/14,172:7df/11,173:fdf/12,174:1feb/13,175:1fec/13,176:7e0/11,177:7e1/11,178:fe0/12,179:7e2/11,180:1fed/13,181:fe1/12,182:1fee/13,183:1fef/13,184:3ea/10,185:fe2/12,186:fe3/12,187:fe4/12,188:1ff0/13,189:fe5/12,190:fe6/12,191:1ff1/13,192:ffe0/16,193:ffe1/16,194:3eb/10,195:1f1/9,196:fe7/12,197:1ff2/13,198:fe8/12,199:7fec/15,200:ffe2/16,201:ffe3/16,202:ffe4/16,203:1ffde/17,204:1ffdf/17,205:ffe5/16,206:3ff1/14,207:7fed/15,208:1f2/9,209:7e3/11,210:ffe6/16,211:1ffe0/17,212:1ffe1/17,213:ffe7/16,214:1ffe2/17,215:3ff2/14,216:7e4/11,217:7e5/11,218:ffe8/16,219:ffe9/16,220:3fffd/18,221:1ffe3/17,222:1ffe4/17,223:1ffe5/17,224:3ec/10,225:3ff3/14,226:3ed/10,227:7e6/11,228:fe9/12,229:7e7/11,230:7e8/11,231:1ff3/13,232:fea/12,233:feb/12,234:7fee/15,235:7fef/15,236:3ff4/14,237:3ff5/14,238:ffea/16,239:1ff4/13,240:ffeb/16,241:1ffe6/17,242:ffec/16,243:ffed/16,244:1ffe7/17,245:1ffe8/17,246:1ffe9/17,247:1ffea/17,248:1ffeb/17,249:3fffe/18,250:1ffec/17,251:1ffed/17,252:1ffee/17,253:1ffef/17,254:1fff0/17,255:ffee/16,256:fffff/20
      while (buffer_len < 10) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 10);
      op = GetEmitOp2(index);
      buffer_len -= op % 10;
      op /= 10;
      emit_ofs = op / 19;
      switch (op % 19) {
        case 7: {
          // 129:2/2,132:3/2,230:0/1
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp18(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer18(op + 0));
          goto refill;
        }
        case 8: {
          // 133:0/2,134:1/2,136:2/2,146:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp9(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer9(op + 0));
          goto refill;
        }
        case 15: {
          // 147:0/3,149:1/3,150:2/3,151:3/3,152:4/3,155:5/3,157:6/3,158:7/3
          while (buffer_len < 3) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 3);
          op = GetEmitOp16(index);
          buffer_len -= op % 3;
          sink(GetEmitBuffer16(op + 0));
          goto refill;
        }
        case 1: {
          // 153:0/1,161:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp3(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer3(op + 0));
          goto refill;
        }
        case 9: {
          // 154:0/2,156:1/2,160:2/2,163:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp10(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer10(op + 0));
          goto refill;
        }
        case 10: {
          // 164:0/2,169:1/2,170:2/2,173:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp11(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer11(op + 0));
          goto refill;
        }
        case 16: {
          // 165:0/3,166:1/3,168:2/3,174:3/3,175:4/3,180:5/3,182:6/3,183:7/3
          while (buffer_len < 3) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 3);
          op = GetEmitOp17(index);
          buffer_len -= op % 3;
          sink(GetEmitBuffer17(op + 0));
          goto refill;
        }
        case 2: {
          // 167:0/1,172:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp4(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer4(op + 0));
          goto refill;
        }
        case 3: {
          // 176:0/1,177:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp5(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer5(op + 0));
          goto refill;
        }
        case 11: {
          // 178:0/2,181:1/2,185:2/2,186:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp12(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer12(op + 0));
          goto refill;
        }
        case 4: {
          // 179:0/1,209:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp6(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer6(op + 0));
          goto refill;
        }
        case 12: {
          // 187:0/2,189:1/2,190:2/2,196:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp13(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer13(op + 0));
          goto refill;
        }
        case 13: {
          // 198:0/2,228:1/2,232:2/2,233:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp14(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer14(op + 0));
          goto refill;
        }
        case 14: {
          // 1:0/3,135:1/3,137:2/3,138:3/3,139:4/3,140:5/3,141:6/3,143:7/3
          while (buffer_len < 3) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 3);
          op = GetEmitOp15(index);
          buffer_len -= op % 3;
          sink(GetEmitBuffer15(op + 0));
          goto refill;
        }
        case 5: {
          // 216:0/1,217:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp7(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer7(op + 0));
          goto refill;
        }
        case 6: {
          // 227:0/1,229:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp8(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer8(op + 0));
          goto refill;
        }
        case 18: {
          // 2:e2/8,3:e3/8,4:e4/8,5:e5/8,6:e6/8,7:e7/8,8:e8/8,10:3fc/10,11:e9/8,12:ea/8,13:3fd/10,14:eb/8,15:ec/8,16:ed/8,17:ee/8,18:ef/8,19:f0/8,20:f1/8,21:f2/8,22:3fe/10,23:f3/8,24:f4/8,25:f5/8,26:f6/8,27:f7/8,28:f8/8,29:f9/8,30:fa/8,31:fb/8,127:fc/8,171:0/4,192:20/6,193:21/6,199:c/5,200:22/6,201:23/6,202:24/6,203:5e/7,204:5f/7,205:25/6,206:1/4,207:d/5,210:26/6,211:60/7,212:61/7,213:27/6,214:62/7,215:2/4,218:28/6,219:29/6,220:fd/8,221:63/7,222:64/7,223:65/7,225:3/4,234:e/5,235:f/5,236:4/4,237:5/4,238:2a/6,240:2b/6,241:66/7,242:2c/6,243:2d/6,244:67/7,245:68/7,246:69/7,247:6a/7,248:6b/7,249:fe/8,250:6c/7,251:6d/7,252:6e/7,253:6f/7,254:70/7,255:2e/6,256:3ff/10
          while (buffer_len < 10) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 10);
          op = GetEmitOp20(index);
          buffer_len -= op % 10;
          sink(GetEmitBuffer20(op + 0));
          goto refill;
        }
        case 17: {
          // 9:a/4,142:b/4,144:c/4,145:d/4,148:e/4,159:f/4,188:0/3,191:1/3,197:2/3,231:3/3,239:4/3
          while (buffer_len < 4) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 4);
          op = GetEmitOp19(index);
          buffer_len -= op % 4;
          sink(GetEmitBuffer19(op + 0));
          goto refill;
        }
        case 0: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          goto refill;
        }
      }
    }
    case 3: {
      // 35:2/2,62:3/2,124:0/1
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp21(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer21(op + 0));
      goto refill;
    }
    case 2: {
      // 39:0/1,43:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp1(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer1(op + 0));
      goto refill;
    }
    case 1: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      goto refill;
    }
    case 0: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      sink(GetEmitBuffer0(emit_ofs + 1));
      goto refill;
    }
  }
  abort();
}
