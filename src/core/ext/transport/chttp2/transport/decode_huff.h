#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=122 unique=74 flat=592 nested=1184
static const uint8_t g_emit_buffer_0[74] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20,
    0x25, 0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3d, 0x41, 0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e,
    0x70, 0x72, 0x75, 0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x59, 0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x26, 0x2a, 0x2c, 0x3b, 0x58, 0x5a};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=4680 unique=76 flat=4096 nested=3264
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
    0x0005, 0x0045, 0x0085, 0x00c5, 0x0105, 0x0145, 0x0185, 0x01c5, 0x0205,
    0x0245, 0x0286, 0x02c6, 0x0306, 0x0346, 0x0386, 0x03c6, 0x0406, 0x0446,
    0x0486, 0x04c6, 0x0506, 0x0546, 0x0586, 0x05c6, 0x0606, 0x0646, 0x0686,
    0x06c6, 0x0706, 0x0746, 0x0786, 0x07c6, 0x0806, 0x0846, 0x0886, 0x08c6,
    0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87, 0x0ac7, 0x0b07,
    0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7, 0x0d07, 0x0d47,
    0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07, 0x0f47, 0x0f87,
    0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x1108, 0x1148, 0x1188, 0x11c8,
    0x1208, 0x1248, 0x0018, 0x0028};
inline uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=122 unique=74 flat=592 nested=1184
static const uint8_t g_emit_buffer_1[74] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20,
    0x25, 0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3d, 0x41, 0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e,
    0x70, 0x72, 0x75, 0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x59, 0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x26, 0x2a, 0x2c, 0x3b, 0x58, 0x5a};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=9352 unique=78 flat=8192 nested=5344
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
    0x0005, 0x0085, 0x0105, 0x0185, 0x0205, 0x0285, 0x0305, 0x0385, 0x0405,
    0x0485, 0x0506, 0x0586, 0x0606, 0x0686, 0x0706, 0x0786, 0x0806, 0x0886,
    0x0906, 0x0986, 0x0a06, 0x0a86, 0x0b06, 0x0b86, 0x0c06, 0x0c86, 0x0d06,
    0x0d86, 0x0e06, 0x0e86, 0x0f06, 0x0f86, 0x1006, 0x1086, 0x1106, 0x1186,
    0x1207, 0x1287, 0x1307, 0x1387, 0x1407, 0x1487, 0x1507, 0x1587, 0x1607,
    0x1687, 0x1707, 0x1787, 0x1807, 0x1887, 0x1907, 0x1987, 0x1a07, 0x1a87,
    0x1b07, 0x1b87, 0x1c07, 0x1c87, 0x1d07, 0x1d87, 0x1e07, 0x1e87, 0x1f07,
    0x1f87, 0x2007, 0x2087, 0x2107, 0x2187, 0x2208, 0x2288, 0x2308, 0x2388,
    0x2408, 0x2488, 0x0019, 0x0029, 0x0039, 0x0049};
