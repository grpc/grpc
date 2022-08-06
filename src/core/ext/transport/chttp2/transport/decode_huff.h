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
// max=9352 unique=78 flat=8192 nested=5344
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
    0x0005, 0x0085, 0x0105, 0x0185, 0x0205, 0x0285, 0x0305, 0x0385, 0x0405,
    0x0485, 0x0506, 0x0586, 0x0606, 0x0686, 0x0706, 0x0786, 0x0806, 0x0886,
    0x0906, 0x0986, 0x0a06, 0x0a86, 0x0b06, 0x0b86, 0x0c06, 0x0c86, 0x0d06,
    0x0d86, 0x0e06, 0x0e86, 0x0f06, 0x0f86, 0x1006, 0x1086, 0x1106, 0x1186,
    0x1207, 0x1287, 0x1307, 0x1387, 0x1407, 0x1487, 0x1507, 0x1587, 0x1607,
    0x1687, 0x1707, 0x1787, 0x1807, 0x1887, 0x1907, 0x1987, 0x1a07, 0x1a87,
    0x1b07, 0x1b87, 0x1c07, 0x1c87, 0x1d07, 0x1d87, 0x1e07, 0x1e87, 0x1f07,
    0x1f87, 0x2007, 0x2087, 0x2107, 0x2187, 0x2208, 0x2288, 0x2308, 0x2388,
    0x2408, 0x2488, 0x0019, 0x0029, 0x0039, 0x0049};
inline uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=122 unique=79 flat=1432 nested=2064
static const uint8_t g_emit_buffer_1[179] = {
    0x30, 0x30, 0x30, 0x31, 0x30, 0x32, 0x30, 0x61, 0x30, 0x63, 0x30, 0x65,
    0x30, 0x69, 0x30, 0x6f, 0x30, 0x73, 0x30, 0x74, 0x31, 0x31, 0x31, 0x32,
    0x31, 0x61, 0x31, 0x63, 0x31, 0x65, 0x31, 0x69, 0x31, 0x6f, 0x31, 0x73,
    0x31, 0x74, 0x32, 0x32, 0x32, 0x61, 0x32, 0x63, 0x32, 0x65, 0x32, 0x69,
    0x32, 0x6f, 0x32, 0x73, 0x32, 0x74, 0x61, 0x61, 0x61, 0x63, 0x61, 0x65,
    0x61, 0x69, 0x61, 0x6f, 0x61, 0x73, 0x61, 0x74, 0x63, 0x63, 0x63, 0x65,
    0x63, 0x69, 0x63, 0x6f, 0x63, 0x73, 0x63, 0x74, 0x65, 0x65, 0x65, 0x69,
    0x65, 0x6f, 0x65, 0x73, 0x65, 0x74, 0x69, 0x69, 0x69, 0x6f, 0x69, 0x73,
    0x69, 0x74, 0x6f, 0x6f, 0x6f, 0x73, 0x6f, 0x74, 0x73, 0x73, 0x73, 0x74,
    0x74, 0x30, 0x20, 0x25, 0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3d, 0x41, 0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d,
    0x6e, 0x70, 0x72, 0x75, 0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x59, 0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x26, 0x2a, 0x2c, 0x3b, 0x58, 0x5a, 0x21, 0x22, 0x28, 0x29, 0x3f};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=22810 unique=182 flat=16384 nested=11104
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
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x070a, 0x080a,
    0x090a, 0x0015, 0x018a, 0x0a0a, 0x0b0a, 0x0c0a, 0x0d0a, 0x0e0a, 0x0f0a,
    0x100a, 0x110a, 0x120a, 0x0195, 0x028a, 0x0b8a, 0x130a, 0x140a, 0x150a,
    0x160a, 0x170a, 0x180a, 0x190a, 0x1a0a, 0x0295, 0x038a, 0x0c8a, 0x148a,
    0x1b0a, 0x1c0a, 0x1d0a, 0x1e0a, 0x1f0a, 0x200a, 0x210a, 0x0395, 0x048a,
    0x0d8a, 0x158a, 0x1c8a, 0x220a, 0x230a, 0x240a, 0x250a, 0x260a, 0x270a,
    0x0495, 0x058a, 0x0e8a, 0x168a, 0x1d8a, 0x238a, 0x280a, 0x290a, 0x2a0a,
    0x2b0a, 0x2c0a, 0x0595, 0x068a, 0x0f8a, 0x178a, 0x1e8a, 0x248a, 0x298a,
    0x2d0a, 0x2e0a, 0x2f0a, 0x300a, 0x0695, 0x078a, 0x108a, 0x188a, 0x1f8a,
    0x258a, 0x2a8a, 0x2e8a, 0x310a, 0x320a, 0x330a, 0x0795, 0x088a, 0x118a,
    0x198a, 0x208a, 0x268a, 0x2b8a, 0x2f8a, 0x328a, 0x340a, 0x350a, 0x0895,
    0x360a, 0x098a, 0x128a, 0x1a8a, 0x218a, 0x278a, 0x2c8a, 0x308a, 0x338a,
    0x358a, 0x0995, 0x3716, 0x3796, 0x3816, 0x3896, 0x3916, 0x3996, 0x3a16,
    0x3a96, 0x3b16, 0x3b96, 0x3c16, 0x3c96, 0x3d16, 0x3d96, 0x3e16, 0x3e96,
    0x3f16, 0x3f96, 0x4016, 0x4096, 0x4116, 0x4196, 0x4216, 0x4296, 0x4316,
    0x4396, 0x4417, 0x4497, 0x4517, 0x4597, 0x4617, 0x4697, 0x4717, 0x4797,
    0x4817, 0x4897, 0x4917, 0x4997, 0x4a17, 0x4a97, 0x4b17, 0x4b97, 0x4c17,
    0x4c97, 0x4d17, 0x4d97, 0x4e17, 0x4e97, 0x4f17, 0x4f97, 0x5017, 0x5097,
    0x5117, 0x5197, 0x5217, 0x5297, 0x5317, 0x5397, 0x5418, 0x5498, 0x5518,
    0x5598, 0x5618, 0x5698, 0x571a, 0x579a, 0x581a, 0x589a, 0x591a, 0x002a,
    0x003a, 0x004a};
