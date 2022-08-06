#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=117 unique=36 flat=288 nested=576
static const uint8_t g_emit_buffer_0[36] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=8966 unique=54 flat=1024 nested=1376
static const uint16_t g_emit_op_0[64] = {
    0x0005, 0x0005, 0x0105, 0x0105, 0x0205, 0x0205, 0x0305, 0x0305,
    0x0405, 0x0405, 0x0505, 0x0505, 0x0605, 0x0605, 0x0705, 0x0705,
    0x0805, 0x0805, 0x0905, 0x0905, 0x0a06, 0x0b06, 0x0c06, 0x0d06,
    0x0e06, 0x0f06, 0x1006, 0x1106, 0x1206, 0x1306, 0x1406, 0x1506,
    0x1606, 0x1706, 0x1806, 0x1906, 0x1a06, 0x1b06, 0x1c06, 0x1d06,
    0x1e06, 0x1f06, 0x2006, 0x2106, 0x2206, 0x2306, 0x000e, 0x0016,
    0x001e, 0x0026, 0x002e, 0x0036, 0x003e, 0x0046, 0x004e, 0x0056,
    0x005e, 0x0066, 0x006e, 0x0076, 0x007e, 0x0086, 0x008e, 0x0096};
inline uint16_t GetEmitOp0(size_t i) { return g_emit_op_0[i]; }
// max=122 unique=68 flat=544 nested=1088
static const uint8_t g_emit_buffer_1[68] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75,
    0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59,
    0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78, 0x79, 0x7a};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=4295 unique=72 flat=2048 nested=2176
static const uint16_t g_emit_op_1[128] = {
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
inline uint16_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=42 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {0x26, 0x2a};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {0x01, 0x03};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=59 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {0x2c, 0x3b};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {0x01, 0x03};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=90 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {0x58, 0x5a};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_4[2] = {0x01, 0x03};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=126 unique=18 flat=144 nested=288
static const uint8_t g_emit_buffer_5[18] = {0x21, 0x22, 0x28, 0x29, 0x3f, 0x27,
                                            0x2b, 0x7c, 0x23, 0x3e, 0x00, 0x24,
                                            0x40, 0x5b, 0x5d, 0x7e, 0x5e, 0x7d};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=551 unique=20 flat=2048 nested=1344
// monotonic increasing
static const uint8_t g_emit_op_5_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 1, 1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2, 2, 2, 2, 2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3, 3, 3, 3, 3,  3,
    3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4, 4, 4, 4, 4,  4,
    4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6, 6, 6, 6, 6,  6,
    6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  9, 9, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 17, 18, 19};
static const uint16_t g_emit_op_5_inner[20] = {
    0x0003, 0x0023, 0x0043, 0x0063, 0x0083, 0x00a4, 0x00c4,
    0x00e4, 0x0105, 0x0125, 0x0146, 0x0166, 0x0186, 0x01a6,
    0x01c6, 0x01e6, 0x0207, 0x0227, 0x000f, 0x0017};
inline uint16_t GetEmitOp5(size_t i) {
  return g_emit_op_5_inner[g_emit_op_5_outer[i]];
}
// max=96 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {0x3c, 0x60};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {0x01, 0x03};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=230 unique=61 flat=768 nested=1256
static const uint8_t g_emit_buffer_7[96] = {
    0x7b, 0x30, 0x7b, 0x31, 0x7b, 0x32, 0x7b, 0x61, 0x7b, 0x63, 0x7b, 0x65,
    0x7b, 0x69, 0x7b, 0x6f, 0x7b, 0x73, 0x7b, 0x74, 0x7b, 0x20, 0x7b, 0x25,
    0x7b, 0x2d, 0x7b, 0x2e, 0x7b, 0x2f, 0x7b, 0x33, 0x7b, 0x34, 0x7b, 0x35,
    0x7b, 0x36, 0x7b, 0x37, 0x7b, 0x38, 0x7b, 0x39, 0x7b, 0x3d, 0x7b, 0x41,
    0x7b, 0x5f, 0x7b, 0x62, 0x7b, 0x64, 0x7b, 0x66, 0x7b, 0x67, 0x7b, 0x68,
    0x7b, 0x6c, 0x7b, 0x6d, 0x7b, 0x6e, 0x7b, 0x70, 0x7b, 0x72, 0x7b, 0x75,
    0x5c, 0xc3, 0xd0, 0x80, 0x82, 0x83, 0xa2, 0xb8, 0xc2, 0xe0, 0xe2, 0x99,
    0xa1, 0xa7, 0xac, 0xb0, 0xb1, 0xb3, 0xd1, 0xd8, 0xd9, 0xe3, 0xe5, 0xe6};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=24335 unique=84 flat=2048 nested=2368