inline uint16_t GetEmitOp1(size_t i) {
  return g_emit_op_1_inner[g_emit_op_1_outer[i]];
}
// max=34 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {0x21, 0x22};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {0x01, 0x03};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=41 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {0x28, 0x29};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {0x01, 0x03};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=63 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_4[3] = {0x3f, 0x27, 0x2b};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=10 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_4[4] = {0x01, 0x01, 0x06, 0x0a};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=126 unique=82 flat=3240 nested=3896
static const uint8_t g_emit_buffer_5[405] = {
    0x7c, 0x30, 0x7c, 0x31, 0x7c, 0x32, 0x7c, 0x61, 0x7c, 0x63, 0x7c, 0x65,
    0x7c, 0x69, 0x7c, 0x6f, 0x7c, 0x73, 0x7c, 0x74, 0x7c, 0x20, 0x7c, 0x25,
    0x7c, 0x2d, 0x7c, 0x2e, 0x7c, 0x2f, 0x7c, 0x33, 0x7c, 0x34, 0x7c, 0x35,
    0x7c, 0x36, 0x7c, 0x37, 0x7c, 0x38, 0x7c, 0x39, 0x7c, 0x3d, 0x7c, 0x41,
    0x7c, 0x5f, 0x7c, 0x62, 0x7c, 0x64, 0x7c, 0x66, 0x7c, 0x67, 0x7c, 0x68,
    0x7c, 0x6c, 0x7c, 0x6d, 0x7c, 0x6e, 0x7c, 0x70, 0x7c, 0x72, 0x7c, 0x75,
    0x7c, 0x3a, 0x7c, 0x42, 0x7c, 0x43, 0x7c, 0x44, 0x7c, 0x45, 0x7c, 0x46,
    0x7c, 0x47, 0x7c, 0x48, 0x7c, 0x49, 0x7c, 0x4a, 0x7c, 0x4b, 0x7c, 0x4c,
    0x7c, 0x4d, 0x7c, 0x4e, 0x7c, 0x4f, 0x7c, 0x50, 0x7c, 0x51, 0x7c, 0x52,
    0x7c, 0x53, 0x7c, 0x54, 0x7c, 0x55, 0x7c, 0x56, 0x7c, 0x57, 0x7c, 0x59,
    0x7c, 0x6a, 0x7c, 0x6b, 0x7c, 0x71, 0x7c, 0x76, 0x7c, 0x77, 0x7c, 0x78,
    0x7c, 0x79, 0x7c, 0x7a, 0x23, 0x30, 0x23, 0x31, 0x23, 0x32, 0x23, 0x61,
    0x23, 0x63, 0x23, 0x65, 0x23, 0x69, 0x23, 0x6f, 0x23, 0x73, 0x23, 0x74,
    0x23, 0x20, 0x23, 0x25, 0x23, 0x2d, 0x23, 0x2e, 0x23, 0x2f, 0x23, 0x33,
    0x23, 0x34, 0x23, 0x35, 0x23, 0x36, 0x23, 0x37, 0x23, 0x38, 0x23, 0x39,
    0x23, 0x3d, 0x23, 0x41, 0x23, 0x5f, 0x23, 0x62, 0x23, 0x64, 0x23, 0x66,
    0x23, 0x67, 0x23, 0x68, 0x23, 0x6c, 0x23, 0x6d, 0x23, 0x6e, 0x23, 0x70,
    0x23, 0x72, 0x23, 0x75, 0x3e, 0x30, 0x3e, 0x31, 0x3e, 0x32, 0x3e, 0x61,
    0x3e, 0x63, 0x3e, 0x65, 0x3e, 0x69, 0x3e, 0x6f, 0x3e, 0x73, 0x3e, 0x74,
    0x3e, 0x20, 0x3e, 0x25, 0x3e, 0x2d, 0x3e, 0x2e, 0x3e, 0x2f, 0x3e, 0x33,
    0x3e, 0x34, 0x3e, 0x35, 0x3e, 0x36, 0x3e, 0x37, 0x3e, 0x38, 0x3e, 0x39,
    0x3e, 0x3d, 0x3e, 0x41, 0x3e, 0x5f, 0x3e, 0x62, 0x3e, 0x64, 0x3e, 0x66,
    0x3e, 0x67, 0x3e, 0x68, 0x3e, 0x6c, 0x3e, 0x6d, 0x3e, 0x6e, 0x3e, 0x70,
    0x3e, 0x72, 0x3e, 0x75, 0x00, 0x30, 0x00, 0x31, 0x00, 0x32, 0x00, 0x61,
    0x00, 0x63, 0x00, 0x65, 0x00, 0x69, 0x00, 0x6f, 0x00, 0x73, 0x00, 0x74,
    0x24, 0x30, 0x24, 0x31, 0x24, 0x32, 0x24, 0x61, 0x24, 0x63, 0x24, 0x65,
    0x24, 0x69, 0x24, 0x6f, 0x24, 0x73, 0x24, 0x74, 0x40, 0x30, 0x40, 0x31,
    0x40, 0x32, 0x40, 0x61, 0x40, 0x63, 0x40, 0x65, 0x40, 0x69, 0x40, 0x6f,
    0x40, 0x73, 0x40, 0x74, 0x5b, 0x30, 0x5b, 0x31, 0x5b, 0x32, 0x5b, 0x61,
    0x5b, 0x63, 0x5b, 0x65, 0x5b, 0x69, 0x5b, 0x6f, 0x5b, 0x73, 0x5b, 0x74,
    0x5d, 0x30, 0x5d, 0x31, 0x5d, 0x32, 0x5d, 0x61, 0x5d, 0x63, 0x5d, 0x65,
    0x5d, 0x69, 0x5d, 0x6f, 0x5d, 0x73, 0x5d, 0x74, 0x7e, 0x30, 0x7e, 0x31,
    0x7e, 0x32, 0x7e, 0x61, 0x7e, 0x63, 0x7e, 0x65, 0x7e, 0x69, 0x7e, 0x6f,
    0x7e, 0x73, 0x7e, 0x74, 0x5e, 0x7d, 0x3c, 0x60, 0x7b};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=103446 unique=222 flat=16384 nested=11200
