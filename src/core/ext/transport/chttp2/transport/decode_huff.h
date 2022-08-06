#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=122 unique=68 flat=544 nested=1088
static const uint8_t g_emit_buffer_0[68] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75,
    0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59,
    0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78, 0x79, 0x7a};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=4295 unique=72 flat=2048 nested=2176
static const uint16_t g_emit_op_0[128] = {
    0x0005, 0x0005, 0x0005, 0x0005, 0x0045, 0x0045, 0x0045, 0x0045, 0x0085,
    0x0085, 0x0085, 0x0085, 0x00c5, 0x00c5, 0x00c5, 0x00c5, 0x0105, 0x0105,
    0x0105, 0x0105, 0x0145, 0x0145, 0x0145, 0x0145, 0x0185, 0x0185, 0x0185,
    0x0185, 0x01c5, 0x01c5, 0x01c5, 0x01c5, 0x0205, 0x0205, 0x0205, 0x0205,
    0x0245, 0x0245, 0x0245, 0x0245, 0x0286, 0x0286, 0x02c6, 0x02c6, 0x0306,
    0x0306, 0x0346, 0x0346, 0x0386, 0x0386, 0x03c6, 0x03c6, 0x0406, 0x0406,
    0x0446, 0x0446, 0x0486, 0x0486, 0x04c6, 0x04c6, 0x0506, 0x0506, 0x0546,
    0x0546, 0x0586, 0x0586, 0x05c6, 0x05c6, 0x0606, 0x0606, 0x0646, 0x0646,
    0x0686, 0x0686, 0x06c6, 0x06c6, 0x0706, 0x0706, 0x0746, 0x0746, 0x0786,
    0x0786, 0x07c6, 0x07c6, 0x0806, 0x0806, 0x0846, 0x0846, 0x0886, 0x0886,
    0x08c6, 0x08c6, 0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87,
    0x0ac7, 0x0b07, 0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7,
    0x0d07, 0x0d47, 0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07,
    0x0f47, 0x0f87, 0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x000f, 0x0017,
    0x001f, 0x0027};