static const uint16_t g_emit_op_7[128] = {
    0x0006, 0x0006, 0x0206, 0x0206, 0x0406, 0x0406, 0x0606, 0x0606, 0x0806,
    0x0806, 0x0a06, 0x0a06, 0x0c06, 0x0c06, 0x0e06, 0x0e06, 0x1006, 0x1006,
    0x1206, 0x1206, 0x1407, 0x1607, 0x1807, 0x1a07, 0x1c07, 0x1e07, 0x2007,
    0x2207, 0x2407, 0x2607, 0x2807, 0x2a07, 0x2c07, 0x2e07, 0x3007, 0x3207,
    0x3407, 0x3607, 0x3807, 0x3a07, 0x3c07, 0x3e07, 0x4007, 0x4207, 0x4407,
    0x4607, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x480d, 0x480d, 0x480d, 0x480d, 0x490d, 0x490d, 0x490d, 0x490d,
    0x4a0d, 0x4a0d, 0x4a0d, 0x4a0d, 0x4b0e, 0x4b0e, 0x4c0e, 0x4c0e, 0x4d0e,
    0x4d0e, 0x4e0e, 0x4e0e, 0x4f0e, 0x4f0e, 0x500e, 0x500e, 0x510e, 0x510e,
    0x520e, 0x520e, 0x530f, 0x540f, 0x550f, 0x560f, 0x570f, 0x580f, 0x590f,
    0x5a0f, 0x5b0f, 0x5c0f, 0x5d0f, 0x5e0f, 0x5f0f, 0x0017, 0x001f, 0x0027,
    0x002f, 0x0037, 0x003f, 0x0047, 0x004f, 0x0057, 0x005f, 0x0067, 0x006f,
    0x0077, 0x007f, 0x0087, 0x008f, 0x0097, 0x009f, 0x00a7, 0x00af, 0x00b7,
    0x00bf, 0x00c7};
