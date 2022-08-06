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
// max=3291 unique=78 flat=8192 nested=5344
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
    3,    48,   93,   138,  183,  228,  273,  318,  363,  408,  454,  499,
    544,  589,  634,  679,  724,  769,  814,  859,  904,  949,  994,  1039,
    1084, 1129, 1174, 1219, 1264, 1309, 1354, 1399, 1444, 1489, 1534, 1579,
    1625, 1670, 1715, 1760, 1805, 1850, 1895, 1940, 1985, 2030, 2075, 2120,
    2165, 2210, 2255, 2300, 2345, 2390, 2435, 2480, 2525, 2570, 2615, 2660,
    2705, 2750, 2795, 2840, 2885, 2930, 2975, 3020, 3066, 3111, 3156, 3201,
    3246, 3291, 18,   27,   36,   45};
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
// max=8918 unique=182 flat=16384 nested=11104
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
    8,    108,  208,  308,  408,  508,  608,  708,  808,  908,  13,   158,
    1008, 1108, 1208, 1308, 1408, 1508, 1608, 1708, 1808, 163,  258,  1158,
    1908, 2008, 2108, 2208, 2308, 2408, 2508, 2608, 263,  358,  1258, 2058,
    2708, 2808, 2908, 3008, 3108, 3208, 3308, 363,  458,  1358, 2158, 2858,
    3408, 3508, 3608, 3708, 3808, 3908, 463,  558,  1458, 2258, 2958, 3558,
    4008, 4108, 4208, 4308, 4408, 563,  658,  1558, 2358, 3058, 3658, 4158,
    4508, 4608, 4708, 4808, 663,  758,  1658, 2458, 3158, 3758, 4258, 4658,
    4908, 5008, 5108, 763,  858,  1758, 2558, 3258, 3858, 4358, 4758, 5058,
    5208, 5308, 863,  5408, 958,  1858, 2658, 3358, 3958, 4458, 4858, 5158,
    5358, 963,  5514, 5564, 5614, 5664, 5714, 5764, 5814, 5864, 5914, 5964,
    6014, 6064, 6114, 6164, 6214, 6264, 6314, 6364, 6414, 6464, 6514, 6564,
    6614, 6664, 6714, 6764, 6815, 6865, 6915, 6965, 7015, 7065, 7115, 7165,
    7215, 7265, 7315, 7365, 7415, 7465, 7515, 7565, 7615, 7665, 7715, 7765,
    7815, 7865, 7915, 7965, 8015, 8065, 8115, 8165, 8215, 8265, 8315, 8365,
    8416, 8466, 8516, 8566, 8616, 8666, 8718, 8768, 8818, 8868, 8918, 30,
    40,   50};
inline uint16_t GetEmitOp1(size_t i) {
  return g_emit_op_1_inner[g_emit_op_1_outer[i]];
}
// max=43 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {39, 43};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_2[2] = {-1, 0};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=226 unique=22 flat=176 nested=352
static const uint8_t g_emit_buffer_3[22] = {
    0,  36,  64,  91,  93,  126, 94,  125, 60,  96,  123,
    92, 195, 208, 128, 130, 131, 162, 184, 194, 224, 226};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=3998 unique=40 flat=16384 nested=8832
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
    1,    191,  381,  571,  761,  951,  1142, 1332, 1523, 1713,
    1903, 2097, 2287, 2477, 2668, 2858, 3048, 3238, 3428, 3618,
    3808, 3998, 20,   30,   40,   50,   60,   70,   80,   90,
    100,  110,  120,  130,  140,  150,  160,  170,  180,  190};