// monotonic increasing
static const uint8_t g_emit_op_5_outer[512] = {
    0,   0,   0,   0,   1,   1,   1,   1,   2,   2,   2,   2,   3,   3,   3,
    3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,   6,   7,   7,
    7,   7,   8,   8,   8,   8,   9,   9,   9,   9,   10,  10,  11,  11,  12,
    12,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
    20,  20,  21,  21,  22,  22,  23,  23,  24,  24,  25,  25,  26,  26,  27,
    27,  28,  28,  29,  29,  30,  30,  31,  31,  32,  32,  33,  33,  34,  34,
    35,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
    64,  65,  66,  67,  68,  68,  68,  68,  69,  69,  70,  70,  71,  71,  72,
    72,  73,  73,  74,  74,  75,  75,  76,  76,  77,  77,  78,  78,  79,  80,
    81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
    96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 105, 105, 105, 105, 105,
    105, 105, 105, 105, 105, 105, 105, 105, 105, 105, 105, 105, 106, 106, 107,
    107, 108, 108, 109, 109, 110, 110, 111, 111, 112, 112, 113, 113, 114, 114,
    115, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
    129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 142,
    142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142,
    142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 153, 153, 153,
    153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153,
    153, 153, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 164,
    164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164,
    164, 164, 164, 164, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174,
    175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175,
    175, 175, 175, 175, 175, 175, 175, 176, 177, 178, 179, 180, 181, 182, 183,
    184, 185, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186,
    186, 186, 186, 186, 186, 186, 186, 186, 186, 187, 188, 189, 190, 191, 192,
    193, 194, 195, 196, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197,
    197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 198, 199, 200, 201,
    202, 203, 204, 205, 206, 207, 208, 208, 208, 208, 208, 208, 208, 208, 208,
    208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 209, 209,
    209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 210,
    210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210,
    211, 211, 211, 211, 211, 211, 211, 211, 212, 212, 212, 212, 212, 212, 212,
    212, 213, 213, 213, 213, 213, 213, 213, 213, 214, 215, 216, 217, 218, 219,
    220, 221};
