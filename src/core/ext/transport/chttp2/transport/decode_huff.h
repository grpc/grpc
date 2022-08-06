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
// max=1760 unique=76 flat=4096 nested=3264
// monotonic increasing
static const uint8_t g_emit_op_0_outer[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
    2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,
    4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  7,
    7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,
    9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13,
    13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18,
    18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23,
    23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27,
    28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32,
    32, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 36, 36, 37, 37, 38, 38,
    39, 39, 40, 40, 41, 41, 42, 42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47, 48,
    48, 49, 49, 50, 50, 51, 51, 52, 52, 53, 53, 54, 54, 55, 55, 56, 56, 57, 57,
    58, 58, 59, 59, 60, 60, 61, 61, 62, 62, 63, 63, 64, 64, 65, 65, 66, 66, 67,
    67, 68, 69, 70, 71, 72, 73, 74, 75};
static const uint16_t g_emit_op_0_inner[76] = {
    5,    29,   53,   77,   101,  125,  149,  173,  197,  221,  246,
    270,  294,  318,  342,  366,  390,  414,  438,  462,  486,  510,
    534,  558,  582,  606,  630,  654,  678,  702,  726,  750,  774,
    798,  822,  846,  871,  895,  919,  943,  967,  991,  1015, 1039,
    1063, 1087, 1111, 1135, 1159, 1183, 1207, 1231, 1255, 1279, 1303,
    1327, 1351, 1375, 1399, 1423, 1447, 1471, 1495, 1519, 1543, 1567,
    1591, 1615, 1640, 1664, 1688, 1712, 1736, 1760, 16,   24};
inline uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=122 unique=74 flat=592 nested=1184
static const uint8_t g_emit_buffer_1[74] = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32, 37,  45,  46,  47,
    51,  52,  53,  54,  55,  56,  57,  61,  65,  95,  98, 100, 102, 103, 104,
    108, 109, 110, 112, 114, 117, 58,  66,  67,  68,  69, 70,  71,  72,  73,
    74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84, 85,  86,  87,  89,
    106, 107, 113, 118, 119, 120, 121, 122, 38,  42,  44, 59,  88,  90};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=3293 unique=78 flat=8192 nested=5344