inline uint16_t GetEmitOp1(size_t i) {
  return g_emit_op_1_inner[g_emit_op_1_outer[i]];
}
// max=43 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {0x27, 0x2b};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {0x01, 0x03};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=226 unique=22 flat=176 nested=352
static const uint8_t g_emit_buffer_3[22] = {
    0x00, 0x24, 0x40, 0x5b, 0x5d, 0x7e, 0x5e, 0x7d, 0x3c, 0x60, 0x7b,
    0x5c, 0xc3, 0xd0, 0x80, 0x82, 0x83, 0xa2, 0xb8, 0xc2, 0xe0, 0xe2};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=10762 unique=40 flat=16384 nested=8832
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
    0x0003, 0x0203, 0x0403, 0x0603, 0x0803, 0x0a03, 0x0c04, 0x0e04,
    0x1005, 0x1205, 0x1405, 0x1609, 0x1809, 0x1a09, 0x1c0a, 0x1e0a,
    0x200a, 0x220a, 0x240a, 0x260a, 0x280a, 0x2a0a, 0x001a, 0x002a,
    0x003a, 0x004a, 0x005a, 0x006a, 0x007a, 0x008a, 0x009a, 0x00aa,
    0x00ba, 0x00ca, 0x00da, 0x00ea, 0x00fa, 0x010a, 0x011a, 0x012a};
inline uint16_t GetEmitOp3(size_t i) {
  return g_emit_op_3_inner[g_emit_op_3_outer[i]];
}
// max=161 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {0x99, 0xa1};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_4[2] = {0x01, 0x03};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=172 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {0xa7, 0xac};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_5[2] = {0x01, 0x03};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=177 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {0xb0, 0xb1};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {0x01, 0x03};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=209 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {0xb3, 0xd1};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_7[2] = {0x01, 0x03};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=217 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {0xd8, 0xd9};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {0x01, 0x03};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=229 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {0xe3, 0xe5};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_9[2] = {0x01, 0x03};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=146 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_10[4] = {0x85, 0x86, 0x88, 0x92};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_10[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=163 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_11[4] = {0x9a, 0x9c, 0xa0, 0xa3};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_11[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=173 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_12[4] = {0xa4, 0xa9, 0xaa, 0xad};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_12[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=186 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_13[4] = {0xb2, 0xb5, 0xb9, 0xba};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_13[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=196 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_14[4] = {0xbb, 0xbd, 0xbe, 0xc4};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_14[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=233 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_15[4] = {0xc6, 0xe4, 0xe8, 0xe9};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_15[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=143 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_16[8] = {0x01, 0x87, 0x89, 0x8a,
                                            0x8b, 0x8c, 0x8d, 0x8f};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=31 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_16[8] = {0x03, 0x07, 0x0b, 0x0f,
                                        0x13, 0x17, 0x1b, 0x1f};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=158 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_17[8] = {0x93, 0x95, 0x96, 0x97,
                                            0x98, 0x9b, 0x9d, 0x9e};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=31 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_17[8] = {0x03, 0x07, 0x0b, 0x0f,
                                        0x13, 0x17, 0x1b, 0x1f};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=183 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_18[8] = {0xa5, 0xa6, 0xa8, 0xae,
                                            0xaf, 0xb4, 0xb6, 0xb7};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=31 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_18[8] = {0x03, 0x07, 0x0b, 0x0f,
                                        0x13, 0x17, 0x1b, 0x1f};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=230 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_19[3] = {0xe6, 0x81, 0x84};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=10 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_19[4] = {0x01, 0x01, 0x06, 0x0a};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=239 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_20[11] = {0xbc, 0xbf, 0xc5, 0xe7, 0xef, 0x09,
                                             0x8e, 0x90, 0x91, 0x94, 0x9f};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=84 unique=11 flat=128 nested=216