inline uint16_t GetEmitOp0(size_t i) { return g_emit_op_0[i]; }
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
// max=4680 unique=76 flat=4096 nested=3264
// monotonic increasing
static const uint8_t g_emit_op_1_outer[256] = {
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
static const uint16_t g_emit_op_1_inner[76] = {
    0x0005, 0x0045, 0x0085, 0x00c5, 0x0105, 0x0145, 0x0185, 0x01c5, 0x0205,
    0x0245, 0x0286, 0x02c6, 0x0306, 0x0346, 0x0386, 0x03c6, 0x0406, 0x0446,
    0x0486, 0x04c6, 0x0506, 0x0546, 0x0586, 0x05c6, 0x0606, 0x0646, 0x0686,
    0x06c6, 0x0706, 0x0746, 0x0786, 0x07c6, 0x0806, 0x0846, 0x0886, 0x08c6,
    0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87, 0x0ac7, 0x0b07,
    0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7, 0x0d07, 0x0d47,
    0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07, 0x0f47, 0x0f87,
    0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x1108, 0x1148, 0x1188, 0x11c8,
    0x1208, 0x1248, 0x0018, 0x0028};
inline uint16_t GetEmitOp1(size_t i) {
  return g_emit_op_1_inner[g_emit_op_1_outer[i]];
}
// max=41 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_2[4] = {0x21, 0x22, 0x28, 0x29};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_2[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=126 unique=53 flat=1160 nested=1584
static const uint8_t g_emit_buffer_3[145] = {
    0x3f, 0x30, 0x3f, 0x31, 0x3f, 0x32, 0x3f, 0x61, 0x3f, 0x63, 0x3f, 0x65,
    0x3f, 0x69, 0x3f, 0x6f, 0x3f, 0x73, 0x3f, 0x74, 0x3f, 0x20, 0x3f, 0x25,
    0x3f, 0x2d, 0x3f, 0x2e, 0x3f, 0x2f, 0x3f, 0x33, 0x3f, 0x34, 0x3f, 0x35,
    0x3f, 0x36, 0x3f, 0x37, 0x3f, 0x38, 0x3f, 0x39, 0x3f, 0x3d, 0x3f, 0x41,
    0x3f, 0x5f, 0x3f, 0x62, 0x3f, 0x64, 0x3f, 0x66, 0x3f, 0x67, 0x3f, 0x68,
    0x3f, 0x6c, 0x3f, 0x6d, 0x3f, 0x6e, 0x3f, 0x70, 0x3f, 0x72, 0x3f, 0x75,
    0x27, 0x30, 0x27, 0x31, 0x27, 0x32, 0x27, 0x61, 0x27, 0x63, 0x27, 0x65,
    0x27, 0x69, 0x27, 0x6f, 0x27, 0x73, 0x27, 0x74, 0x2b, 0x30, 0x2b, 0x31,
    0x2b, 0x32, 0x2b, 0x61, 0x2b, 0x63, 0x2b, 0x65, 0x2b, 0x69, 0x2b, 0x6f,
    0x2b, 0x73, 0x2b, 0x74, 0x7c, 0x30, 0x7c, 0x31, 0x7c, 0x32, 0x7c, 0x61,
    0x7c, 0x63, 0x7c, 0x65, 0x7c, 0x69, 0x7c, 0x6f, 0x7c, 0x73, 0x7c, 0x74,
    0x23, 0x3e, 0x00, 0x24, 0x40, 0x5b, 0x5d, 0x7e, 0x5e, 0x7d, 0x3c, 0x60,
    0x7b};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=9239 unique=85 flat=4096 nested=3408
// monotonic increasing
static const uint8_t g_emit_op_3_outer[256] = {
    0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,
    9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 47,
    47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 58, 58, 58, 58, 58, 58, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69,
    69, 69, 69, 69, 69, 69, 69, 69, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71,
    71, 71, 72, 72, 72, 72, 72, 72, 72, 72, 73, 73, 73, 73, 73, 73, 73, 73, 74,
    74, 74, 74, 74, 74, 74, 74, 75, 75, 75, 75, 75, 75, 75, 75, 76, 76, 76, 76,
    76, 76, 76, 76, 77, 77, 77, 77, 77, 77, 77, 77, 78, 78, 78, 78, 79, 79, 79,
    79, 80, 80, 81, 81, 82, 82, 83, 84};
static const uint16_t g_emit_op_3_inner[85] = {
    0x0007, 0x0087, 0x0107, 0x0187, 0x0207, 0x0287, 0x0307, 0x0387, 0x0407,
    0x0487, 0x0508, 0x0588, 0x0608, 0x0688, 0x0708, 0x0788, 0x0808, 0x0888,
    0x0908, 0x0988, 0x0a08, 0x0a88, 0x0b08, 0x0b88, 0x0c08, 0x0c88, 0x0d08,
    0x0d88, 0x0e08, 0x0e88, 0x0f08, 0x0f88, 0x1008, 0x1088, 0x1108, 0x1188,
    0x0012, 0x1208, 0x1288, 0x1308, 0x1388, 0x1408, 0x1488, 0x1508, 0x1588,
    0x1608, 0x1688, 0x1213, 0x1708, 0x1788, 0x1808, 0x1888, 0x1908, 0x1988,
    0x1a08, 0x1a88, 0x1b08, 0x1b88, 0x1713, 0x1c08, 0x1c88, 0x1d08, 0x1d88,
    0x1e08, 0x1e88, 0x1f08, 0x1f88, 0x2008, 0x2088, 0x1c13, 0x2114, 0x2154,
    0x2195, 0x21d5, 0x2215, 0x2255, 0x2295, 0x22d5, 0x2316, 0x2356, 0x2397,
    0x23d7, 0x2417, 0x0028, 0x0038};
inline uint16_t GetEmitOp3(size_t i) {
  return g_emit_op_3_inner[g_emit_op_3_outer[i]];
}
// max=226 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_4[15] = {0x5c, 0xc3, 0xd0, 0x80, 0x82,
                                            0x83, 0xa2, 0xb8, 0xc2, 0xe0,
                                            0xe2, 0x99, 0xa1, 0xa7, 0xac};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=117 unique=15 flat=256 nested=376
// monotonic increasing
static const uint8_t g_emit_op_4[32] = {
    0x03, 0x03, 0x03, 0x03, 0x0b, 0x0b, 0x0b, 0x0b, 0x13, 0x13, 0x13,
    0x13, 0x1c, 0x1c, 0x24, 0x24, 0x2c, 0x2c, 0x34, 0x34, 0x3c, 0x3c,
    0x44, 0x44, 0x4c, 0x4c, 0x54, 0x54, 0x5d, 0x65, 0x6d, 0x75};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=239 unique=76 flat=608 nested=1216
