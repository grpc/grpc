#include <stdlib.h>

#include <cstddef>
#include <cstdint>
static const uint8_t g_table1_0_emit[36] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75};
static const uint16_t g_table1_0_ops[64] = {
    0x0005, 0x0005, 0x0105, 0x0105, 0x0205, 0x0205, 0x0305, 0x0305,
    0x0405, 0x0405, 0x0505, 0x0505, 0x0605, 0x0605, 0x0705, 0x0705,
    0x0805, 0x0805, 0x0905, 0x0905, 0x0a06, 0x0b06, 0x0c06, 0x0d06,
    0x0e06, 0x0f06, 0x1006, 0x1106, 0x1206, 0x1306, 0x1406, 0x1506,
    0x1606, 0x1706, 0x1806, 0x1906, 0x1a06, 0x1b06, 0x1c06, 0x1d06,
    0x1e06, 0x1f06, 0x2006, 0x2106, 0x2206, 0x2306, 0x000e, 0x0016,
    0x001e, 0x0026, 0x002e, 0x0036, 0x003e, 0x0046, 0x004e, 0x0056,
    0x005e, 0x0066, 0x006e, 0x0076, 0x007e, 0x0086, 0x008e, 0x0096};
static const uint8_t* const g_table1_emit[] = {
    g_table1_0_emit,
};
static const uint16_t* const g_table1_ops[] = {
    g_table1_0_ops,
};
inline uint64_t GetOp1(size_t i) { return g_table1_ops[i >> 6][i & 0x3f]; }
inline uint64_t GetEmit1(size_t i, size_t emit) {
  return g_table1_emit[i >> 6][emit];
}
static const uint8_t g_table2_0_emit[68] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75,
    0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59,
    0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78, 0x79, 0x7a};