inline uint16_t GetEmitOp3(size_t i) {
  return g_emit_op_3_inner[g_emit_op_3_outer[i]];
}
// max=161 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {153, 161};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_4[2] = {-1, 0};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=172 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {167, 172};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_5[2] = {-1, 0};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=177 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {176, 177};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_6[2] = {-1, 0};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=209 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {179, 209};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_7[2] = {-1, 0};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=217 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {216, 217};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_8[2] = {-1, 0};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=229 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {227, 229};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_9[2] = {-1, 0};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=0 unique=0 flat=0 nested=0
// monotonic increasing
static const uint8_t g_emit_buffer_10_outer[0] = {

};
// monotonic increasing
static const uint8_t g_emit_buffer_10_inner[0] = {

};
inline uint8_t GetEmitBuffer10(size_t i) {
  return g_emit_buffer_10_inner[g_emit_buffer_10_outer[i]];
}
// max=4 unique=2 flat=32 nested=48
// monotonic increasing
static const uint8_t g_emit_op_10[4] = {2, 2, 4, 4};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=134 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_11[2] = {133, 134};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_11[2] = {-1, 0};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=146 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_12[2] = {136, 146};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_12[2] = {-1, 0};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=0 unique=0 flat=0 nested=0
// monotonic increasing
static const uint8_t g_emit_buffer_13_outer[0] = {

};
// monotonic increasing
static const uint8_t g_emit_buffer_13_inner[0] = {

};
inline uint8_t GetEmitBuffer13(size_t i) {
  return g_emit_buffer_13_inner[g_emit_buffer_13_outer[i]];
}
// max=4 unique=2 flat=32 nested=48
// monotonic increasing
static const uint8_t g_emit_op_13[4] = {2, 2, 4, 4};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=156 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {154, 156};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_14[2] = {-1, 0};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=163 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {160, 163};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_15[2] = {-1, 0};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=0 unique=0 flat=0 nested=0
// monotonic increasing
static const uint8_t g_emit_buffer_16_outer[0] = {

};
// monotonic increasing
static const uint8_t g_emit_buffer_16_inner[0] = {

};
inline uint8_t GetEmitBuffer16(size_t i) {
  return g_emit_buffer_16_inner[g_emit_buffer_16_outer[i]];
}
// max=4 unique=2 flat=32 nested=48
// monotonic increasing
static const uint8_t g_emit_op_16[4] = {2, 2, 4, 4};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=169 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {164, 169};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_17[2] = {-1, 0};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=173 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {170, 173};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_18[2] = {-1, 0};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=0 unique=0 flat=0 nested=0
// monotonic increasing
static const uint8_t g_emit_buffer_19_outer[0] = {

};
// monotonic increasing
static const uint8_t g_emit_buffer_19_inner[0] = {

};
inline uint8_t GetEmitBuffer19(size_t i) {
  return g_emit_buffer_19_inner[g_emit_buffer_19_outer[i]];
}
// max=4 unique=2 flat=32 nested=48
// monotonic increasing
static const uint8_t g_emit_op_19[4] = {2, 2, 4, 4};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=181 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_20[2] = {178, 181};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_20[2] = {-1, 0};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=186 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_21[2] = {185, 186};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_21[2] = {-1, 0};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=0 unique=0 flat=0 nested=0
// monotonic increasing
static const uint8_t g_emit_buffer_22_outer[0] = {

};
// monotonic increasing
static const uint8_t g_emit_buffer_22_inner[0] = {

};
inline uint8_t GetEmitBuffer22(size_t i) {
  return g_emit_buffer_22_inner[g_emit_buffer_22_outer[i]];
}
// max=4 unique=2 flat=32 nested=48
// monotonic increasing
static const uint8_t g_emit_op_22[4] = {2, 2, 4, 4};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=189 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_23[2] = {187, 189};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_23[2] = {-1, 0};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=196 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_24[2] = {190, 196};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_24[2] = {-1, 0};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=0 unique=0 flat=0 nested=0
// monotonic increasing
static const uint8_t g_emit_buffer_25_outer[0] = {

};
// monotonic increasing
static const uint8_t g_emit_buffer_25_inner[0] = {

};
inline uint8_t GetEmitBuffer25(size_t i) {
  return g_emit_buffer_25_inner[g_emit_buffer_25_outer[i]];
}
// max=4 unique=2 flat=32 nested=48
// monotonic increasing
static const uint8_t g_emit_op_25[4] = {2, 2, 4, 4};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=228 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_26[2] = {198, 228};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_26[2] = {-1, 0};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=233 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_27[2] = {232, 233};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_27[2] = {-1, 0};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=143 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_28[8] = {1,   135, 137, 138,
                                            139, 140, 141, 143};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=22 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_28[8] = {1, 4, 7, 10, 13, 16, 19, 22};
