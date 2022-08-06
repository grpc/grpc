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
    3248, 3293, 9,    18,   27,   36};
inline uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=34 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_1[2] = {33, 34};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_1[2] = {10, 11};
inline uint8_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=41 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {40, 41};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {10, 11};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=63 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_3[3] = {63, 39, 43};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=15 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_3[4] = {10, 10, 13, 15};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=126 unique=14 flat=112 nested=224
static const uint8_t g_emit_buffer_4[14] = {124, 35,  62, 0,   36, 64, 91,
                                            93,  126, 94, 125, 60, 96, 123};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=1068 unique=22 flat=8192 nested=4448
// monotonic increasing
static const uint8_t g_emit_op_4_outer[512] = {
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
    1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12,
    12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 15, 16, 17, 18, 19, 20, 21};
static const uint16_t g_emit_op_4_inner[22] = {
    11,  93,  174,  256, 337, 418, 499, 580, 661, 743, 824,
    906, 987, 1068, 9,   18,  27,  36,  45,  54,  63,  72};
inline uint16_t GetEmitOp4(size_t i) {
  return g_emit_op_4_inner[g_emit_op_4_outer[i]];
}
// max=195 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {92, 195};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=20 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_5[2] = {19, 20};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=194 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_6[4] = {131, 162, 184, 194};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=26 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_6[4] = {20, 22, 24, 26};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=229 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_7[8] = {176, 177, 179, 209,
                                           216, 217, 227, 229};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=42 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_7[8] = {21, 24, 27, 30, 33, 36, 39, 42};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=208 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_8[3] = {208, 128, 130};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=24 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_8[4] = {19, 19, 22, 24};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=230 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_9[15] = {
    230, 129, 132, 133, 134, 136, 146, 154, 156, 160, 163, 164, 169, 170, 173};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=78 unique=15 flat=128 nested=248
// monotonic increasing
static const uint8_t g_emit_op_9[16] = {21, 21, 26, 30, 34, 38, 42, 46,
                                        50, 54, 58, 62, 66, 70, 74, 78};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=226 unique=6 flat=48 nested=96
static const uint8_t g_emit_buffer_10[6] = {224, 226, 153, 161, 167, 172};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=36 unique=6 flat=64 nested=112
// monotonic increasing
static const uint8_t g_emit_op_10[8] = {20, 20, 23, 23, 27, 30, 33, 36};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=233 unique=20 flat=160 nested=320
static const uint8_t g_emit_buffer_11[20] = {178, 181, 185, 186, 187, 189, 190,
                                             196, 198, 228, 232, 233, 1,   135,
                                             137, 138, 139, 140, 141, 143};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=118 unique=20 flat=256 nested=416
// monotonic increasing
static const uint8_t g_emit_op_11[32] = {
    22, 22, 27, 27, 32, 32, 37, 37, 42, 42, 47, 47, 52,  52,  57,  57,
    62, 62, 67, 67, 72, 72, 77, 77, 83, 88, 93, 98, 103, 108, 113, 118};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=255 unique=71 flat=568 nested=1136
static const uint8_t g_emit_buffer_12[71] = {
    147, 149, 150, 151, 152, 155, 157, 158, 165, 166, 168, 174, 175, 180, 182,
    183, 188, 191, 197, 231, 239, 9,   142, 144, 145, 148, 159, 171, 206, 215,
    225, 236, 237, 199, 207, 234, 235, 192, 193, 200, 201, 202, 205, 210, 213,
    218, 219, 238, 240, 242, 243, 255, 203, 204, 211, 212, 214, 221, 222, 223,
    241, 244, 245, 246, 247, 248, 250, 251, 252, 253, 254};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=10107 unique=86 flat=8192 nested=5472
// monotonic increasing
static const uint8_t g_emit_op_12_outer[512] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21,
    21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 24,
    24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26,
    26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28,
    28, 29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31,
    31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 32, 32, 33, 33, 33, 33, 34,
    34, 34, 34, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 38, 38, 39, 39, 40, 40,
    41, 41, 42, 42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47, 48, 48, 49, 49, 50,
    50, 51, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67,
    68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85};