// monotonic increasing
static const uint8_t g_emit_op_20[16] = {0x03, 0x03, 0x0b, 0x0b, 0x13, 0x13,
                                         0x1b, 0x1b, 0x23, 0x23, 0x2c, 0x34,
                                         0x3c, 0x44, 0x4c, 0x54};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=255 unique=76 flat=608 nested=1216
static const uint8_t g_emit_buffer_21[76] = {
    0xab, 0xce, 0xd7, 0xe1, 0xec, 0xed, 0xc7, 0xcf, 0xea, 0xeb, 0xc0,
    0xc1, 0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda, 0xdb, 0xee, 0xf0,
    0xf2, 0xf3, 0xff, 0xcb, 0xcc, 0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf,
    0xf1, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=2410 unique=77 flat=16384 nested=9424
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
    0x0004, 0x0024, 0x0044, 0x0064, 0x0084, 0x00a4, 0x00c5, 0x00e5, 0x0105,
    0x0125, 0x0146, 0x0166, 0x0186, 0x01a6, 0x01c6, 0x01e6, 0x0206, 0x0226,
    0x0246, 0x0266, 0x0286, 0x02a6, 0x02c6, 0x02e6, 0x0306, 0x0327, 0x0347,
    0x0367, 0x0387, 0x03a7, 0x03c7, 0x03e7, 0x0407, 0x0427, 0x0447, 0x0467,
    0x0487, 0x04a7, 0x04c7, 0x04e7, 0x0507, 0x0527, 0x0547, 0x0567, 0x0588,
    0x05a8, 0x05c8, 0x05e8, 0x0608, 0x0628, 0x0648, 0x0668, 0x0688, 0x06a8,
    0x06c8, 0x06e8, 0x0708, 0x0728, 0x0748, 0x0768, 0x0788, 0x07a8, 0x07c8,
    0x07e8, 0x0808, 0x0828, 0x0848, 0x0868, 0x0888, 0x08a8, 0x08c8, 0x08e8,
    0x0908, 0x092a, 0x094a, 0x096a, 0x001a};
inline uint16_t GetEmitOp21(size_t i) {
  return g_emit_op_21_inner[g_emit_op_21_outer[i]];
}
// max=124 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_22[3] = {0x7c, 0x23, 0x3e};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=10 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_22[4] = {0x01, 0x01, 0x06, 0x0a};
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
      const auto index = (buffer_ >> (buffer_len_ - 10)) & 0x3ff;
      const auto op = GetEmitOp1(index);
      buffer_len_ -= op & 15;
      const auto emit_ofs = op >> 7;
      switch ((op >> 4) & 7) {
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
    if (!RefillTo10()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 10)) & 0x3ff;
    const auto op = GetEmitOp3(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 9;
    switch ((op >> 4) & 31) {
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
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp4(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer4(emit_ofs + 0));
  }
  void DecodeStep3() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp5(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer5(emit_ofs + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp7(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer7(emit_ofs + 0));
  }
  void DecodeStep6() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp8(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer8(emit_ofs + 0));
  }
  void DecodeStep7() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp9(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer9(emit_ofs + 0));
  }
  void DecodeStep8() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp10(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer10(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp11(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer11(emit_ofs + 0));
  }
  void DecodeStep10() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp12(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer12(emit_ofs + 0));
  }
  void DecodeStep11() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp13(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer13(emit_ofs + 0));
  }
  void DecodeStep12() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp14(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer14(emit_ofs + 0));
  }
  void DecodeStep13() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp15(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer15(emit_ofs + 0));
  }
  void DecodeStep14() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp16(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer16(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp17(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer17(emit_ofs + 0));
  }
  void DecodeStep16() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp18(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer18(emit_ofs + 0));
  }
  void DecodeStep17() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp19(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer19(emit_ofs + 0));
  }
  void DecodeStep18() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetEmitOp20(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmitBuffer20(emit_ofs + 0));
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
    const auto index = (buffer_ >> (buffer_len_ - 10)) & 0x3ff;
    const auto op = GetEmitOp21(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 5;
    switch ((op >> 4) & 1) {
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
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp22(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer22(emit_ofs + 0));
  }
  void Done() {
    if (buffer_len_ < 9) {
      buffer_ = (buffer_ << (9 - buffer_len_)) |
                ((uint64_t(1) << (9 - buffer_len_)) - 1);
      buffer_len_ = 9;
    }
    const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
    const auto op = GetEmitOp0(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 7;
    switch ((op >> 4) & 7) {
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