static const uint32_t g_emit_op_5_inner[222] = {
    0x00000007, 0x00000207, 0x00000407, 0x00000607, 0x00000807, 0x00000a07,
    0x00000c07, 0x00000e07, 0x00001007, 0x00001207, 0x00001408, 0x00001608,
    0x00001808, 0x00001a08, 0x00001c08, 0x00001e08, 0x00002008, 0x00002208,
    0x00002408, 0x00002608, 0x00002808, 0x00002a08, 0x00002c08, 0x00002e08,
    0x00003008, 0x00003208, 0x00003408, 0x00003608, 0x00003808, 0x00003a08,
    0x00003c08, 0x00003e08, 0x00004008, 0x00004208, 0x00004408, 0x00004608,
    0x00004809, 0x00004a09, 0x00004c09, 0x00004e09, 0x00005009, 0x00005209,
    0x00005409, 0x00005609, 0x00005809, 0x00005a09, 0x00005c09, 0x00005e09,
    0x00006009, 0x00006209, 0x00006409, 0x00006609, 0x00006809, 0x00006a09,
    0x00006c09, 0x00006e09, 0x00007009, 0x00007209, 0x00007409, 0x00007609,
    0x00007809, 0x00007a09, 0x00007c09, 0x00007e09, 0x00008009, 0x00008209,
    0x00008409, 0x00008609, 0x00000012, 0x00008808, 0x00008a08, 0x00008c08,
    0x00008e08, 0x00009008, 0x00009208, 0x00009408, 0x00009608, 0x00009808,
    0x00009a08, 0x00009c09, 0x00009e09, 0x0000a009, 0x0000a209, 0x0000a409,
    0x0000a609, 0x0000a809, 0x0000aa09, 0x0000ac09, 0x0000ae09, 0x0000b009,
    0x0000b209, 0x0000b409, 0x0000b609, 0x0000b809, 0x0000ba09, 0x0000bc09,
    0x0000be09, 0x0000c009, 0x0000c209, 0x0000c409, 0x0000c609, 0x0000c809,
    0x0000ca09, 0x0000cc09, 0x0000ce09, 0x00008813, 0x0000d008, 0x0000d208,
    0x0000d408, 0x0000d608, 0x0000d808, 0x0000da08, 0x0000dc08, 0x0000de08,
    0x0000e008, 0x0000e208, 0x0000e409, 0x0000e609, 0x0000e809, 0x0000ea09,
    0x0000ec09, 0x0000ee09, 0x0000f009, 0x0000f209, 0x0000f409, 0x0000f609,
    0x0000f809, 0x0000fa09, 0x0000fc09, 0x0000fe09, 0x00010009, 0x00010209,
    0x00010409, 0x00010609, 0x00010809, 0x00010a09, 0x00010c09, 0x00010e09,
    0x00011009, 0x00011209, 0x00011409, 0x00011609, 0x0000d013, 0x00011809,
    0x00011a09, 0x00011c09, 0x00011e09, 0x00012009, 0x00012209, 0x00012409,
    0x00012609, 0x00012809, 0x00012a09, 0x00011814, 0x00012c09, 0x00012e09,
    0x00013009, 0x00013209, 0x00013409, 0x00013609, 0x00013809, 0x00013a09,
    0x00013c09, 0x00013e09, 0x00012c14, 0x00014009, 0x00014209, 0x00014409,
    0x00014609, 0x00014809, 0x00014a09, 0x00014c09, 0x00014e09, 0x00015009,
    0x00015209, 0x00014014, 0x00015409, 0x00015609, 0x00015809, 0x00015a09,
    0x00015c09, 0x00015e09, 0x00016009, 0x00016209, 0x00016409, 0x00016609,
    0x00015414, 0x00016809, 0x00016a09, 0x00016c09, 0x00016e09, 0x00017009,
    0x00017209, 0x00017409, 0x00017609, 0x00017809, 0x00017a09, 0x00016814,
    0x00017c09, 0x00017e09, 0x00018009, 0x00018209, 0x00018409, 0x00018609,
    0x00018809, 0x00018a09, 0x00018c09, 0x00018e09, 0x00017c14, 0x00019015,
    0x00019115, 0x00019216, 0x00019316, 0x00019416, 0x00000029, 0x00000039,
    0x00000049, 0x00000059, 0x00000069, 0x00000079, 0x00000089, 0x00000099};
inline uint32_t GetEmitOp5(size_t i) {
  return g_emit_op_5_inner[g_emit_op_5_outer[i]];
}
// max=195 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {0x5c, 0xc3};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {0x01, 0x03};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=194 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_7[4] = {0x83, 0xa2, 0xb8, 0xc2};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_7[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=229 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_8[8] = {0xb0, 0xb1, 0xb3, 0xd1,
                                           0xd8, 0xd9, 0xe3, 0xe5};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=31 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_8[8] = {0x03, 0x07, 0x0b, 0x0f,
                                       0x13, 0x17, 0x1b, 0x1f};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=208 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_9[3] = {0xd0, 0x80, 0x82};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=10 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_9[4] = {0x01, 0x01, 0x06, 0x0a};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=230 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_10[15] = {0xe6, 0x81, 0x84, 0x85, 0x86,
                                             0x88, 0x92, 0x9a, 0x9c, 0xa0,
                                             0xa3, 0xa4, 0xa9, 0xaa, 0xad};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=116 unique=15 flat=128 nested=248
// monotonic increasing
static const uint8_t g_emit_op_10[16] = {0x03, 0x03, 0x0c, 0x14, 0x1c, 0x24,
                                         0x2c, 0x34, 0x3c, 0x44, 0x4c, 0x54,
                                         0x5c, 0x64, 0x6c, 0x74};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=226 unique=6 flat=48 nested=96