static const uint16_t g_emit_op_12_inner[86] = {
    23,   167,  311,  455,  599,   743,  887,  1031, 1175, 1319, 1463,
    1607, 1751, 1895, 2039, 2183,  2327, 2471, 2615, 2759, 2903, 3048,
    3192, 3336, 3480, 3624, 3768,  3912, 4056, 4200, 4344, 4488, 4632,
    4777, 4921, 5065, 5209, 5354,  5498, 5642, 5786, 5930, 6074, 6218,
    6362, 6506, 6650, 6794, 6938,  7082, 7226, 7370, 7515, 7659, 7803,
    7947, 8091, 8235, 8379, 8523,  8667, 8811, 8955, 9099, 9243, 9387,
    9531, 9675, 9819, 9963, 10107, 9,    18,   27,   36,   45,   54,
    63,   72,   81,   90,   99,    108,  117,  126,  135};
inline uint16_t GetEmitOp12(size_t i) {
  return g_emit_op_12_inner[g_emit_op_12_outer[i]];
}
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_13[2] = {2, 3};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_13[2] = {28, 29};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=5 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {4, 5};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_14[2] = {28, 29};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {6, 7};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_15[2] = {28, 29};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_16[2] = {8, 11};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_16[2] = {28, 29};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=14 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {12, 14};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_17[2] = {28, 29};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=16 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {15, 16};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_18[2] = {28, 29};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=18 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_19[2] = {17, 18};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_19[2] = {28, 29};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=20 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_20[2] = {19, 20};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_20[2] = {28, 29};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_21[2] = {21, 23};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_21[2] = {28, 29};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=25 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_22[2] = {24, 25};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_22[2] = {28, 29};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_23[2] = {26, 27};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_23[2] = {28, 29};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_24[2] = {28, 29};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_24[2] = {28, 29};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=31 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_25[2] = {30, 31};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_25[2] = {28, 29};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=220 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_26[2] = {127, 220};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_26[2] = {28, 29};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=256 unique=5 flat=80 nested=120
static const uint16_t g_emit_buffer_27[5] = {249, 10, 13, 22, 256};
inline uint16_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=42 unique=5 flat=64 nested=104
// monotonic increasing
static const uint8_t g_emit_op_27[8] = {28, 28, 28, 28, 33, 36, 39, 42};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
template <typename F>
bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {
  uint64_t buffer = 0;
  uint64_t index;
  size_t emit_ofs;
  int buffer_len = 0;
  uint64_t op;
refill:
  while (buffer_len < 9) {
    if (begin == end) return buffer_len == 0;
    buffer <<= 8;
    buffer |= static_cast<uint64_t>(*begin++);
    buffer_len += 8;
  }
  index = buffer >> (buffer_len - 9);
  op = GetEmitOp0(index);
  buffer_len -= op % 9;
  op /= 9;
  emit_ofs = op / 5;
  switch (op % 5) {
    case 4: {
      // 0:8/4,1:3fd8/14,2:7ffe2/19,3:7ffe3/19,4:7ffe4/19,5:7ffe5/19,6:7ffe6/19,7:7ffe7/19,8:7ffe8/19,9:7fea/15,10:1ffffc/21,11:7ffe9/19,12:7ffea/19,13:1ffffd/21,14:7ffeb/19,15:7ffec/19,16:7ffed/19,17:7ffee/19,18:7ffef/19,19:7fff0/19,20:7fff1/19,21:7fff2/19,22:1ffffe/21,23:7fff3/19,24:7fff4/19,25:7fff5/19,26:7fff6/19,27:7fff7/19,28:7fff8/19,29:7fff9/19,30:7fffa/19,31:7fffb/19,35:2/3,36:9/4,60:3c/6,62:3/3,64:a/4,91:b/4,92:3f0/10,93:c/4,94:1c/5,96:3d/6,123:3e/6,124:0/2,125:1d/5,126:d/4,127:7fffc/19,128:7e6/11,129:1fd2/13,130:7e7/11,131:7e8/11,132:1fd3/13,133:1fd4/13,134:1fd5/13,135:3fd9/14,136:1fd6/13,137:3fda/14,138:3fdb/14,139:3fdc/14,140:3fdd/14,141:3fde/14,142:7feb/15,143:3fdf/14,144:7fec/15,145:7fed/15,146:1fd7/13,147:3fe0/14,148:7fee/15,149:3fe1/14,150:3fe2/14,151:3fe3/14,152:3fe4/14,153:fdc/12,154:1fd8/13,155:3fe5/14,156:1fd9/13,157:3fe6/14,158:3fe7/14,159:7fef/15,160:1fda/13,161:fdd/12,162:7e9/11,163:1fdb/13,164:1fdc/13,165:3fe8/14,166:3fe9/14,167:fde/12,168:3fea/14,169:1fdd/13,170:1fde/13,171:7ff0/15,172:fdf/12,173:1fdf/13,174:3feb/14,175:3fec/14,176:fe0/12,177:fe1/12,178:1fe0/13,179:fe2/12,180:3fed/14,181:1fe1/13,182:3fee/14,183:3fef/14,184:7ea/11,185:1fe2/13,186:1fe3/13,187:1fe4/13,188:3ff0/14,189:1fe5/13,190:1fe6/13,191:3ff1/14,192:1ffe0/17,193:1ffe1/17,194:7eb/11,195:3f1/10,196:1fe7/13,197:3ff2/14,198:1fe8/13,199:ffec/16,200:1ffe2/17,201:1ffe3/17,202:1ffe4/17,203:3ffde/18,204:3ffdf/18,205:1ffe5/17,206:7ff1/15,207:ffed/16,208:3f2/10,209:fe3/12,210:1ffe6/17,211:3ffe0/18,212:3ffe1/18,213:1ffe7/17,214:3ffe2/18,215:7ff2/15,216:fe4/12,217:fe5/12,218:1ffe8/17,219:1ffe9/17,220:7fffd/19,221:3ffe3/18,222:3ffe4/18,223:3ffe5/18,224:7ec/11,225:7ff3/15,226:7ed/11,227:fe6/12,228:1fe9/13,229:fe7/12,230:fe8/12,231:3ff3/14,232:1fea/13,233:1feb/13,234:ffee/16,235:ffef/16,236:7ff4/15,237:7ff5/15,238:1ffea/17,239:3ff4/14,240:1ffeb/17,241:3ffe6/18,242:1ffec/17,243:1ffed/17,244:3ffe7/18,245:3ffe8/18,246:3ffe9/18,247:3ffea/18,248:3ffeb/18,249:7fffe/19,250:3ffec/18,251:3ffed/18,252:3ffee/18,253:3ffef/18,254:3fff0/18,255:1ffee/17,256:1fffff/21
      while (buffer_len < 9) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 9);
      op = GetEmitOp4(index);
      buffer_len -= op % 9;
      op /= 9;
      emit_ofs = op / 9;
      switch (op % 9) {
        case 2: {
          // 128:2/2,130:3/2,208:0/1
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp8(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer8(op + 0));
          goto refill;
        }
        case 6: {
          // 129:2/4,132:3/4,133:4/4,134:5/4,136:6/4,146:7/4,154:8/4,156:9/4,160:a/4,163:b/4,164:c/4,169:d/4,170:e/4,173:f/4,230:0/3
          while (buffer_len < 4) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 4);
          op = GetEmitOp9(index);
          buffer_len -= op % 4;
          sink(GetEmitBuffer9(op + 0));
          goto refill;
        }
        case 3: {
          // 131:0/2,162:1/2,184:2/2,194:3/2
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp6(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer6(op + 0));
          goto refill;
        }
        case 4: {
          // 153:4/3,161:5/3,167:6/3,172:7/3,224:0/2,226:1/2
          while (buffer_len < 3) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 3);
          op = GetEmitOp10(index);
          buffer_len -= op % 3;
          sink(GetEmitBuffer10(op + 0));
          goto refill;
        }
        case 5: {
          // 176:0/3,177:1/3,179:2/3,209:3/3,216:4/3,217:5/3,227:6/3,229:7/3
          while (buffer_len < 3) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 3);
          op = GetEmitOp7(index);
          buffer_len -= op % 3;
          sink(GetEmitBuffer7(op + 0));
          goto refill;
        }
        case 7: {
          // 1:18/5,135:19/5,137:1a/5,138:1b/5,139:1c/5,140:1d/5,141:1e/5,143:1f/5,178:0/4,181:1/4,185:2/4,186:3/4,187:4/4,189:5/4,190:6/4,196:7/4,198:8/4,228:9/4,232:a/4,233:b/4
          while (buffer_len < 5) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 5);
          op = GetEmitOp11(index);
          buffer_len -= op % 5;
          sink(GetEmitBuffer11(op + 0));
          goto refill;
        }
        case 8: {
          // 2:3e2/10,3:3e3/10,4:3e4/10,5:3e5/10,6:3e6/10,7:3e7/10,8:3e8/10,9:2a/6,10:ffc/12,11:3e9/10,12:3ea/10,13:ffd/12,14:3eb/10,15:3ec/10,16:3ed/10,17:3ee/10,18:3ef/10,19:3f0/10,20:3f1/10,21:3f2/10,22:ffe/12,23:3f3/10,24:3f4/10,25:3f5/10,26:3f6/10,27:3f7/10,28:3f8/10,29:3f9/10,30:3fa/10,31:3fb/10,127:3fc/10,142:2b/6,144:2c/6,145:2d/6,147:0/5,148:2e/6,149:1/5,150:2/5,151:3/5,152:4/5,155:5/5,157:6/5,158:7/5,159:2f/6,165:8/5,166:9/5,168:a/5,171:30/6,174:b/5,175:c/5,180:d/5,182:e/5,183:f/5,188:10/5,191:11/5,192:e0/8,193:e1/8,197:12/5,199:6c/7,200:e2/8,201:e3/8,202:e4/8,203:1de/9,204:1df/9,205:e5/8,206:31/6,207:6d/7,210:e6/8,211:1e0/9,212:1e1/9,213:e7/8,214:1e2/9,215:32/6,218:e8/8,219:e9/8,220:3fd/10,221:1e3/9,222:1e4/9,223:1e5/9,225:33/6,231:13/5,234:6e/7,235:6f/7,236:34/6,237:35/6,238:ea/8,239:14/5,240:eb/8,241:1e6/9,242:ec/8,243:ed/8,244:1e7/9,245:1e8/9,246:1e9/9,247:1ea/9,248:1eb/9,249:3fe/10,250:1ec/9,251:1ed/9,252:1ee/9,253:1ef/9,254:1f0/9,255:ee/8,256:fff/12
          while (buffer_len < 9) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 9);
          op = GetEmitOp12(index);
          buffer_len -= op % 9;
          op /= 9;
          emit_ofs = op / 16;
          switch (op % 16) {
            case 15: {
              // 10:4/3,13:5/3,22:6/3,249:0/1,256:7/3
              while (buffer_len < 3) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 3);
              op = GetEmitOp27(index);
              buffer_len -= op % 3;
              sink(GetEmitBuffer27(op + 0));
              goto refill;
            }
            case 14: {
              // 127:0/1,220:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp26(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer26(op + 0));
              goto refill;
            }
            case 5: {
              // 12:0/1,14:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp17(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer17(op + 0));
              goto refill;
            }
            case 6: {
              // 15:0/1,16:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp18(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer18(op + 0));
              goto refill;
            }
            case 7: {
              // 17:0/1,18:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp19(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer19(op + 0));
              goto refill;
            }
            case 8: {
              // 19:0/1,20:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp20(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer20(op + 0));
              goto refill;
            }
            case 9: {
              // 21:0/1,23:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp21(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer21(op + 0));
              goto refill;
            }
            case 10: {
              // 24:0/1,25:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp22(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer22(op + 0));
              goto refill;
            }
            case 11: {
              // 26:0/1,27:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp23(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer23(op + 0));
              goto refill;
            }
            case 12: {
              // 28:0/1,29:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp24(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer24(op + 0));
              goto refill;
            }
            case 1: {
              // 2:0/1,3:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp13(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer13(op + 0));
              goto refill;
            }
            case 13: {
              // 30:0/1,31:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp25(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer25(op + 0));
              goto refill;
            }
            case 2: {
              // 4:0/1,5:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp14(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer14(op + 0));
              goto refill;
            }
            case 3: {
              // 6:0/1,7:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp15(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer15(op + 0));
              goto refill;
            }
            case 4: {
              // 8:0/1,11:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp16(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer16(op + 0));
              goto refill;
            }
            case 0: {
              sink(GetEmitBuffer12(emit_ofs + 0));
              goto refill;
            }
          }
        }
        case 1: {
          // 92:0/1,195:1/1
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
        case 0: {
          sink(GetEmitBuffer4(emit_ofs + 0));
          goto refill;
        }
      }
    }
    case 1: {
      // 33:0/1,34:1/1
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
    case 3: {
      // 39:2/2,43:3/2,63:0/1
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp3(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer3(op + 0));
      goto refill;
    }
    case 2: {
      // 40:0/1,41:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp2(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer2(op + 0));
      goto refill;
    }
    case 0: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      goto refill;
    }
  }
  abort();
}