static const uint8_t g_emit_buffer_5[76] = {
    0xb0, 0xb1, 0xb3, 0xd1, 0xd8, 0xd9, 0xe3, 0xe5, 0xe6, 0x81, 0x84,
    0x85, 0x86, 0x88, 0x92, 0x9a, 0x9c, 0xa0, 0xa3, 0xa4, 0xa9, 0xaa,
    0xad, 0xb2, 0xb5, 0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4,
    0xe8, 0xe9, 0x01, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8f, 0x93,
    0x95, 0x96, 0x97, 0x98, 0x9b, 0x9d, 0x9e, 0xa5, 0xa6, 0xa8, 0xae,
    0xaf, 0xb4, 0xb6, 0xb7, 0xbc, 0xbf, 0xc5, 0xe7, 0xef, 0x09, 0x8e,
    0x90, 0x91, 0x94, 0x9f, 0xab, 0xce, 0xd7, 0xe1, 0xec, 0xed};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=19208 unique=86 flat=4096 nested=3424
// monotonic increasing
static const uint8_t g_emit_op_5_outer[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
    2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,
    4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  7,
    7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,
    10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14,
    14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19,
    19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24,
    24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28,
    29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33,
    33, 34, 34, 34, 34, 35, 35, 36, 36, 37, 37, 38, 38, 39, 39, 40, 40, 41, 41,
    42, 42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47, 48, 48, 49, 49, 50, 50, 51,
    51, 52, 52, 53, 53, 54, 54, 55, 55, 56, 56, 57, 57, 58, 58, 59, 59, 60, 60,
    61, 61, 62, 62, 63, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
    77, 78, 79, 80, 81, 82, 83, 84, 85};
static const uint16_t g_emit_op_5_inner[86] = {
    0x0005, 0x0105, 0x0205, 0x0305, 0x0405, 0x0505, 0x0605, 0x0705, 0x0805,
    0x0906, 0x0a06, 0x0b06, 0x0c06, 0x0d06, 0x0e06, 0x0f06, 0x1006, 0x1106,
    0x1206, 0x1306, 0x1406, 0x1506, 0x1606, 0x1706, 0x1806, 0x1906, 0x1a06,
    0x1b06, 0x1c06, 0x1d06, 0x1e06, 0x1f06, 0x2006, 0x2106, 0x2206, 0x2307,
    0x2407, 0x2507, 0x2607, 0x2707, 0x2807, 0x2907, 0x2a07, 0x2b07, 0x2c07,
    0x2d07, 0x2e07, 0x2f07, 0x3007, 0x3107, 0x3207, 0x3307, 0x3407, 0x3507,
    0x3607, 0x3707, 0x3807, 0x3907, 0x3a07, 0x3b07, 0x3c07, 0x3d07, 0x3e07,
    0x3f07, 0x4008, 0x4108, 0x4208, 0x4308, 0x4408, 0x4508, 0x4608, 0x4708,
    0x4808, 0x4908, 0x4a08, 0x4b08, 0x0018, 0x0028, 0x0038, 0x0048, 0x0058,
    0x0068, 0x0078, 0x0088, 0x0098, 0x00a8};
inline uint16_t GetEmitOp5(size_t i) {
  return g_emit_op_5_inner[g_emit_op_5_outer[i]];
}
// max=207 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {0xc7, 0xcf};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {0x01, 0x03};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=235 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {0xea, 0xeb};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_7[2] = {0x01, 0x03};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=201 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_8[4] = {0xc0, 0xc1, 0xc8, 0xc9};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_8[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=213 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_9[4] = {0xca, 0xcd, 0xd2, 0xd5};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_9[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=240 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_10[4] = {0xda, 0xdb, 0xee, 0xf0};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_10[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=244 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_11[8] = {0xd3, 0xd4, 0xd6, 0xdd,
                                            0xde, 0xdf, 0xf1, 0xf4};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=31 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_11[8] = {0x03, 0x07, 0x0b, 0x0f,
                                        0x13, 0x17, 0x1b, 0x1f};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=253 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_12[8] = {0xf5, 0xf6, 0xf7, 0xf8,
                                            0xfa, 0xfb, 0xfc, 0xfd};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=31 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_12[8] = {0x03, 0x07, 0x0b, 0x0f,
                                        0x13, 0x17, 0x1b, 0x1f};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=254 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_13[15] = {0xfe, 0x02, 0x03, 0x04, 0x05,
                                             0x06, 0x07, 0x08, 0x0b, 0x0c,
                                             0x0e, 0x0f, 0x10, 0x11, 0x12};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=116 unique=15 flat=128 nested=248