// monotonic increasing
static const uint8_t g_emit_op_1_outer[512] = {
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
static const uint16_t g_emit_op_1_inner[78] = {
    5,    50,   95,   140,  185,  230,  275,  320,  365,  410,  456,  501,
    546,  591,  636,  681,  726,  771,  816,  861,  906,  951,  996,  1041,
    1086, 1131, 1176, 1221, 1266, 1311, 1356, 1401, 1446, 1491, 1536, 1581,
    1627, 1672, 1717, 1762, 1807, 1852, 1897, 1942, 1987, 2032, 2077, 2122,
    2167, 2212, 2257, 2302, 2347, 2392, 2437, 2482, 2527, 2572, 2617, 2662,
    2707, 2752, 2797, 2842, 2887, 2932, 2977, 3022, 3068, 3113, 3158, 3203,
    3248, 3293, 18,   27,   36,   45};
inline uint16_t GetEmitOp1(size_t i) {
  return g_emit_op_1_inner[g_emit_op_1_outer[i]];
}
// max=34 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {33, 34};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {10, 11};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=41 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {40, 41};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {10, 11};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=63 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_4[3] = {63, 39, 43};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=15 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_4[4] = {10, 10, 13, 15};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=126 unique=14 flat=112 nested=224
static const uint8_t g_emit_buffer_5[14] = {124, 35,  62, 0,   36, 64, 91,
                                            93,  126, 94, 125, 60, 96, 123};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=1068 unique=22 flat=8192 nested=4448
// monotonic increasing
static const uint8_t g_emit_op_5_outer[512] = {
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
static const uint16_t g_emit_op_5_inner[22] = {
    11,  93,  174,  256, 337, 418, 499, 580, 661, 743, 824,
    906, 987, 1068, 18,  27,  36,  45,  54,  63,  72,  81};
inline uint16_t GetEmitOp5(size_t i) {
  return g_emit_op_5_inner[g_emit_op_5_outer[i]];
}
// max=195 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {92, 195};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=20 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {19, 20};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=194 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_7[4] = {131, 162, 184, 194};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=26 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_7[4] = {20, 22, 24, 26};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=229 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_8[8] = {176, 177, 179, 209,
                                           216, 217, 227, 229};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=42 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_8[8] = {21, 24, 27, 30, 33, 36, 39, 42};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=208 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_9[3] = {208, 128, 130};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=24 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_9[4] = {19, 19, 22, 24};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=230 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_10[15] = {
    230, 129, 132, 133, 134, 136, 146, 154, 156, 160, 163, 164, 169, 170, 173};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=78 unique=15 flat=128 nested=248
// monotonic increasing
static const uint8_t g_emit_op_10[16] = {21, 21, 26, 30, 34, 38, 42, 46,
                                         50, 54, 58, 62, 66, 70, 74, 78};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=226 unique=6 flat=48 nested=96
static const uint8_t g_emit_buffer_11[6] = {224, 226, 153, 161, 167, 172};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=36 unique=6 flat=64 nested=112
// monotonic increasing
static const uint8_t g_emit_op_11[8] = {20, 20, 23, 23, 27, 30, 33, 36};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=233 unique=20 flat=160 nested=320
static const uint8_t g_emit_buffer_12[20] = {178, 181, 185, 186, 187, 189, 190,
                                             196, 198, 228, 232, 233, 1,   135,
                                             137, 138, 139, 140, 141, 143};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=118 unique=20 flat=256 nested=416
// monotonic increasing
static const uint8_t g_emit_op_12[32] = {
    22, 22, 27, 27, 32, 32, 37, 37, 42, 42, 47, 47, 52,  52,  57,  57,
    62, 62, 67, 67, 72, 72, 77, 77, 83, 88, 93, 98, 103, 108, 113, 118};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=255 unique=71 flat=568 nested=1136
static const uint8_t g_emit_buffer_13[71] = {
    147, 149, 150, 151, 152, 155, 157, 158, 165, 166, 168, 174, 175, 180, 182,
    183, 188, 191, 197, 231, 239, 9,   142, 144, 145, 148, 159, 171, 206, 215,
    225, 236, 237, 199, 207, 234, 235, 192, 193, 200, 201, 202, 205, 210, 213,
    218, 219, 238, 240, 242, 243, 255, 203, 204, 211, 212, 214, 221, 222, 223,
    241, 244, 245, 246, 247, 248, 250, 251, 252, 253, 254};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=10107 unique=86 flat=8192 nested=5472
// monotonic increasing
static const uint8_t g_emit_op_13_outer[512] = {
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
static const uint16_t g_emit_op_13_inner[86] = {
    23,   167,  311,  455,  599,   743,  887,  1031, 1175, 1319, 1463,
    1607, 1751, 1895, 2039, 2183,  2327, 2471, 2615, 2759, 2903, 3048,
    3192, 3336, 3480, 3624, 3768,  3912, 4056, 4200, 4344, 4488, 4632,
    4777, 4921, 5065, 5209, 5354,  5498, 5642, 5786, 5930, 6074, 6218,
    6362, 6506, 6650, 6794, 6938,  7082, 7226, 7370, 7515, 7659, 7803,
    7947, 8091, 8235, 8379, 8523,  8667, 8811, 8955, 9099, 9243, 9387,
    9531, 9675, 9819, 9963, 10107, 18,   27,   36,   45,   54,   63,
    72,   81,   90,   99,   108,   117,  126,  135,  144};
inline uint16_t GetEmitOp13(size_t i) {
  return g_emit_op_13_inner[g_emit_op_13_outer[i]];
}
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {2, 3};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_14[2] = {28, 29};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=5 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {4, 5};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_15[2] = {28, 29};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_16[2] = {6, 7};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_16[2] = {28, 29};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {8, 11};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_17[2] = {28, 29};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=14 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {12, 14};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_18[2] = {28, 29};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=16 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_19[2] = {15, 16};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_19[2] = {28, 29};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=18 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_20[2] = {17, 18};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_20[2] = {28, 29};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=20 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_21[2] = {19, 20};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_21[2] = {28, 29};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_22[2] = {21, 23};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_22[2] = {28, 29};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=25 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_23[2] = {24, 25};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_23[2] = {28, 29};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_24[2] = {26, 27};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_24[2] = {28, 29};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_25[2] = {28, 29};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_25[2] = {28, 29};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=31 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_26[2] = {30, 31};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_26[2] = {28, 29};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=220 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_27[2] = {127, 220};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_27[2] = {28, 29};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=249 unique=4 flat=32 nested=64
static const uint8_t g_emit_buffer_28[4] = {249, 10, 13, 22};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=48 unique=5 flat=64 nested=104
static const uint8_t g_emit_op_28[8] = {28, 28, 28, 28, 36, 42, 48, 33};
inline uint8_t GetEmitOp28(size_t i) { return g_emit_op_28[i]; }
template <typename F>
class HuffDecoder {
 public:
  HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end)
      : sink_(sink), begin_(begin), end_(end) {}
  bool Run() {
    while (ok_) {
      if (!RefillTo9()) {
        Done();
        return ok_;
      }
      const auto index = buffer_ >> (buffer_len_ - 9);
      auto op = GetEmitOp1(index);
      buffer_len_ -= op % 9;
      op /= 9;
      const auto emit_ofs = op / 5;
      switch (op % 5) {
        case 1: {
          DecodeStep0();
          break;
        }
        case 2: {
          DecodeStep1();
          break;
        }
        case 3: {
          DecodeStep2();
          break;
        }
        case 4: {
          DecodeStep3();
          break;
        }
        case 0: {
          sink_(GetEmitBuffer1(emit_ofs + 0));
          break;
        }
      }
    }
    return ok_;
  }

 private:
  bool RefillTo9() {
    switch (buffer_len_) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8: {
        return Read1();
      }
      case 0: {
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp3(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer3(op + 0));
  }
  void DecodeStep2() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp4(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer4(op + 0));
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
  void DecodeStep3() {
    if (!RefillTo9()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 9);
    auto op = GetEmitOp5(index);
    buffer_len_ -= op % 9;
    op /= 9;
    const auto emit_ofs = op / 9;
    switch (op % 9) {
      case 7: {
        DecodeStep10();
        break;
      }
      case 8: {
        DecodeStep11();
        break;
      }
      case 1: {
        DecodeStep4();
        break;
      }
      case 3: {
        DecodeStep5();
        break;
      }
      case 5: {
        DecodeStep6();
        break;
      }
      case 2: {
        DecodeStep7();
        break;
      }
      case 6: {
        DecodeStep8();
        break;
      }
      case 4: {
        DecodeStep9();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer5(emit_ofs + 0));
        break;
      }
    }
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp7(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer7(op + 0));
  }
  void DecodeStep6() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp8(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer8(op + 0));
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
  void DecodeStep7() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp9(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer9(op + 0));
  }
  void DecodeStep8() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 4);
    auto op = GetEmitOp10(index);
    buffer_len_ -= op % 4;
    sink_(GetEmitBuffer10(op + 0));
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
  void DecodeStep9() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp11(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer11(op + 0));
  }
  void DecodeStep10() {
    if (!RefillTo5()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 5);
    auto op = GetEmitOp12(index);
    buffer_len_ -= op % 5;
    sink_(GetEmitBuffer12(op + 0));
  }
  bool RefillTo5() {
    switch (buffer_len_) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4: {
        return Read1();
      }
    }
    return true;
  }
  void DecodeStep11() {
    if (!RefillTo9()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 9);
    auto op = GetEmitOp13(index);
    buffer_len_ -= op % 9;
    op /= 9;
    const auto emit_ofs = op / 16;
    switch (op % 16) {
      case 1: {
        DecodeStep12();
        break;
      }
      case 2: {
        DecodeStep13();
        break;
      }
      case 3: {
        DecodeStep14();
        break;
      }
      case 4: {
        DecodeStep15();
        break;
      }
      case 5: {
        DecodeStep16();
        break;
      }
      case 6: {
        DecodeStep17();
        break;
      }
      case 7: {
        DecodeStep18();
        break;
      }
      case 8: {
        DecodeStep19();
        break;
      }
      case 9: {
        DecodeStep20();
        break;
      }
      case 10: {
        DecodeStep21();
        break;
      }
      case 11: {
        DecodeStep22();
        break;
      }
      case 12: {
        DecodeStep23();
        break;
      }
      case 13: {
        DecodeStep24();
        break;
      }
      case 14: {
        DecodeStep25();
        break;
      }
      case 15: {
        DecodeStep26();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer13(emit_ofs + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp16(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer16(op + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp19(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer19(op + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp22(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer22(op + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp25(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer25(op + 0));
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
    op /= 3;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmitBuffer28(emit_ofs + 0));
        break;
      }
    }
  }
  void Done() {
    if (buffer_len_ < 8) {
      buffer_ = (buffer_ << (8 - buffer_len_)) |
                ((uint64_t(1) << (8 - buffer_len_)) - 1);
      buffer_len_ = 8;
    }
    const auto index = buffer_ >> (buffer_len_ - 8);
    auto op = GetEmitOp0(index);
    buffer_len_ -= op % 8;
    op /= 8;
    const auto emit_ofs = op / 3;
    switch (op % 3) {
      case 1:
      case 2: {
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