static const uint8_t g_emit_buffer_11[6] = {0xe0, 0xe2, 0x99, 0xa1, 0xa7, 0xac};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=23 unique=6 flat=64 nested=112
// monotonic increasing
static const uint8_t g_emit_op_11[8] = {0x02, 0x02, 0x06, 0x06,
                                        0x0b, 0x0f, 0x13, 0x17};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=233 unique=20 flat=160 nested=320
static const uint8_t g_emit_buffer_12[20] = {
    0xb2, 0xb5, 0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4,
    0xe8, 0xe9, 0x01, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8f};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=157 unique=20 flat=256 nested=416
// monotonic increasing
static const uint8_t g_emit_op_12[32] = {
    0x04, 0x04, 0x0c, 0x0c, 0x14, 0x14, 0x1c, 0x1c, 0x24, 0x24, 0x2c,
    0x2c, 0x34, 0x34, 0x3c, 0x3c, 0x44, 0x44, 0x4c, 0x4c, 0x54, 0x54,
    0x5c, 0x5c, 0x65, 0x6d, 0x75, 0x7d, 0x85, 0x8d, 0x95, 0x9d};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=255 unique=71 flat=568 nested=1136
static const uint8_t g_emit_buffer_13[71] = {
    0x93, 0x95, 0x96, 0x97, 0x98, 0x9b, 0x9d, 0x9e, 0xa5, 0xa6, 0xa8, 0xae,
    0xaf, 0xb4, 0xb6, 0xb7, 0xbc, 0xbf, 0xc5, 0xe7, 0xef, 0x09, 0x8e, 0x90,
    0x91, 0x94, 0x9f, 0xab, 0xce, 0xd7, 0xe1, 0xec, 0xed, 0xc7, 0xcf, 0xea,
    0xeb, 0xc0, 0xc1, 0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda, 0xdb, 0xee,
    0xf0, 0xf2, 0xf3, 0xff, 0xcb, 0xcc, 0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf,
    0xf1, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=17929 unique=86 flat=8192 nested=5472
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
    0x0005, 0x0105, 0x0205, 0x0305, 0x0405, 0x0505, 0x0605, 0x0705, 0x0805,
    0x0905, 0x0a05, 0x0b05, 0x0c05, 0x0d05, 0x0e05, 0x0f05, 0x1005, 0x1105,
    0x1205, 0x1305, 0x1405, 0x1506, 0x1606, 0x1706, 0x1806, 0x1906, 0x1a06,
    0x1b06, 0x1c06, 0x1d06, 0x1e06, 0x1f06, 0x2006, 0x2107, 0x2207, 0x2307,
    0x2407, 0x2508, 0x2608, 0x2708, 0x2808, 0x2908, 0x2a08, 0x2b08, 0x2c08,
    0x2d08, 0x2e08, 0x2f08, 0x3008, 0x3108, 0x3208, 0x3308, 0x3409, 0x3509,
    0x3609, 0x3709, 0x3809, 0x3909, 0x3a09, 0x3b09, 0x3c09, 0x3d09, 0x3e09,
    0x3f09, 0x4009, 0x4109, 0x4209, 0x4309, 0x4409, 0x4509, 0x4609, 0x0019,
    0x0029, 0x0039, 0x0049, 0x0059, 0x0069, 0x0079, 0x0089, 0x0099, 0x00a9,
    0x00b9, 0x00c9, 0x00d9, 0x00e9, 0x00f9};