static const uint16_t g_table2_0_ops[128] = {
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
static const uint8_t* const g_table2_emit[] = {
    g_table2_0_emit,
};
static const uint16_t* const g_table2_ops[] = {
    g_table2_0_ops,
};
inline uint64_t GetOp2(size_t i) { return g_table2_ops[i >> 7][i & 0x7f]; }
inline uint64_t GetEmit2(size_t i, size_t emit) {
  return g_table2_emit[i >> 7][emit];
}
static const uint8_t g_table3_0_emit[2] = {0x26, 0x2a};
static const uint8_t g_table3_0_ops[2] = {0x01, 0x03};
static const uint8_t* const g_table3_emit[] = {
    g_table3_0_emit,
};
static const uint8_t* const g_table3_ops[] = {
    g_table3_0_ops,
};
inline uint64_t GetOp3(size_t i) { return g_table3_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit3(size_t i, size_t emit) {
  return g_table3_emit[i >> 1][emit];
}
static const uint8_t g_table4_0_emit[2] = {0x2c, 0x3b};
#define g_table4_0_ops g_table3_0_ops
static const uint8_t* const g_table4_emit[] = {
    g_table4_0_emit,
};
static const uint8_t* const g_table4_ops[] = {
    g_table4_0_ops,
};
inline uint64_t GetOp4(size_t i) { return g_table4_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit4(size_t i, size_t emit) {
  return g_table4_emit[i >> 1][emit];
}
static const uint8_t g_table5_0_emit[2] = {0x58, 0x5a};
#define g_table5_0_ops g_table3_0_ops
static const uint8_t* const g_table5_emit[] = {
    g_table5_0_emit,
};
static const uint8_t* const g_table5_ops[] = {
    g_table5_0_ops,
};
inline uint64_t GetOp5(size_t i) { return g_table5_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit5(size_t i, size_t emit) {
  return g_table5_emit[i >> 1][emit];
}
static const uint8_t g_table6_0_emit[18] = {0x21, 0x22, 0x28, 0x29, 0x3f, 0x27,
                                            0x2b, 0x7c, 0x23, 0x3e, 0x00, 0x24,
                                            0x40, 0x5b, 0x5d, 0x7e, 0x5e, 0x7d};
static const uint16_t g_table6_0_inner[20] = {
    0x0003, 0x0023, 0x0043, 0x0063, 0x0083, 0x00a4, 0x00c4,
    0x00e4, 0x0105, 0x0125, 0x0146, 0x0166, 0x0186, 0x01a6,
    0x01c6, 0x01e6, 0x0207, 0x0227, 0x000f, 0x0017};
static const uint8_t g_table6_0_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 1, 1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2, 2, 2, 2, 2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3, 3, 3, 3, 3,  3,
    3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4, 4, 4, 4, 4,  4,
    4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6, 6, 6, 6, 6,  6,
    6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  9, 9, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 17, 18, 19};
static const uint8_t* const g_table6_emit[] = {
    g_table6_0_emit,
};
static const uint16_t* const g_table6_inner[] = {
    g_table6_0_inner,
};
static const uint8_t* const g_table6_outer[] = {
    g_table6_0_outer,
};
inline uint64_t GetOp6(size_t i) {
  return g_table6_inner[i >> 7][g_table6_outer[i >> 7][i & 0x7f]];
}
inline uint64_t GetEmit6(size_t i, size_t emit) {
  return g_table6_emit[i >> 7][emit];
}
static const uint8_t g_table7_0_emit[2] = {0x3c, 0x60};
#define g_table7_0_ops g_table3_0_ops
static const uint8_t* const g_table7_emit[] = {
    g_table7_0_emit,
};
static const uint8_t* const g_table7_ops[] = {
    g_table7_0_ops,
};
inline uint64_t GetOp7(size_t i) { return g_table7_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit7(size_t i, size_t emit) {
  return g_table7_emit[i >> 1][emit];
}
static const uint8_t g_table8_0_emit[96] = {
    0x7b, 0x30, 0x7b, 0x31, 0x7b, 0x32, 0x7b, 0x61, 0x7b, 0x63, 0x7b, 0x65,
    0x7b, 0x69, 0x7b, 0x6f, 0x7b, 0x73, 0x7b, 0x74, 0x7b, 0x20, 0x7b, 0x25,
    0x7b, 0x2d, 0x7b, 0x2e, 0x7b, 0x2f, 0x7b, 0x33, 0x7b, 0x34, 0x7b, 0x35,
    0x7b, 0x36, 0x7b, 0x37, 0x7b, 0x38, 0x7b, 0x39, 0x7b, 0x3d, 0x7b, 0x41,
    0x7b, 0x5f, 0x7b, 0x62, 0x7b, 0x64, 0x7b, 0x66, 0x7b, 0x67, 0x7b, 0x68,
    0x7b, 0x6c, 0x7b, 0x6d, 0x7b, 0x6e, 0x7b, 0x70, 0x7b, 0x72, 0x7b, 0x75,
    0x5c, 0xc3, 0xd0, 0x80, 0x82, 0x83, 0xa2, 0xb8, 0xc2, 0xe0, 0xe2, 0x99,
    0xa1, 0xa7, 0xac, 0xb0, 0xb1, 0xb3, 0xd1, 0xd8, 0xd9, 0xe3, 0xe5, 0xe6};
static const uint16_t g_table8_0_ops[128] = {
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
static const uint8_t* const g_table8_emit[] = {
    g_table8_0_emit,
};
static const uint16_t* const g_table8_ops[] = {
    g_table8_0_ops,
};
inline uint64_t GetOp8(size_t i) { return g_table8_ops[i >> 7][i & 0x7f]; }
inline uint64_t GetEmit8(size_t i, size_t emit) {
  return g_table8_emit[i >> 7][emit];
}
static const uint8_t g_table9_0_emit[2] = {0x81, 0x84};
#define g_table9_0_ops g_table3_0_ops
static const uint8_t* const g_table9_emit[] = {
    g_table9_0_emit,
};
static const uint8_t* const g_table9_ops[] = {
    g_table9_0_ops,
};
inline uint64_t GetOp9(size_t i) { return g_table9_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit9(size_t i, size_t emit) {
  return g_table9_emit[i >> 1][emit];
}
static const uint8_t g_table10_0_emit[2] = {0x85, 0x86};
#define g_table10_0_ops g_table3_0_ops
static const uint8_t* const g_table10_emit[] = {
    g_table10_0_emit,
};
static const uint8_t* const g_table10_ops[] = {
    g_table10_0_ops,
};
inline uint64_t GetOp10(size_t i) { return g_table10_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit10(size_t i, size_t emit) {
  return g_table10_emit[i >> 1][emit];
}
static const uint8_t g_table11_0_emit[2] = {0x88, 0x92};
#define g_table11_0_ops g_table3_0_ops
static const uint8_t* const g_table11_emit[] = {
    g_table11_0_emit,
};
static const uint8_t* const g_table11_ops[] = {
    g_table11_0_ops,
};
inline uint64_t GetOp11(size_t i) { return g_table11_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit11(size_t i, size_t emit) {
  return g_table11_emit[i >> 1][emit];
}
static const uint8_t g_table12_0_emit[2] = {0x9a, 0x9c};
#define g_table12_0_ops g_table3_0_ops
static const uint8_t* const g_table12_emit[] = {
    g_table12_0_emit,
};
static const uint8_t* const g_table12_ops[] = {
    g_table12_0_ops,
};
inline uint64_t GetOp12(size_t i) { return g_table12_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit12(size_t i, size_t emit) {
  return g_table12_emit[i >> 1][emit];
}
static const uint8_t g_table13_0_emit[2] = {0xa0, 0xa3};
#define g_table13_0_ops g_table3_0_ops
static const uint8_t* const g_table13_emit[] = {
    g_table13_0_emit,
};
static const uint8_t* const g_table13_ops[] = {
    g_table13_0_ops,
};
inline uint64_t GetOp13(size_t i) { return g_table13_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit13(size_t i, size_t emit) {
  return g_table13_emit[i >> 1][emit];
}
static const uint8_t g_table14_0_emit[2] = {0xa4, 0xa9};
#define g_table14_0_ops g_table3_0_ops
static const uint8_t* const g_table14_emit[] = {
    g_table14_0_emit,
};
static const uint8_t* const g_table14_ops[] = {
    g_table14_0_ops,
};
inline uint64_t GetOp14(size_t i) { return g_table14_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit14(size_t i, size_t emit) {
  return g_table14_emit[i >> 1][emit];
}
static const uint8_t g_table15_0_emit[2] = {0xaa, 0xad};
#define g_table15_0_ops g_table3_0_ops
static const uint8_t* const g_table15_emit[] = {
    g_table15_0_emit,
};
static const uint8_t* const g_table15_ops[] = {
    g_table15_0_ops,
};
inline uint64_t GetOp15(size_t i) { return g_table15_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit15(size_t i, size_t emit) {
  return g_table15_emit[i >> 1][emit];
}
static const uint8_t g_table16_0_emit[2] = {0xb2, 0xb5};
#define g_table16_0_ops g_table3_0_ops
static const uint8_t* const g_table16_emit[] = {
    g_table16_0_emit,
};
static const uint8_t* const g_table16_ops[] = {
    g_table16_0_ops,
};
inline uint64_t GetOp16(size_t i) { return g_table16_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit16(size_t i, size_t emit) {
  return g_table16_emit[i >> 1][emit];
}
static const uint8_t g_table17_0_emit[2] = {0xb9, 0xba};
#define g_table17_0_ops g_table3_0_ops
static const uint8_t* const g_table17_emit[] = {
    g_table17_0_emit,
};
static const uint8_t* const g_table17_ops[] = {
    g_table17_0_ops,
};
inline uint64_t GetOp17(size_t i) { return g_table17_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit17(size_t i, size_t emit) {
  return g_table17_emit[i >> 1][emit];
}
static const uint8_t g_table18_0_emit[2] = {0xbb, 0xbd};
#define g_table18_0_ops g_table3_0_ops
static const uint8_t* const g_table18_emit[] = {
    g_table18_0_emit,
};
static const uint8_t* const g_table18_ops[] = {
    g_table18_0_ops,
};
inline uint64_t GetOp18(size_t i) { return g_table18_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit18(size_t i, size_t emit) {
  return g_table18_emit[i >> 1][emit];
}
static const uint8_t g_table19_0_emit[2] = {0xbe, 0xc4};
#define g_table19_0_ops g_table3_0_ops
static const uint8_t* const g_table19_emit[] = {
    g_table19_0_emit,
};
static const uint8_t* const g_table19_ops[] = {
    g_table19_0_ops,
};
inline uint64_t GetOp19(size_t i) { return g_table19_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit19(size_t i, size_t emit) {
  return g_table19_emit[i >> 1][emit];
}
static const uint8_t g_table20_0_emit[2] = {0xc6, 0xe4};
#define g_table20_0_ops g_table3_0_ops
static const uint8_t* const g_table20_emit[] = {
    g_table20_0_emit,
};
static const uint8_t* const g_table20_ops[] = {
    g_table20_0_ops,
};
inline uint64_t GetOp20(size_t i) { return g_table20_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit20(size_t i, size_t emit) {
  return g_table20_emit[i >> 1][emit];
}
static const uint8_t g_table21_0_emit[2] = {0xe8, 0xe9};
#define g_table21_0_ops g_table3_0_ops
static const uint8_t* const g_table21_emit[] = {
    g_table21_0_emit,
};
static const uint8_t* const g_table21_ops[] = {
    g_table21_0_ops,
};
inline uint64_t GetOp21(size_t i) { return g_table21_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit21(size_t i, size_t emit) {
  return g_table21_emit[i >> 1][emit];
}
static const uint8_t g_table22_0_emit[4] = {0x01, 0x87, 0x89, 0x8a};
static const uint8_t g_table22_0_ops[4] = {0x02, 0x06, 0x0a, 0x0e};
static const uint8_t* const g_table22_emit[] = {
    g_table22_0_emit,
};
static const uint8_t* const g_table22_ops[] = {
    g_table22_0_ops,
};
inline uint64_t GetOp22(size_t i) { return g_table22_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit22(size_t i, size_t emit) {
  return g_table22_emit[i >> 2][emit];
}
static const uint8_t g_table23_0_emit[4] = {0x8b, 0x8c, 0x8d, 0x8f};
#define g_table23_0_ops g_table22_0_ops
static const uint8_t* const g_table23_emit[] = {
    g_table23_0_emit,
};
static const uint8_t* const g_table23_ops[] = {
    g_table23_0_ops,
};
inline uint64_t GetOp23(size_t i) { return g_table23_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit23(size_t i, size_t emit) {
  return g_table23_emit[i >> 2][emit];
}
static const uint8_t g_table24_0_emit[4] = {0x93, 0x95, 0x96, 0x97};
#define g_table24_0_ops g_table22_0_ops
static const uint8_t* const g_table24_emit[] = {
    g_table24_0_emit,
};
static const uint8_t* const g_table24_ops[] = {
    g_table24_0_ops,
};
inline uint64_t GetOp24(size_t i) { return g_table24_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit24(size_t i, size_t emit) {
  return g_table24_emit[i >> 2][emit];
}
static const uint8_t g_table25_0_emit[4] = {0x98, 0x9b, 0x9d, 0x9e};
#define g_table25_0_ops g_table22_0_ops
static const uint8_t* const g_table25_emit[] = {
    g_table25_0_emit,
};
static const uint8_t* const g_table25_ops[] = {
    g_table25_0_ops,
};
inline uint64_t GetOp25(size_t i) { return g_table25_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit25(size_t i, size_t emit) {
  return g_table25_emit[i >> 2][emit];
}
static const uint8_t g_table26_0_emit[4] = {0xa5, 0xa6, 0xa8, 0xae};
#define g_table26_0_ops g_table22_0_ops
static const uint8_t* const g_table26_emit[] = {
    g_table26_0_emit,
};
static const uint8_t* const g_table26_ops[] = {
    g_table26_0_ops,
};
inline uint64_t GetOp26(size_t i) { return g_table26_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit26(size_t i, size_t emit) {
  return g_table26_emit[i >> 2][emit];
}
static const uint8_t g_table27_0_emit[4] = {0xaf, 0xb4, 0xb6, 0xb7};
#define g_table27_0_ops g_table22_0_ops
static const uint8_t* const g_table27_emit[] = {
    g_table27_0_emit,
};
static const uint8_t* const g_table27_ops[] = {
    g_table27_0_ops,
};
inline uint64_t GetOp27(size_t i) { return g_table27_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit27(size_t i, size_t emit) {
  return g_table27_emit[i >> 2][emit];
}
static const uint8_t g_table28_0_emit[4] = {0xbc, 0xbf, 0xc5, 0xe7};
#define g_table28_0_ops g_table22_0_ops
static const uint8_t* const g_table28_emit[] = {
    g_table28_0_emit,
};
static const uint8_t* const g_table28_ops[] = {
    g_table28_0_ops,
};
inline uint64_t GetOp28(size_t i) { return g_table28_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit28(size_t i, size_t emit) {
  return g_table28_emit[i >> 2][emit];
}
static const uint8_t g_table29_0_emit[10] = {0xab, 0xce, 0xd7, 0xe1, 0xec,
                                             0xed, 0xc7, 0xcf, 0xea, 0xeb};
static const uint8_t g_table29_0_ops[16] = {0x03, 0x03, 0x0b, 0x0b, 0x13, 0x13,
                                            0x1b, 0x1b, 0x23, 0x23, 0x2b, 0x2b,
                                            0x34, 0x3c, 0x44, 0x4c};
static const uint8_t* const g_table29_emit[] = {
    g_table29_0_emit,
};
static const uint8_t* const g_table29_ops[] = {
    g_table29_0_ops,
};
inline uint64_t GetOp29(size_t i) { return g_table29_ops[i >> 4][i & 0xf]; }
inline uint64_t GetEmit29(size_t i, size_t emit) {
  return g_table29_emit[i >> 4][emit];
}
static const uint8_t g_table30_0_emit[7] = {0xef, 0x09, 0x8e, 0x90,
                                            0x91, 0x94, 0x9f};
static const uint8_t g_table30_0_ops[8] = {0x02, 0x02, 0x07, 0x0b,
                                           0x0f, 0x13, 0x17, 0x1b};
static const uint8_t* const g_table30_emit[] = {
    g_table30_0_emit,
};
static const uint8_t* const g_table30_ops[] = {
    g_table30_0_ops,
};
inline uint64_t GetOp30(size_t i) { return g_table30_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit30(size_t i, size_t emit) {
  return g_table30_emit[i >> 3][emit];
}
static const uint8_t g_table31_0_emit[8] = {0xc0, 0xc1, 0xc8, 0xc9,
                                            0xca, 0xcd, 0xd2, 0xd5};
static const uint16_t g_table31_0_ops[32] = {
    0x0005, 0x0005, 0x0005, 0x0005, 0x0015, 0x0015, 0x0015, 0x0015,
    0x0025, 0x0025, 0x0025, 0x0025, 0x0035, 0x0035, 0x0035, 0x0035,
    0x0045, 0x0045, 0x0045, 0x0045, 0x0055, 0x0055, 0x0055, 0x0055,
    0x0065, 0x0065, 0x0065, 0x0065, 0x0075, 0x0075, 0x0075, 0x0075};
static const uint8_t g_table31_1_emit[9] = {0xda, 0xdb, 0xee, 0xf0, 0xf2,
                                            0xf3, 0xff, 0xcb, 0xcc};
static const uint16_t g_table31_1_ops[32] = {
    0x0005, 0x0005, 0x0005, 0x0005, 0x0015, 0x0015, 0x0015, 0x0015,
    0x0025, 0x0025, 0x0025, 0x0025, 0x0035, 0x0035, 0x0035, 0x0035,
    0x0045, 0x0045, 0x0045, 0x0045, 0x0055, 0x0055, 0x0055, 0x0055,
    0x0065, 0x0065, 0x0065, 0x0065, 0x0076, 0x0076, 0x0086, 0x0086};
static const uint8_t g_table31_2_emit[16] = {0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf,
                                             0xf1, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
                                             0xfa, 0xfb, 0xfc, 0xfd};
static const uint16_t g_table31_2_ops[32] = {
    0x0006, 0x0006, 0x0016, 0x0016, 0x0026, 0x0026, 0x0036, 0x0036,
    0x0046, 0x0046, 0x0056, 0x0056, 0x0066, 0x0066, 0x0076, 0x0076,
    0x0086, 0x0086, 0x0096, 0x0096, 0x00a6, 0x00a6, 0x00b6, 0x00b6,
    0x00c6, 0x00c6, 0x00d6, 0x00d6, 0x00e6, 0x00e6, 0x00f6, 0x00f6};
static const uint8_t g_table31_3_emit[30] = {
    0xfe, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c,
    0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9};
static const uint16_t g_table31_3_ops[32] = {
    0x0006, 0x0006, 0x0017, 0x0027, 0x0037, 0x0047, 0x0057, 0x0067,
    0x0077, 0x0087, 0x0097, 0x00a7, 0x00b7, 0x00c7, 0x00d7, 0x00e7,
    0x00f7, 0x0107, 0x0117, 0x0127, 0x0137, 0x0147, 0x0157, 0x0167,
    0x0177, 0x0187, 0x0197, 0x01a7, 0x01b7, 0x01c7, 0x01d7, 0x000f};
static const uint8_t* const g_table31_emit[] = {
    g_table31_0_emit,
    g_table31_1_emit,
    g_table31_2_emit,
    g_table31_3_emit,
};
static const uint16_t* const g_table31_ops[] = {
    g_table31_0_ops,
    g_table31_1_ops,
    g_table31_2_ops,
    g_table31_3_ops,
};
inline uint64_t GetOp31(size_t i) { return g_table31_ops[i >> 5][i & 0x1f]; }
inline uint64_t GetEmit31(size_t i, size_t emit) {
  return g_table31_emit[i >> 5][emit];
}
static const uint8_t g_table32_0_emit[3] = {0x0a, 0x0d, 0x16};
static const uint8_t g_table32_0_ops[4] = {0x02, 0x0a, 0x12, 0x06};
static const uint8_t* const g_table32_emit[] = {
    g_table32_0_emit,
};
static const uint8_t* const g_table32_ops[] = {
    g_table32_0_ops,
};
inline uint64_t GetOp32(size_t i) { return g_table32_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit32(size_t i, size_t emit) {
  return g_table32_emit[i >> 2][emit];
}
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
      const auto op = GetOp2(index);
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
          sink_(GetEmit2(index, emit_ofs + 0));
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
    const auto op = GetOp3(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit3(index, emit_ofs + 0));
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
    const auto op = GetOp4(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit4(index, emit_ofs + 0));
  }
  void DecodeStep2() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp5(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit5(index, emit_ofs + 0));
  }
  void DecodeStep3() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetOp6(index);
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
        sink_(GetEmit6(index, emit_ofs + 0));
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
    const auto op = GetOp7(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit7(index, emit_ofs + 0));
  }
  void DecodeStep5() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetOp8(index);
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
        sink_(GetEmit8(index, emit_ofs + 0));
        break;
      }
      case 0: {
        sink_(GetEmit8(index, emit_ofs + 0));
        sink_(GetEmit8(index, emit_ofs + 1));
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
    const auto op = GetOp9(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit9(index, emit_ofs + 0));
  }
  void DecodeStep7() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp10(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit10(index, emit_ofs + 0));
  }
  void DecodeStep8() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp11(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit11(index, emit_ofs + 0));
  }
  void DecodeStep9() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp12(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit12(index, emit_ofs + 0));
  }
  void DecodeStep10() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp13(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit13(index, emit_ofs + 0));
  }
  void DecodeStep11() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp14(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit14(index, emit_ofs + 0));
  }
  void DecodeStep12() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp15(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit15(index, emit_ofs + 0));
  }
  void DecodeStep13() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp16(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit16(index, emit_ofs + 0));
  }
  void DecodeStep14() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp17(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit17(index, emit_ofs + 0));
  }
  void DecodeStep15() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp18(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit18(index, emit_ofs + 0));
  }
  void DecodeStep16() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp19(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit19(index, emit_ofs + 0));
  }
  void DecodeStep17() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp20(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit20(index, emit_ofs + 0));
  }
  void DecodeStep18() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp21(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit21(index, emit_ofs + 0));
  }
  void DecodeStep19() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp22(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit22(index, emit_ofs + 0));
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
    const auto op = GetOp23(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit23(index, emit_ofs + 0));
  }
  void DecodeStep21() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp24(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit24(index, emit_ofs + 0));
  }
  void DecodeStep22() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp25(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit25(index, emit_ofs + 0));
  }
  void DecodeStep23() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp26(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit26(index, emit_ofs + 0));
  }
  void DecodeStep24() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp27(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit27(index, emit_ofs + 0));
  }
  void DecodeStep25() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp28(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit28(index, emit_ofs + 0));
  }
  void DecodeStep26() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetOp29(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit29(index, emit_ofs + 0));
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
    const auto op = GetOp30(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit30(index, emit_ofs + 0));
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
    const auto op = GetOp31(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 4;
    switch ((op >> 3) & 1) {
      case 1: {
        DecodeStep29();
        break;
      }
      case 0: {
        sink_(GetEmit31(index, emit_ofs + 0));
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
    const auto op = GetOp32(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 3;
    switch ((op >> 2) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit32(index, emit_ofs + 0));
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
    const auto op = GetOp1(index);
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
        sink_(GetEmit1(index, emit_ofs + 0));
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
