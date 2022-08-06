#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=122 unique=74 flat=592 nested=1184
static const uint8_t g_emit_buffer_0[74] = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32, 37,  45,  46,  47,
    51,  52,  53,  54,  55,  56,  57,  61,  65,  95,  98, 100, 102, 103, 104,
    108, 109, 110, 112, 114, 117, 58,  66,  67,  68,  69, 70,  71,  72,  73,
    74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84, 85,  86,  87,  89,
    106, 107, 113, 118, 119, 120, 121, 122, 38,  42,  44, 59,  88,  90};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=3293 unique=78 flat=8192 nested=5344
// monotonic increasing
static const uint8_t g_emit_op_0_outer[512] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11,
    11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13,
    13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 16,
    16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18,
    18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20,
    20, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23,
    23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25,
    25, 25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27,
    28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30,
    30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32,
    32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 34, 35,
    35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38,
    39, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43,
    43, 44, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48,
    48, 48, 49, 49, 49, 49, 50, 50, 50, 50, 51, 51, 51, 51, 52, 52, 52, 52, 53,
    53, 53, 53, 54, 54, 54, 54, 55, 55, 55, 55, 56, 56, 56, 56, 57, 57, 57, 57,
    58, 58, 58, 58, 59, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 61, 62, 62, 62,
    62, 63, 63, 63, 63, 64, 64, 64, 64, 65, 65, 65, 65, 66, 66, 66, 66, 67, 67,
    67, 67, 68, 68, 69, 69, 70, 70, 71, 71, 72, 72, 73, 73, 74, 75, 76, 77};
static const uint16_t g_emit_op_0_inner[78] = {
    5,    50,   95,   140,  185,  230,  275,  320,  365,  410,  456,  501,
    546,  591,  636,  681,  726,  771,  816,  861,  906,  951,  996,  1041,
    1086, 1131, 1176, 1221, 1266, 1311, 1356, 1401, 1446, 1491, 1536, 1581,
    1627, 1672, 1717, 1762, 1807, 1852, 1897, 1942, 1987, 2032, 2077, 2122,
    2167, 2212, 2257, 2302, 2347, 2392, 2437, 2482, 2527, 2572, 2617, 2662,
    2707, 2752, 2797, 2842, 2887, 2932, 2977, 3022, 3068, 3113, 3158, 3203,
    3248, 3293, 18,   27,   36,   45};