inline uint16_t GetEmitOp13(size_t i) {
  return g_emit_op_13_inner[g_emit_op_13_outer[i]];
}
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {0x02, 0x03};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_14[2] = {0x01, 0x03};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=5 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {0x04, 0x05};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_15[2] = {0x01, 0x03};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_16[2] = {0x06, 0x07};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_16[2] = {0x01, 0x03};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=11 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {0x08, 0x0b};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_17[2] = {0x01, 0x03};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=14 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {0x0c, 0x0e};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_18[2] = {0x01, 0x03};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=16 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_19[2] = {0x0f, 0x10};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_19[2] = {0x01, 0x03};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=18 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_20[2] = {0x11, 0x12};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_20[2] = {0x01, 0x03};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=20 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_21[2] = {0x13, 0x14};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_21[2] = {0x01, 0x03};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_22[2] = {0x15, 0x17};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_22[2] = {0x01, 0x03};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=25 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_23[2] = {0x18, 0x19};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_23[2] = {0x01, 0x03};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_24[2] = {0x1a, 0x1b};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_24[2] = {0x01, 0x03};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=29 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_25[2] = {0x1c, 0x1d};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_25[2] = {0x01, 0x03};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=31 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_26[2] = {0x1e, 0x1f};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_26[2] = {0x01, 0x03};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=220 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_27[2] = {0x7f, 0xdc};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_27[2] = {0x01, 0x03};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=249 unique=4 flat=32 nested=64
static const uint8_t g_emit_buffer_28[4] = {0xf9, 0x0a, 0x0d, 0x16};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=27 unique=5 flat=64 nested=104
static const uint8_t g_emit_op_28[8] = {0x01, 0x01, 0x01, 0x01,
                                        0x0b, 0x13, 0x1b, 0x07};
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
      const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
      const auto op = GetEmitOp1(index);
      buffer_len_ -= op & 15;
      const auto emit_ofs = op >> 7;
      switch ((op >> 4) & 7) {
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
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp2(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer2(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp3(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer3(emit_ofs + 0));
  }
  void DecodeStep2() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp4(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer4(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
    const auto op = GetEmitOp5(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 8;
    switch ((op >> 4) & 15) {
      case 8: {
        DecodeStep10();
        break;
      }
      case 9: {
        DecodeStep11();
        break;
      }
      case 2: {
        DecodeStep4();
        break;
      }
      case 4: {
        DecodeStep5();
        break;
      }
      case 6: {
        DecodeStep6();
        break;
      }
      case 3: {
        DecodeStep7();
        break;
      }
      case 7: {
        DecodeStep8();
        break;
      }
      case 5: {
        DecodeStep9();
        break;
      }
      case 1: {
        sink_(GetEmitBuffer5(emit_ofs + 0));
        break;
      }
      case 0: {
        sink_(GetEmitBuffer5(emit_ofs + 0));
        sink_(GetEmitBuffer5(emit_ofs + 1));
        break;
      }
    }
  }
  void DecodeStep4() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp6(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer6(emit_ofs + 0));
  }
  void DecodeStep5() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp7(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer7(emit_ofs + 0));
  }
  void DecodeStep6() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp8(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer8(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp9(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer9(emit_ofs + 0));
  }
  void DecodeStep8() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetEmitOp10(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmitBuffer10(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp11(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer11(emit_ofs + 0));
  }
  void DecodeStep10() {
    if (!RefillTo5()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 5)) & 0x1f;
    const auto op = GetEmitOp12(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmitBuffer12(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
    const auto op = GetEmitOp13(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 8;
    switch ((op >> 4) & 15) {
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
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp14(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer14(emit_ofs + 0));
  }
  void DecodeStep13() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp15(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer15(emit_ofs + 0));
  }
  void DecodeStep14() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp16(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer16(emit_ofs + 0));
  }
  void DecodeStep15() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp17(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer17(emit_ofs + 0));
  }
  void DecodeStep16() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp18(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer18(emit_ofs + 0));
  }
  void DecodeStep17() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp19(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer19(emit_ofs + 0));
  }
  void DecodeStep18() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp20(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer20(emit_ofs + 0));
  }
  void DecodeStep19() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp21(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer21(emit_ofs + 0));
  }
  void DecodeStep20() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp22(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer22(emit_ofs + 0));
  }
  void DecodeStep21() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp23(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer23(emit_ofs + 0));
  }
  void DecodeStep22() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp24(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer24(emit_ofs + 0));
  }
  void DecodeStep23() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp25(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer25(emit_ofs + 0));
  }
  void DecodeStep24() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp26(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer26(emit_ofs + 0));
  }
  void DecodeStep25() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp27(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer27(emit_ofs + 0));
  }
  void DecodeStep26() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp28(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 3;
    switch ((op >> 2) & 1) {
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
    const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
    const auto op = GetEmitOp0(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 6;
    switch ((op >> 4) & 3) {
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