// monotonic increasing
static const uint8_t g_emit_op_13[16] = {0x03, 0x03, 0x0c, 0x14, 0x1c, 0x24,
                                         0x2c, 0x34, 0x3c, 0x44, 0x4c, 0x54,
                                         0x5c, 0x64, 0x6c, 0x74};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=255 unique=5 flat=40 nested=80
static const uint8_t g_emit_buffer_14[5] = {0xf2, 0xf3, 0xff, 0xcb, 0xcc};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=19 unique=5 flat=64 nested=104
// monotonic increasing
static const uint8_t g_emit_op_14[8] = {0x02, 0x02, 0x06, 0x06,
                                        0x0a, 0x0a, 0x0f, 0x13};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=249 unique=18 flat=144 nested=288
static const uint8_t g_emit_buffer_15[18] = {
    0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
    0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=278 unique=19 flat=1024 nested=816
// monotonic increasing
static const uint8_t g_emit_op_15_outer[64] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
    4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,
    8,  8,  8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11,
    12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 16, 17, 18};
static const uint16_t g_emit_op_15_inner[19] = {
    0x0004, 0x0014, 0x0024, 0x0034, 0x0044, 0x0054, 0x0064,
    0x0074, 0x0084, 0x0094, 0x00a4, 0x00b4, 0x00c4, 0x00d4,
    0x00e4, 0x00f6, 0x0106, 0x0116, 0x000e};
inline uint16_t GetEmitOp15(size_t i) {
  return g_emit_op_15_inner[g_emit_op_15_outer[i]];
}
template <typename F>
class HuffDecoder {
 public:
  HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end)
      : sink_(sink), begin_(begin), end_(end) {}
  bool Run() {
    while (ok_) {
      if (!RefillTo8()) {
        Done();
        return ok_;
      }
      const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
      const auto op = GetEmitOp1(index);
      buffer_len_ -= op & 15;
      const auto emit_ofs = op >> 6;
      switch ((op >> 4) & 3) {
        case 1: {
          DecodeStep0();
          break;
        }
        case 2: {
          DecodeStep1();
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
  bool RefillTo8() {
    switch (buffer_len_) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7: {
        return Read1();
      }
    }
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp2(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer2(emit_ofs + 0));
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
  void DecodeStep1() {
    if (!RefillTo8()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
    const auto op = GetEmitOp3(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 6;
    switch ((op >> 4) & 3) {
      case 2: {
        DecodeStep2();
        break;
      }
      case 3: {
        DecodeStep3();
        break;
      }
      case 1: {
        sink_(GetEmitBuffer3(emit_ofs + 0));
        break;
      }
      case 0: {
        sink_(GetEmitBuffer3(emit_ofs + 0));
        sink_(GetEmitBuffer3(emit_ofs + 1));
        break;
      }
    }
  }
  void DecodeStep2() {
    if (!RefillTo5()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 5)) & 0x1f;
    const auto op = GetEmitOp4(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmitBuffer4(emit_ofs + 0));
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
  void DecodeStep3() {
    if (!RefillTo8()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
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
      case 6: {
        DecodeStep12();
        break;
      }
      case 10: {
        DecodeStep13();
        break;
      }
      case 1: {
        DecodeStep4();
        break;
      }
      case 2: {
        DecodeStep5();
        break;
      }
      case 3: {
        DecodeStep6();
        break;
      }
      case 4: {
        DecodeStep7();
        break;
      }
      case 5: {
        DecodeStep8();
        break;
      }
      case 7: {
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
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp6(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer6(emit_ofs + 0));
  }
  bool RefillTo1() {
    switch (buffer_len_) {
      case 0: {
        return Read1();
      }
    }
    return true;
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp8(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer8(emit_ofs + 0));
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
  void DecodeStep10() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp12(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer12(emit_ofs + 0));
  }
  void DecodeStep11() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetEmitOp13(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmitBuffer13(emit_ofs + 0));
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
  void DecodeStep12() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp14(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer14(emit_ofs + 0));
  }
  void DecodeStep13() {
    if (!RefillTo6()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 6)) & 0x3f;
    const auto op = GetEmitOp15(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 4;
    switch ((op >> 3) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmitBuffer15(emit_ofs + 0));
        break;
      }
    }
  }
  bool RefillTo6() {
    switch (buffer_len_) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5: {
        return Read1();
      }
    }
    return true;
  }
  void Done() {
    if (buffer_len_ < 7) {
      buffer_ = (buffer_ << (7 - buffer_len_)) |
                ((uint64_t(1) << (7 - buffer_len_)) - 1);
      buffer_len_ = 7;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetEmitOp0(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 6;
    switch ((op >> 3) & 7) {
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