inline uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=122 unique=79 flat=1432 nested=2064
static const uint8_t g_emit_buffer_1[179] = {
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
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=8920 unique=182 flat=16384 nested=11104
// monotonic increasing
static const uint8_t g_emit_op_1_outer[1024] = {
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
static const uint16_t g_emit_op_1_inner[182] = {
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
    8418, 8468, 8518, 8568, 8618, 8668, 8720, 8770, 8820, 8870, 8920, 30,
    40,   50};
inline uint16_t GetEmitOp1(size_t i) {
  return g_emit_op_1_inner[g_emit_op_1_outer[i]];
}
// max=43 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {39, 43};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=12 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {11, 12};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=226 unique=22 flat=176 nested=352
static const uint8_t g_emit_buffer_3[22] = {
    0,  36,  64,  91,  93,  126, 94,  125, 60,  96,  123,
    92, 195, 208, 128, 130, 131, 162, 184, 194, 224, 226};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=4010 unique=40 flat=16384 nested=8832
// monotonic increasing
static const uint8_t g_emit_op_3_outer[1024] = {
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
static const uint16_t g_emit_op_3_inner[40] = {
    13,   203,  393,  583,  773,  963,  1154, 1344, 1535, 1725,
    1915, 2109, 2299, 2489, 2680, 2870, 3060, 3250, 3440, 3630,
    3820, 4010, 20,   30,   40,   50,   60,   70,   80,   90,
    100,  110,  120,  130,  140,  150,  160,  170,  180,  190};
inline uint16_t GetEmitOp3(size_t i) {
  return g_emit_op_3_inner[g_emit_op_3_outer[i]];
}
// max=161 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {153, 161};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_4[2] = {21, 22};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=172 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {167, 172};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_5[2] = {21, 22};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=177 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {176, 177};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {21, 22};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=209 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {179, 209};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_7[2] = {21, 22};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=217 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {216, 217};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {21, 22};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=229 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {227, 229};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_9[2] = {21, 22};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=146 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_10[4] = {133, 134, 136, 146};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_10[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=163 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_11[4] = {154, 156, 160, 163};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_11[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=173 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_12[4] = {164, 169, 170, 173};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_12[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=186 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_13[4] = {178, 181, 185, 186};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_13[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=196 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_14[4] = {187, 189, 190, 196};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_14[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=233 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_15[4] = {198, 228, 232, 233};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_15[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=143 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_16[8] = {1,   135, 137, 138,
                                            139, 140, 141, 143};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_16[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=158 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_17[8] = {147, 149, 150, 151,
                                            152, 155, 157, 158};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_17[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=183 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_18[8] = {165, 166, 168, 174,
                                            175, 180, 182, 183};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_18[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=230 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_19[3] = {230, 129, 132};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=26 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_19[4] = {21, 21, 24, 26};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=239 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_20[11] = {188, 191, 197, 231, 239, 9,
                                             142, 144, 145, 148, 159};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=64 unique=11 flat=128 nested=216
// monotonic increasing
static const uint8_t g_emit_op_20[16] = {23, 23, 27, 27, 31, 31, 35, 35,
                                         39, 39, 44, 48, 52, 56, 60, 64};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=255 unique=76 flat=608 nested=1216
static const uint8_t g_emit_buffer_21[76] = {
    171, 206, 215, 225, 236, 237, 199, 207, 234, 235, 192, 193, 200,
    201, 202, 205, 210, 213, 218, 219, 238, 240, 242, 243, 255, 203,
    204, 211, 212, 214, 221, 222, 223, 241, 244, 245, 246, 247, 248,
    250, 251, 252, 253, 254, 2,   3,   4,   5,   6,   7,   8,   11,
    12,  14,  15,  16,  17,  18,  19,  20,  21,  23,  24,  25,  26,
    27,  28,  29,  30,  31,  127, 220, 249, 10,  13,  22};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=1530 unique=77 flat=16384 nested=9424
// monotonic increasing
static const uint8_t g_emit_op_21_outer[1024] = {
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
static const uint16_t g_emit_op_21_inner[77] = {
    24,   44,   64,   84,   104,  124,  145,  165,  185,  205,  226,
    246,  266,  286,  306,  326,  346,  366,  386,  406,  426,  446,
    466,  486,  506,  527,  547,  567,  587,  607,  627,  647,  667,
    687,  707,  727,  747,  767,  787,  807,  827,  847,  867,  887,
    908,  928,  948,  968,  988,  1008, 1028, 1048, 1068, 1088, 1108,
    1128, 1148, 1168, 1188, 1208, 1228, 1248, 1268, 1288, 1308, 1328,
    1348, 1368, 1388, 1408, 1428, 1448, 1468, 1490, 1510, 1530, 40};
inline uint16_t GetEmitOp21(size_t i) {
  return g_emit_op_21_inner[g_emit_op_21_outer[i]];
}
// max=124 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_22[3] = {124, 35, 62};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=16 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_22[4] = {11, 11, 14, 16};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
template <typename F>
class HuffDecoder {
 public:
  HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end)
      : sink_(sink), begin_(begin), end_(end) {}
  bool Run() {
    while (ok_) {
      if (!RefillTo10()) {
        Done();
        return ok_;
      }
      const auto index = buffer_ >> (buffer_len_ - 10);
      auto op = GetEmitOp1(index);
      buffer_len_ -= op % 10;
      op /= 10;
      const auto emit_ofs = op / 5;
      switch (op % 5) {
        case 2: {
          DecodeStep0();
          break;
        }
        case 4: {
          DecodeStep1();
          break;
        }
        case 3: {
          DecodeStep20();
          break;
        }
        case 1: {
          sink_(GetEmitBuffer1(emit_ofs + 0));
          break;
        }
        case 0: {
          sink_(GetEmitBuffer1(emit_ofs + 0));
          sink_(GetEmitBuffer1(emit_ofs + 1));
          break;
        }
      }
    }
    return ok_;
  }

 private:
  bool RefillTo10() {
    switch (buffer_len_) {
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9: {
        return Read1();
      }
      case 0:
      case 1: {
        return Read2();
      }
    }
    return true;
  }
  bool Read2() {
    if (begin_ + 2 > end_) return false;
    buffer_ <<= 16;
    buffer_ |= static_cast<uint64_t>(*begin_++) << 8;
    buffer_ |= static_cast<uint64_t>(*begin_++) << 0;
    buffer_len_ += 16;
    return true;
  }
  bool Read1() {
    if (begin_ + 1 > end_) return false;
    buffer_ <<= 8;
    buffer_ |= static_cast<uint64_t>(*begin_++) << 0;
    buffer_len_ += 8;
    return true;
  }
  void DecodeStep0() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp2(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer2(op + 0));
  }
  bool RefillTo1() {
    switch (buffer_len_) {
      case 0: {
        return Read1();
      }
    }
    return true;
  }
  void DecodeStep1() {
    if (!RefillTo10()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 10);
    auto op = GetEmitOp3(index);
    buffer_len_ -= op % 10;
    op /= 10;
    const auto emit_ofs = op / 19;
    switch (op % 19) {
      case 10: {
        DecodeStep10();
        break;
      }
      case 11: {
        DecodeStep11();
        break;
      }
      case 12: {
        DecodeStep12();
        break;
      }
      case 13: {
        DecodeStep13();
        break;
      }
      case 14: {
        DecodeStep14();
        break;
      }
      case 15: {
        DecodeStep15();
        break;
      }
      case 16: {
        DecodeStep16();
        break;
      }
      case 7: {
        DecodeStep17();
        break;
      }
      case 17: {
        DecodeStep18();
        break;
      }
      case 18: {
        DecodeStep19();
        break;
      }
      case 1: {
        DecodeStep2();
        break;
      }
      case 2: {
        DecodeStep3();
        break;
      }
      case 3: {
        DecodeStep4();
        break;
      }
      case 4: {
        DecodeStep5();
        break;
      }
      case 5: {
        DecodeStep6();
        break;
      }
      case 6: {
        DecodeStep7();
        break;
      }
      case 8: {
        DecodeStep8();
        break;
      }
      case 9: {
        DecodeStep9();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer3(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep2() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp4(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer4(op + 0));
  }
  void DecodeStep3() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp5(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer5(op + 0));
  }
  void DecodeStep4() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp6(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer6(op + 0));
  }
  void DecodeStep5() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp7(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer7(op + 0));
  }
  void DecodeStep6() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp8(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer8(op + 0));
  }
  void DecodeStep7() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp9(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer9(op + 0));
  }
  void DecodeStep8() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp10(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer10(op + 0));
  }
  bool RefillTo2() {
    switch (buffer_len_) {
      case 0:
      case 1: {
        return Read1();
      }
    }
    return true;
  }
  void DecodeStep9() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp11(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer11(op + 0));
  }
  void DecodeStep10() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp12(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer12(op + 0));
  }
  void DecodeStep11() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp13(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer13(op + 0));
  }
  void DecodeStep12() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp14(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer14(op + 0));
  }
  void DecodeStep13() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp15(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer15(op + 0));
  }
  void DecodeStep14() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp16(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer16(op + 0));
  }
  bool RefillTo3() {
    switch (buffer_len_) {
      case 0:
      case 1:
      case 2: {
        return Read1();
      }
    }
    return true;
  }
  void DecodeStep15() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp17(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer17(op + 0));
  }
  void DecodeStep16() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp18(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer18(op + 0));
  }
  void DecodeStep17() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp19(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer19(op + 0));
  }
  void DecodeStep18() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 4);
    auto op = GetEmitOp20(index);
    buffer_len_ -= op % 4;
    sink_(GetEmitBuffer20(op + 0));
  }
  bool RefillTo4() {
    switch (buffer_len_) {
      case 0:
      case 1:
      case 2:
      case 3: {
        return Read1();
      }
    }
    return true;
  }
  void DecodeStep19() {
    if (!RefillTo10()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 10);
    auto op = GetEmitOp21(index);
    buffer_len_ -= op % 10;
    op /= 10;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmitBuffer21(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep20() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp22(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer22(op + 0));
  }
  void Done() {
    if (buffer_len_ < 9) {
      buffer_ = (buffer_ << (9 - buffer_len_)) |
                ((uint64_t(1) << (9 - buffer_len_)) - 1);
      buffer_len_ = 9;
    }
    const auto index = buffer_ >> (buffer_len_ - 9);
    auto op = GetEmitOp0(index);
    buffer_len_ -= op % 9;
    op /= 9;
    const auto emit_ofs = op / 5;
    switch (op % 5) {
      case 1:
      case 2:
      case 3:
      case 4: {
        break;
      }
      case 0: {
        sink_(GetEmitBuffer0(emit_ofs + 0));
        break;
      }
    }
    if (buffer_len_ == 0) return;
    const uint64_t mask = (1 << buffer_len_) - 1;
    if ((buffer_ & mask) != mask) ok_ = false;
  }
  F sink_;
  const uint8_t* begin_;
  const uint8_t* const end_;
  uint64_t buffer_ = 0;
  int buffer_len_ = 0;
  bool ok_ = true;
};