inline uint16_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=132 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {0x81, 0x84};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {0x01, 0x03};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=134 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {0x85, 0x86};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_9[2] = {0x01, 0x03};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=146 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_10[2] = {0x88, 0x92};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_10[2] = {0x01, 0x03};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=156 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_11[2] = {0x9a, 0x9c};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_11[2] = {0x01, 0x03};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=163 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_12[2] = {0xa0, 0xa3};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_12[2] = {0x01, 0x03};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=169 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_13[2] = {0xa4, 0xa9};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_13[2] = {0x01, 0x03};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=173 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {0xaa, 0xad};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_14[2] = {0x01, 0x03};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=181 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {0xb2, 0xb5};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_15[2] = {0x01, 0x03};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=186 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_16[2] = {0xb9, 0xba};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_16[2] = {0x01, 0x03};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=189 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {0xbb, 0xbd};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_17[2] = {0x01, 0x03};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=196 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {0xbe, 0xc4};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_18[2] = {0x01, 0x03};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=228 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_19[2] = {0xc6, 0xe4};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_19[2] = {0x01, 0x03};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=233 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_20[2] = {0xe8, 0xe9};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=3 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_20[2] = {0x01, 0x03};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=138 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_21[4] = {0x01, 0x87, 0x89, 0x8a};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_21[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=143 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_22[4] = {0x8b, 0x8c, 0x8d, 0x8f};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_22[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=151 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_23[4] = {0x93, 0x95, 0x96, 0x97};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_23[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=158 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_24[4] = {0x98, 0x9b, 0x9d, 0x9e};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_24[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=174 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_25[4] = {0xa5, 0xa6, 0xa8, 0xae};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_25[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=183 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_26[4] = {0xaf, 0xb4, 0xb6, 0xb7};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_26[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=231 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_27[4] = {0xbc, 0xbf, 0xc5, 0xe7};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=14 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_27[4] = {0x02, 0x06, 0x0a, 0x0e};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=237 unique=10 flat=80 nested=160
static const uint8_t g_emit_buffer_28[10] = {0xab, 0xce, 0xd7, 0xe1, 0xec,
                                             0xed, 0xc7, 0xcf, 0xea, 0xeb};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=76 unique=10 flat=128 nested=208
// monotonic increasing
static const uint8_t g_emit_op_28[16] = {0x03, 0x03, 0x0b, 0x0b, 0x13, 0x13,
                                         0x1b, 0x1b, 0x23, 0x23, 0x2b, 0x2b,
                                         0x34, 0x3c, 0x44, 0x4c};
inline uint8_t GetEmitOp28(size_t i) { return g_emit_op_28[i]; }
// max=239 unique=7 flat=56 nested=112
static const uint8_t g_emit_buffer_29[7] = {0xef, 0x09, 0x8e, 0x90,
                                            0x91, 0x94, 0x9f};
inline uint8_t GetEmitBuffer29(size_t i) { return g_emit_buffer_29[i]; }
// max=27 unique=7 flat=64 nested=120
// monotonic increasing
static const uint8_t g_emit_op_29[8] = {0x02, 0x02, 0x07, 0x0b,
                                        0x0f, 0x13, 0x17, 0x1b};
inline uint8_t GetEmitOp29(size_t i) { return g_emit_op_29[i]; }
// max=255 unique=63 flat=504 nested=1008
static const uint8_t g_emit_buffer_30[63] = {
    0xc0, 0xc1, 0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda, 0xdb, 0xee,
    0xf0, 0xf2, 0xf3, 0xff, 0xcb, 0xcc, 0xd3, 0xd4, 0xd6, 0xdd, 0xde,
    0xdf, 0xf1, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd,
    0xfe, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a,
    0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9};
inline uint8_t GetEmitBuffer30(size_t i) { return g_emit_buffer_30[i]; }
// max=999 unique=64 flat=2048 nested=2048
// monotonic increasing
static const uint8_t g_emit_op_30_outer[128] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,
    4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,
    9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14,
    14, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
    23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32,
    32, 33, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
static const uint16_t g_emit_op_30_inner[64] = {
    0x0005, 0x0015, 0x0025, 0x0035, 0x0045, 0x0055, 0x0065, 0x0075,
    0x0085, 0x0095, 0x00a5, 0x00b5, 0x00c5, 0x00d5, 0x00e5, 0x00f6,
    0x0106, 0x0116, 0x0126, 0x0136, 0x0146, 0x0156, 0x0166, 0x0176,
    0x0186, 0x0196, 0x01a6, 0x01b6, 0x01c6, 0x01d6, 0x01e6, 0x01f6,
    0x0206, 0x0216, 0x0227, 0x0237, 0x0247, 0x0257, 0x0267, 0x0277,
    0x0287, 0x0297, 0x02a7, 0x02b7, 0x02c7, 0x02d7, 0x02e7, 0x02f7,
    0x0307, 0x0317, 0x0327, 0x0337, 0x0347, 0x0357, 0x0367, 0x0377,
    0x0387, 0x0397, 0x03a7, 0x03b7, 0x03c7, 0x03d7, 0x03e7, 0x000f};
inline uint16_t GetEmitOp30(size_t i) {
  return g_emit_op_30_inner[g_emit_op_30_outer[i]];
}
// max=22 unique=3 flat=24 nested=48
// monotonic increasing
static const uint8_t g_emit_buffer_31[3] = {0x0a, 0x0d, 0x16};
inline uint8_t GetEmitBuffer31(size_t i) { return g_emit_buffer_31[i]; }
// max=18 unique=4 flat=32 nested=64
static const uint8_t g_emit_op_31[4] = {0x02, 0x0a, 0x12, 0x06};
inline uint8_t GetEmitOp31(size_t i) { return g_emit_op_31[i]; }
template <typename F>
class HuffDecoder {
 public:
  HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end)
      : sink_(sink), begin_(begin), end_(end) {}
  bool Run() {
    while (ok_) {
      if (!RefillTo7()) {
        Done();
        return ok_;
      }
      const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
      const auto op = GetEmitOp1(index);
      buffer_len_ -= op & 7;
      const auto emit_ofs = op >> 6;
      switch ((op >> 3) & 7) {
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
  bool RefillTo7() {
    switch (buffer_len_) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6: {
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
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetEmitOp5(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 5;
    switch ((op >> 3) & 3) {
      case 1: {
        DecodeStep4();
        break;
      }
      case 2: {
        DecodeStep5();
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
  void DecodeStep5() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetEmitOp7(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 8;
    switch ((op >> 3) & 31) {
      case 6: {
        DecodeStep10();
        break;
      }
      case 7: {
        DecodeStep11();
        break;
      }
      case 8: {
        DecodeStep12();
        break;
      }
      case 9: {
        DecodeStep13();
        break;
      }
      case 10: {
        DecodeStep14();
        break;
      }
      case 11: {
        DecodeStep15();
        break;
      }
      case 12: {
        DecodeStep16();
        break;
      }
      case 13: {
        DecodeStep17();
        break;
      }
      case 14: {
        DecodeStep18();
        break;
      }
      case 15: {
        DecodeStep19();
        break;
      }
      case 16: {
        DecodeStep20();
        break;
      }
      case 17: {
        DecodeStep21();
        break;
      }
      case 18: {
        DecodeStep22();
        break;
      }
      case 19: {
        DecodeStep23();
        break;
      }
      case 20: {
        DecodeStep24();
        break;
      }
      case 21: {
        DecodeStep25();
        break;
      }
      case 23: {
        DecodeStep26();
        break;
      }
      case 22: {
        DecodeStep27();
        break;
      }
      case 24: {
        DecodeStep28();
        break;
      }
      case 2: {
        DecodeStep6();
        break;
      }
      case 3: {
        DecodeStep7();
        break;
      }
      case 4: {
        DecodeStep8();
        break;
      }
      case 5: {
        DecodeStep9();
        break;
      }
      case 1: {
        sink_(GetEmitBuffer7(emit_ofs + 0));
        break;
      }
      case 0: {
        sink_(GetEmitBuffer7(emit_ofs + 0));
        sink_(GetEmitBuffer7(emit_ofs + 1));
        break;
      }
    }
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp10(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer10(emit_ofs + 0));
  }
  void DecodeStep9() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp11(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer11(emit_ofs + 0));
  }
  void DecodeStep10() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp12(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer12(emit_ofs + 0));
  }
  void DecodeStep11() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetEmitOp13(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmitBuffer13(emit_ofs + 0));
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp21(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer21(emit_ofs + 0));
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
  void DecodeStep21() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp23(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer23(emit_ofs + 0));
  }
  void DecodeStep22() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp24(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer24(emit_ofs + 0));
  }
  void DecodeStep23() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp25(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer25(emit_ofs + 0));
  }
  void DecodeStep24() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp26(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer26(emit_ofs + 0));
  }
  void DecodeStep25() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp27(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer27(emit_ofs + 0));
  }
  void DecodeStep26() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetEmitOp28(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmitBuffer28(emit_ofs + 0));
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
  void DecodeStep27() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetEmitOp29(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmitBuffer29(emit_ofs + 0));
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
  void DecodeStep28() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetEmitOp30(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 4;
    switch ((op >> 3) & 1) {
      case 1: {
        DecodeStep29();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer30(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep29() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetEmitOp31(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 3;
    switch ((op >> 2) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmitBuffer31(emit_ofs + 0));
        break;
      }
    }
  }
  void Done() {
    if (buffer_len_ < 6) {
      buffer_ = (buffer_ << (6 - buffer_len_)) |
                ((uint64_t(1) << (6 - buffer_len_)) - 1);
      buffer_len_ = 6;
    }
    const auto index = (buffer_ >> (buffer_len_ - 6)) & 0x3f;
    const auto op = GetEmitOp0(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 8;
    switch ((op >> 3) & 31) {
      case 1:
      case 10:
      case 11:
      case 12:
      case 13:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9: {
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