inline uint8_t GetEmitOp28(size_t i) { return g_emit_op_28[i]; }
// max=158 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_29[8] = {147, 149, 150, 151,
                                            152, 155, 157, 158};
inline uint8_t GetEmitBuffer29(size_t i) { return g_emit_buffer_29[i]; }
// max=22 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_29[8] = {1, 4, 7, 10, 13, 16, 19, 22};
inline uint8_t GetEmitOp29(size_t i) { return g_emit_op_29[i]; }
// max=183 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_30[8] = {165, 166, 168, 174,
                                            175, 180, 182, 183};
inline uint8_t GetEmitBuffer30(size_t i) { return g_emit_buffer_30[i]; }
// max=22 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_30[8] = {1, 4, 7, 10, 13, 16, 19, 22};
inline uint8_t GetEmitOp30(size_t i) { return g_emit_op_30[i]; }
// max=230 unique=1 flat=8 nested=16
// monotonic increasing
static const uint8_t g_emit_buffer_31[1] = {230};
inline uint8_t GetEmitBuffer31(size_t i) { return g_emit_buffer_31[i]; }
// max=4 unique=2 flat=32 nested=48
static const uint8_t g_emit_op_31[4] = {-1, -1, 4, 4};
inline uint8_t GetEmitOp31(size_t i) { return g_emit_op_31[i]; }
// max=132 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_32[2] = {129, 132};
inline uint8_t GetEmitBuffer32(size_t i) { return g_emit_buffer_32[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_32[2] = {-1, 0};
inline uint8_t GetEmitOp32(size_t i) { return g_emit_op_32[i]; }
// max=239 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_33[11] = {188, 191, 197, 231, 239, 9,
                                             142, 144, 145, 148, 159};
inline uint8_t GetEmitBuffer33(size_t i) { return g_emit_buffer_33[i]; }
// max=42 unique=11 flat=128 nested=216
// monotonic increasing
static const uint8_t g_emit_op_33[16] = {1,  1,  5,  5,  9,  9,  13, 13,
                                         17, 17, 22, 26, 30, 34, 38, 42};
inline uint8_t GetEmitOp33(size_t i) { return g_emit_op_33[i]; }
// max=255 unique=76 flat=608 nested=1216
static const uint8_t g_emit_buffer_34[76] = {
    171, 206, 215, 225, 236, 237, 199, 207, 234, 235, 192, 193, 200,
    201, 202, 205, 210, 213, 218, 219, 238, 240, 242, 243, 255, 203,
    204, 211, 212, 214, 221, 222, 223, 241, 244, 245, 246, 247, 248,
    250, 251, 252, 253, 254, 2,   3,   4,   5,   6,   7,   8,   11,
    12,  14,  15,  16,  17,  18,  19,  20,  21,  23,  24,  25,  26,
    27,  28,  29,  30,  31,  127, 220, 249, 10,  13,  22};
inline uint8_t GetEmitBuffer34(size_t i) { return g_emit_buffer_34[i]; }
// max=1508 unique=77 flat=16384 nested=9424
// monotonic increasing
static const uint8_t g_emit_op_34_outer[1024] = {
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
static const uint16_t g_emit_op_34_inner[77] = {
    2,    22,   42,   62,   82,   102,  123,  143,  163,  183,  204,
    224,  244,  264,  284,  304,  324,  344,  364,  384,  404,  424,
    444,  464,  484,  505,  525,  545,  565,  585,  605,  625,  645,
    665,  685,  705,  725,  745,  765,  785,  805,  825,  845,  865,
    886,  906,  926,  946,  966,  986,  1006, 1026, 1046, 1066, 1086,
    1106, 1126, 1146, 1166, 1186, 1206, 1226, 1246, 1266, 1286, 1306,
    1326, 1346, 1366, 1386, 1406, 1426, 1446, 1468, 1488, 1508, 18};
inline uint16_t GetEmitOp34(size_t i) {
  return g_emit_op_34_inner[g_emit_op_34_outer[i]];
}
// max=124 unique=1 flat=8 nested=16
// monotonic increasing
static const uint8_t g_emit_buffer_35[1] = {124};
inline uint8_t GetEmitBuffer35(size_t i) { return g_emit_buffer_35[i]; }
// max=4 unique=2 flat=32 nested=48
static const uint8_t g_emit_op_35[4] = {-1, -1, 4, 4};
inline uint8_t GetEmitOp35(size_t i) { return g_emit_op_35[i]; }
// max=62 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_36[2] = {35, 62};
inline uint8_t GetEmitBuffer36(size_t i) { return g_emit_buffer_36[i]; }
// max=0 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_36[2] = {-1, 0};
inline uint8_t GetEmitOp36(size_t i) { return g_emit_op_36[i]; }
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
          DecodeStep33();
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
      case 9: {
        DecodeStep11();
        break;
      }
      case 10: {
        DecodeStep14();
        break;
      }
      case 11: {
        DecodeStep17();
        break;
      }
      case 1: {
        DecodeStep2();
        break;
      }
      case 12: {
        DecodeStep20();
        break;
      }
      case 13: {
        DecodeStep23();
        break;
      }
      case 14: {
        DecodeStep26();
        break;
      }
      case 15: {
        DecodeStep27();
        break;
      }
      case 16: {
        DecodeStep28();
        break;
      }
      case 7: {
        DecodeStep29();
        break;
      }
      case 2: {
        DecodeStep3();
        break;
      }
      case 17: {
        DecodeStep31();
        break;
      }
      case 18: {
        DecodeStep32();
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
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 1: {
        DecodeStep10();
        break;
      }
      case 0: {
        DecodeStep9();
        break;
      }
    }
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp11(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer11(op + 0));
  }
  void DecodeStep10() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp12(index);
    buffer_len_ -= op % 1;
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
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 0: {
        DecodeStep12();
        break;
      }
      case 1: {
        DecodeStep13();
        break;
      }
    }
  }
  void DecodeStep12() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp14(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer14(op + 0));
  }
  void DecodeStep13() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp15(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer15(op + 0));
  }
  void DecodeStep14() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp16(index);
    buffer_len_ -= op % 2;
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 0: {
        DecodeStep15();
        break;
      }
      case 1: {
        DecodeStep16();
        break;
      }
    }
  }
  void DecodeStep15() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp17(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer17(op + 0));
  }
  void DecodeStep16() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp18(index);
    buffer_len_ -= op % 1;
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
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 0: {
        DecodeStep18();
        break;
      }
      case 1: {
        DecodeStep19();
        break;
      }
    }
  }
  void DecodeStep18() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp20(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer20(op + 0));
  }
  void DecodeStep19() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp21(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer21(op + 0));
  }
  void DecodeStep20() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp22(index);
    buffer_len_ -= op % 2;
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 0: {
        DecodeStep21();
        break;
      }
      case 1: {
        DecodeStep22();
        break;
      }
    }
  }
  void DecodeStep21() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp23(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer23(op + 0));
  }
  void DecodeStep22() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp24(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer24(op + 0));
  }
  void DecodeStep23() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp25(index);
    buffer_len_ -= op % 2;
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 0: {
        DecodeStep24();
        break;
      }
      case 1: {
        DecodeStep25();
        break;
      }
    }
  }
  void DecodeStep24() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp26(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer26(op + 0));
  }
  void DecodeStep25() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp27(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer27(op + 0));
  }
  void DecodeStep26() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp28(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer28(op + 0));
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
  void DecodeStep27() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp29(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer29(op + 0));
  }
  void DecodeStep28() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp30(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer30(op + 0));
  }
  void DecodeStep29() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp31(index);
    buffer_len_ -= op % 2;
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 1: {
        DecodeStep30();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer31(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep30() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp32(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer32(op + 0));
  }
  void DecodeStep31() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 4);
    auto op = GetEmitOp33(index);
    buffer_len_ -= op % 4;
    sink_(GetEmitBuffer33(op + 0));
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
  void DecodeStep32() {
    if (!RefillTo10()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 10);
    auto op = GetEmitOp34(index);
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
        sink_(GetEmitBuffer34(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep33() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp35(index);
    buffer_len_ -= op % 2;
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 1: {
        DecodeStep34();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer35(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep34() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp36(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer36(op + 0));
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
