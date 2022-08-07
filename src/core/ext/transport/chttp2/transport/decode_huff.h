#include <stdlib.h>

#include <cstddef>
#include <cstdint>
static const uint8_t g_table1_0_emit[74] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20,
    0x25, 0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3d, 0x41, 0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e,
    0x70, 0x72, 0x75, 0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x59, 0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x26, 0x2a, 0x2c, 0x3b, 0x58, 0x5a};
static const uint16_t g_table1_0_inner[76] = {
    0x0005, 0x0045, 0x0085, 0x00c5, 0x0105, 0x0145, 0x0185, 0x01c5, 0x0205,
    0x0245, 0x0286, 0x02c6, 0x0306, 0x0346, 0x0386, 0x03c6, 0x0406, 0x0446,
    0x0486, 0x04c6, 0x0506, 0x0546, 0x0586, 0x05c6, 0x0606, 0x0646, 0x0686,
    0x06c6, 0x0706, 0x0746, 0x0786, 0x07c6, 0x0806, 0x0846, 0x0886, 0x08c6,
    0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87, 0x0ac7, 0x0b07,
    0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7, 0x0d07, 0x0d47,
    0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07, 0x0f47, 0x0f87,
    0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x1108, 0x1148, 0x1188, 0x11c8,
    0x1208, 0x1248, 0x0018, 0x0028};
static const uint8_t g_table1_0_outer[256] = {
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
inline uint64_t GetOp1(size_t i) {
  return g_table1_0_inner[g_table1_0_outer[i]];
}
inline uint64_t GetEmit1(size_t, size_t emit) { return g_table1_0_emit[emit]; }
static const uint8_t g_table2_0_emit[2] = {0x30, 0x31};
static const uint16_t g_table2_0_inner[2] = {0x0005, 0x0085};
static const uint8_t g_table2_0_outer[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                             0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
                                             1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t g_table2_1_emit[2] = {0x32, 0x61};
#define g_table2_1_inner g_table2_0_inner
#define g_table2_1_outer g_table2_0_outer
static const uint8_t g_table2_2_emit[2] = {0x63, 0x65};
#define g_table2_2_inner g_table2_0_inner
#define g_table2_2_outer g_table2_0_outer
static const uint8_t g_table2_3_emit[2] = {0x69, 0x6f};
#define g_table2_3_inner g_table2_0_inner
#define g_table2_3_outer g_table2_0_outer
static const uint8_t g_table2_4_emit[2] = {0x73, 0x74};
#define g_table2_4_inner g_table2_0_inner
#define g_table2_4_outer g_table2_0_outer
static const uint8_t g_table2_5_emit[4] = {0x20, 0x25, 0x2d, 0x2e};
static const uint16_t g_table2_5_inner[4] = {0x0006, 0x0086, 0x0106, 0x0186};
static const uint8_t g_table2_5_outer[32] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
                                             1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
                                             2, 2, 3, 3, 3, 3, 3, 3, 3, 3};
static const uint8_t g_table2_6_emit[4] = {0x2f, 0x33, 0x34, 0x35};
#define g_table2_6_inner g_table2_5_inner
#define g_table2_6_outer g_table2_5_outer
static const uint8_t g_table2_7_emit[4] = {0x36, 0x37, 0x38, 0x39};
#define g_table2_7_inner g_table2_5_inner
#define g_table2_7_outer g_table2_5_outer
static const uint8_t g_table2_8_emit[4] = {0x3d, 0x41, 0x5f, 0x62};
#define g_table2_8_inner g_table2_5_inner
#define g_table2_8_outer g_table2_5_outer
static const uint8_t g_table2_9_emit[4] = {0x64, 0x66, 0x67, 0x68};
#define g_table2_9_inner g_table2_5_inner
#define g_table2_9_outer g_table2_5_outer
static const uint8_t g_table2_10_emit[4] = {0x6c, 0x6d, 0x6e, 0x70};
#define g_table2_10_inner g_table2_5_inner
#define g_table2_10_outer g_table2_5_outer
static const uint8_t g_table2_11_emit[6] = {0x72, 0x75, 0x3a, 0x42, 0x43, 0x44};
static const uint16_t g_table2_11_inner[6] = {0x0006, 0x0086, 0x0107,
                                              0x0187, 0x0207, 0x0287};
static const uint8_t g_table2_11_outer[32] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
                                              1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3,
                                              3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
static const uint8_t g_table2_12_emit[8] = {0x45, 0x46, 0x47, 0x48,
                                            0x49, 0x4a, 0x4b, 0x4c};
static const uint16_t g_table2_12_inner[8] = {0x0007, 0x0087, 0x0107, 0x0187,
                                              0x0207, 0x0287, 0x0307, 0x0387};
static const uint8_t g_table2_12_outer[32] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5,
                                              5, 5, 6, 6, 6, 6, 7, 7, 7, 7};
static const uint8_t g_table2_13_emit[8] = {0x4d, 0x4e, 0x4f, 0x50,
                                            0x51, 0x52, 0x53, 0x54};
#define g_table2_13_inner g_table2_12_inner
#define g_table2_13_outer g_table2_12_outer
static const uint8_t g_table2_14_emit[8] = {0x55, 0x56, 0x57, 0x59,
                                            0x6a, 0x6b, 0x71, 0x76};
#define g_table2_14_inner g_table2_12_inner
#define g_table2_14_outer g_table2_12_outer
static const uint8_t g_table2_15_emit[10] = {0x77, 0x78, 0x79, 0x7a, 0x26,
                                             0x2a, 0x2c, 0x3b, 0x58, 0x5a};
static const uint16_t g_table2_15_inner[14] = {
    0x0007, 0x0087, 0x0107, 0x0187, 0x0208, 0x0288, 0x0308,
    0x0388, 0x0408, 0x0488, 0x0019, 0x0029, 0x0039, 0x0049};
static const uint8_t g_table2_15_outer[32] = {
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3,  3,  3,  3,
    4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 12, 13};
static const uint8_t* const g_table2_emit[] = {
    g_table2_0_emit,  g_table2_1_emit,  g_table2_2_emit,  g_table2_3_emit,
    g_table2_4_emit,  g_table2_5_emit,  g_table2_6_emit,  g_table2_7_emit,
    g_table2_8_emit,  g_table2_9_emit,  g_table2_10_emit, g_table2_11_emit,
    g_table2_12_emit, g_table2_13_emit, g_table2_14_emit, g_table2_15_emit,
};
static const uint16_t* const g_table2_inner[] = {
    g_table2_0_inner,  g_table2_1_inner,  g_table2_2_inner,  g_table2_3_inner,
    g_table2_4_inner,  g_table2_5_inner,  g_table2_6_inner,  g_table2_7_inner,
    g_table2_8_inner,  g_table2_9_inner,  g_table2_10_inner, g_table2_11_inner,
    g_table2_12_inner, g_table2_13_inner, g_table2_14_inner, g_table2_15_inner,
};
static const uint8_t* const g_table2_outer[] = {
    g_table2_0_outer,  g_table2_1_outer,  g_table2_2_outer,  g_table2_3_outer,
    g_table2_4_outer,  g_table2_5_outer,  g_table2_6_outer,  g_table2_7_outer,
    g_table2_8_outer,  g_table2_9_outer,  g_table2_10_outer, g_table2_11_outer,
    g_table2_12_outer, g_table2_13_outer, g_table2_14_outer, g_table2_15_outer,
};
inline uint64_t GetOp2(size_t i) {
  return g_table2_inner[i >> 5][g_table2_outer[i >> 5][i & 0x1f]];
}
inline uint64_t GetEmit2(size_t i, size_t emit) {
  return g_table2_emit[i >> 5][emit];
}
static const uint8_t g_table3_0_emit[2] = {0x21, 0x22};
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
static const uint8_t g_table4_0_emit[2] = {0x28, 0x29};
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
static const uint8_t g_table5_0_emit[3] = {0x3f, 0x27, 0x2b};
static const uint8_t g_table5_0_ops[4] = {0x01, 0x01, 0x06, 0x0a};
static const uint8_t* const g_table5_emit[] = {
    g_table5_0_emit,
};
static const uint8_t* const g_table5_ops[] = {
    g_table5_0_ops,
};
inline uint64_t GetOp5(size_t i) { return g_table5_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit5(size_t i, size_t emit) {
  return g_table5_emit[i >> 2][emit];
}
static const uint8_t g_table6_0_emit[1] = {0x7c};
static const uint16_t g_table6_0_inner[1] = {0x0002};
static const uint8_t g_table6_0_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#define g_table6_1_emit g_table6_0_emit
#define g_table6_1_inner g_table6_0_inner
#define g_table6_1_outer g_table6_0_outer
static const uint8_t g_table6_2_emit[1] = {0x23};
static const uint16_t g_table6_2_inner[1] = {0x0003};
#define g_table6_2_outer g_table6_0_outer
static const uint8_t g_table6_3_emit[1] = {0x3e};
#define g_table6_3_inner g_table6_2_inner
#define g_table6_3_outer g_table6_0_outer
static const uint8_t g_table6_4_emit[2] = {0x00, 0x24};
static const uint16_t g_table6_4_inner[2] = {0x0004, 0x0104};
static const uint8_t g_table6_4_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t g_table6_5_emit[2] = {0x40, 0x5b};
#define g_table6_5_inner g_table6_4_inner
#define g_table6_5_outer g_table6_4_outer
static const uint8_t g_table6_6_emit[2] = {0x5d, 0x7e};
#define g_table6_6_inner g_table6_4_inner
#define g_table6_6_outer g_table6_4_outer
static const uint8_t g_table6_7_emit[5] = {0x5e, 0x7d, 0x3c, 0x60, 0x7b};
static const uint16_t g_table6_7_inner[13] = {
    0x0005, 0x0105, 0x0206, 0x0306, 0x0406, 0x0019, 0x0029,
    0x0039, 0x0049, 0x0059, 0x0069, 0x0079, 0x0089};
static const uint8_t g_table6_7_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,  1,  1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,  3,  3, 3, 3,
    3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t* const g_table6_emit[] = {
    g_table6_0_emit, g_table6_1_emit, g_table6_2_emit, g_table6_3_emit,
    g_table6_4_emit, g_table6_5_emit, g_table6_6_emit, g_table6_7_emit,
};
static const uint16_t* const g_table6_inner[] = {
    g_table6_0_inner, g_table6_1_inner, g_table6_2_inner, g_table6_3_inner,
    g_table6_4_inner, g_table6_5_inner, g_table6_6_inner, g_table6_7_inner,
};
static const uint8_t* const g_table6_outer[] = {
    g_table6_0_outer, g_table6_1_outer, g_table6_2_outer, g_table6_3_outer,
    g_table6_4_outer, g_table6_5_outer, g_table6_6_outer, g_table6_7_outer,
};
inline uint64_t GetOp6(size_t i) {
  return g_table6_inner[i >> 6][g_table6_outer[i >> 6][i & 0x3f]];
}
inline uint64_t GetEmit6(size_t i, size_t emit) {
  return g_table6_emit[i >> 6][emit];
}
static const uint8_t g_table7_0_emit[2] = {0x5c, 0xc3};
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
static const uint8_t g_table8_0_emit[4] = {0x83, 0xa2, 0xb8, 0xc2};
static const uint8_t g_table8_0_ops[4] = {0x02, 0x06, 0x0a, 0x0e};
static const uint8_t* const g_table8_emit[] = {
    g_table8_0_emit,
};
static const uint8_t* const g_table8_ops[] = {
    g_table8_0_ops,
};
inline uint64_t GetOp8(size_t i) { return g_table8_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit8(size_t i, size_t emit) {
  return g_table8_emit[i >> 2][emit];
}
static const uint8_t g_table9_0_emit[8] = {0xb0, 0xb1, 0xb3, 0xd1,
                                           0xd8, 0xd9, 0xe3, 0xe5};
static const uint8_t g_table9_0_inner[8] = {0x03, 0x07, 0x0b, 0x0f,
                                            0x13, 0x17, 0x1b, 0x1f};
static const uint8_t g_table9_0_outer[8] = {0, 1, 2, 3, 4, 5, 6, 7};
inline uint64_t GetOp9(size_t i) {
  return g_table9_0_inner[g_table9_0_outer[i]];
}
inline uint64_t GetEmit9(size_t, size_t emit) { return g_table9_0_emit[emit]; }
static const uint8_t g_table10_0_emit[3] = {0xd0, 0x80, 0x82};
static const uint8_t g_table10_0_inner[3] = {0x01, 0x06, 0x0a};
static const uint8_t g_table10_0_outer[4] = {0, 0, 1, 2};
inline uint64_t GetOp10(size_t i) {
  return g_table10_0_inner[g_table10_0_outer[i]];
}
inline uint64_t GetEmit10(size_t, size_t emit) {
  return g_table10_0_emit[emit];
}
static const uint8_t g_table11_0_emit[15] = {0xe6, 0x81, 0x84, 0x85, 0x86,
                                             0x88, 0x92, 0x9a, 0x9c, 0xa0,
                                             0xa3, 0xa4, 0xa9, 0xaa, 0xad};
static const uint8_t g_table11_0_inner[15] = {0x03, 0x0c, 0x14, 0x1c, 0x24,
                                              0x2c, 0x34, 0x3c, 0x44, 0x4c,
                                              0x54, 0x5c, 0x64, 0x6c, 0x74};
static const uint8_t g_table11_0_outer[16] = {0, 0, 1, 2,  3,  4,  5,  6,
                                              7, 8, 9, 10, 11, 12, 13, 14};
inline uint64_t GetOp11(size_t i) {
  return g_table11_0_inner[g_table11_0_outer[i]];
}
inline uint64_t GetEmit11(size_t, size_t emit) {
  return g_table11_0_emit[emit];
}
static const uint8_t g_table12_0_emit[6] = {0xe0, 0xe2, 0x99, 0xa1, 0xa7, 0xac};
static const uint8_t g_table12_0_inner[6] = {0x02, 0x06, 0x0b,
                                             0x0f, 0x13, 0x17};
static const uint8_t g_table12_0_outer[8] = {0, 0, 1, 1, 2, 3, 4, 5};
inline uint64_t GetOp12(size_t i) {
  return g_table12_0_inner[g_table12_0_outer[i]];
}
inline uint64_t GetEmit12(size_t, size_t emit) {
  return g_table12_0_emit[emit];
}
static const uint8_t g_table13_0_emit[20] = {
    0xb2, 0xb5, 0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4,
    0xe8, 0xe9, 0x01, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8f};
static const uint8_t g_table13_0_inner[20] = {
    0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c, 0x44, 0x4c,
    0x54, 0x5c, 0x65, 0x6d, 0x75, 0x7d, 0x85, 0x8d, 0x95, 0x9d};
static const uint8_t g_table13_0_outer[32] = {
    0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8, 8, 9, 9, 10, 10, 11, 11, 12, 13, 14, 15, 16, 17, 18, 19};
inline uint64_t GetOp13(size_t i) {
  return g_table13_0_inner[g_table13_0_outer[i]];
}
inline uint64_t GetEmit13(size_t, size_t emit) {
  return g_table13_0_emit[emit];
}
static const uint8_t g_table14_0_emit[1] = {0x93};
static const uint16_t g_table14_0_inner[1] = {0x0005};
static const uint8_t g_table14_0_outer[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t g_table14_1_emit[1] = {0x95};
#define g_table14_1_inner g_table14_0_inner
#define g_table14_1_outer g_table14_0_outer
static const uint8_t g_table14_2_emit[1] = {0x96};
#define g_table14_2_inner g_table14_0_inner
#define g_table14_2_outer g_table14_0_outer
static const uint8_t g_table14_3_emit[1] = {0x97};
#define g_table14_3_inner g_table14_0_inner
#define g_table14_3_outer g_table14_0_outer
static const uint8_t g_table14_4_emit[1] = {0x98};
#define g_table14_4_inner g_table14_0_inner
#define g_table14_4_outer g_table14_0_outer
static const uint8_t g_table14_5_emit[1] = {0x9b};
#define g_table14_5_inner g_table14_0_inner
#define g_table14_5_outer g_table14_0_outer
static const uint8_t g_table14_6_emit[1] = {0x9d};
#define g_table14_6_inner g_table14_0_inner
#define g_table14_6_outer g_table14_0_outer
static const uint8_t g_table14_7_emit[1] = {0x9e};
#define g_table14_7_inner g_table14_0_inner
#define g_table14_7_outer g_table14_0_outer
static const uint8_t g_table14_8_emit[1] = {0xa5};
#define g_table14_8_inner g_table14_0_inner
#define g_table14_8_outer g_table14_0_outer
static const uint8_t g_table14_9_emit[1] = {0xa6};
#define g_table14_9_inner g_table14_0_inner
#define g_table14_9_outer g_table14_0_outer
static const uint8_t g_table14_10_emit[1] = {0xa8};
#define g_table14_10_inner g_table14_0_inner
#define g_table14_10_outer g_table14_0_outer
static const uint8_t g_table14_11_emit[1] = {0xae};
#define g_table14_11_inner g_table14_0_inner
#define g_table14_11_outer g_table14_0_outer
static const uint8_t g_table14_12_emit[1] = {0xaf};
#define g_table14_12_inner g_table14_0_inner
#define g_table14_12_outer g_table14_0_outer
static const uint8_t g_table14_13_emit[1] = {0xb4};
#define g_table14_13_inner g_table14_0_inner
#define g_table14_13_outer g_table14_0_outer
static const uint8_t g_table14_14_emit[1] = {0xb6};
#define g_table14_14_inner g_table14_0_inner
#define g_table14_14_outer g_table14_0_outer
static const uint8_t g_table14_15_emit[1] = {0xb7};
#define g_table14_15_inner g_table14_0_inner
#define g_table14_15_outer g_table14_0_outer
static const uint8_t g_table14_16_emit[1] = {0xbc};
#define g_table14_16_inner g_table14_0_inner
#define g_table14_16_outer g_table14_0_outer
static const uint8_t g_table14_17_emit[1] = {0xbf};
#define g_table14_17_inner g_table14_0_inner
#define g_table14_17_outer g_table14_0_outer
static const uint8_t g_table14_18_emit[1] = {0xc5};
#define g_table14_18_inner g_table14_0_inner
#define g_table14_18_outer g_table14_0_outer
static const uint8_t g_table14_19_emit[1] = {0xe7};
#define g_table14_19_inner g_table14_0_inner
#define g_table14_19_outer g_table14_0_outer
static const uint8_t g_table14_20_emit[1] = {0xef};
#define g_table14_20_inner g_table14_0_inner
#define g_table14_20_outer g_table14_0_outer
static const uint8_t g_table14_21_emit[2] = {0x09, 0x8e};
static const uint16_t g_table14_21_inner[2] = {0x0006, 0x0026};
static const uint8_t g_table14_21_outer[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t g_table14_22_emit[2] = {0x90, 0x91};
#define g_table14_22_inner g_table14_21_inner
#define g_table14_22_outer g_table14_21_outer
static const uint8_t g_table14_23_emit[2] = {0x94, 0x9f};
#define g_table14_23_inner g_table14_21_inner
#define g_table14_23_outer g_table14_21_outer
static const uint8_t g_table14_24_emit[2] = {0xab, 0xce};
#define g_table14_24_inner g_table14_21_inner
#define g_table14_24_outer g_table14_21_outer
static const uint8_t g_table14_25_emit[2] = {0xd7, 0xe1};
#define g_table14_25_inner g_table14_21_inner
#define g_table14_25_outer g_table14_21_outer
static const uint8_t g_table14_26_emit[2] = {0xec, 0xed};
#define g_table14_26_inner g_table14_21_inner
#define g_table14_26_outer g_table14_21_outer
static const uint8_t g_table14_27_emit[4] = {0xc7, 0xcf, 0xea, 0xeb};
static const uint16_t g_table14_27_inner[4] = {0x0007, 0x0027, 0x0047, 0x0067};
static const uint8_t g_table14_27_outer[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
static const uint8_t g_table14_28_emit[8] = {0xc0, 0xc1, 0xc8, 0xc9,
                                             0xca, 0xcd, 0xd2, 0xd5};
static const uint16_t g_table14_28_inner[8] = {0x0008, 0x0028, 0x0048, 0x0068,
                                               0x0088, 0x00a8, 0x00c8, 0x00e8};
static const uint8_t g_table14_28_outer[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
static const uint8_t g_table14_29_emit[9] = {0xda, 0xdb, 0xee, 0xf0, 0xf2,
                                             0xf3, 0xff, 0xcb, 0xcc};
static const uint16_t g_table14_29_inner[9] = {
    0x0008, 0x0028, 0x0048, 0x0068, 0x0088, 0x00a8, 0x00c8, 0x00e9, 0x0109};
static const uint8_t g_table14_29_outer[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8};
static const uint8_t g_table14_30_emit[16] = {
    0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf, 0xf1, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd};
static const uint16_t g_table14_30_inner[16] = {
    0x0009, 0x0029, 0x0049, 0x0069, 0x0089, 0x00a9, 0x00c9, 0x00e9,
    0x0109, 0x0129, 0x0149, 0x0169, 0x0189, 0x01a9, 0x01c9, 0x01e9};
static const uint8_t g_table14_30_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
    2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,
    4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  7,
    7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,
    9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11,
    11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14,
    14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15};
static const uint8_t g_table14_31_emit[33] = {
    0xfe, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a,
    0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16};
static const uint16_t g_table14_31_inner[34] = {
    0x0009, 0x002a, 0x004a, 0x006a, 0x008a, 0x00aa, 0x00ca, 0x00ea, 0x010a,
    0x012a, 0x014a, 0x016a, 0x018a, 0x01aa, 0x01ca, 0x01ea, 0x020a, 0x022a,
    0x024a, 0x026a, 0x028a, 0x02aa, 0x02ca, 0x02ea, 0x030a, 0x032a, 0x034a,
    0x036a, 0x038a, 0x03aa, 0x03cc, 0x03ec, 0x040c, 0x001c};
static const uint8_t g_table14_31_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,
    3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,
    8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13,
    13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17,
    18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22,
    22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27,
    27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 31, 32, 33};
static const uint8_t* const g_table14_emit[] = {
    g_table14_0_emit,  g_table14_1_emit,  g_table14_2_emit,  g_table14_3_emit,
    g_table14_4_emit,  g_table14_5_emit,  g_table14_6_emit,  g_table14_7_emit,
    g_table14_8_emit,  g_table14_9_emit,  g_table14_10_emit, g_table14_11_emit,
    g_table14_12_emit, g_table14_13_emit, g_table14_14_emit, g_table14_15_emit,
    g_table14_16_emit, g_table14_17_emit, g_table14_18_emit, g_table14_19_emit,
    g_table14_20_emit, g_table14_21_emit, g_table14_22_emit, g_table14_23_emit,
    g_table14_24_emit, g_table14_25_emit, g_table14_26_emit, g_table14_27_emit,
    g_table14_28_emit, g_table14_29_emit, g_table14_30_emit, g_table14_31_emit,
};
static const uint16_t* const g_table14_inner[] = {
    g_table14_0_inner,  g_table14_1_inner,  g_table14_2_inner,
    g_table14_3_inner,  g_table14_4_inner,  g_table14_5_inner,
    g_table14_6_inner,  g_table14_7_inner,  g_table14_8_inner,
    g_table14_9_inner,  g_table14_10_inner, g_table14_11_inner,
    g_table14_12_inner, g_table14_13_inner, g_table14_14_inner,
    g_table14_15_inner, g_table14_16_inner, g_table14_17_inner,
    g_table14_18_inner, g_table14_19_inner, g_table14_20_inner,
    g_table14_21_inner, g_table14_22_inner, g_table14_23_inner,
    g_table14_24_inner, g_table14_25_inner, g_table14_26_inner,
    g_table14_27_inner, g_table14_28_inner, g_table14_29_inner,
    g_table14_30_inner, g_table14_31_inner,
};
static const uint8_t* const g_table14_outer[] = {
    g_table14_0_outer,  g_table14_1_outer,  g_table14_2_outer,
    g_table14_3_outer,  g_table14_4_outer,  g_table14_5_outer,
    g_table14_6_outer,  g_table14_7_outer,  g_table14_8_outer,
    g_table14_9_outer,  g_table14_10_outer, g_table14_11_outer,
    g_table14_12_outer, g_table14_13_outer, g_table14_14_outer,
    g_table14_15_outer, g_table14_16_outer, g_table14_17_outer,
    g_table14_18_outer, g_table14_19_outer, g_table14_20_outer,
    g_table14_21_outer, g_table14_22_outer, g_table14_23_outer,
    g_table14_24_outer, g_table14_25_outer, g_table14_26_outer,
    g_table14_27_outer, g_table14_28_outer, g_table14_29_outer,
    g_table14_30_outer, g_table14_31_outer,
};
inline uint64_t GetOp14(size_t i) {
  return g_table14_inner[i >> 7][g_table14_outer[i >> 7][i & 0x7f]];
}
inline uint64_t GetEmit14(size_t i, size_t emit) {
  return g_table14_emit[i >> 7][emit];
}
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
      const auto op = GetOp2(index);
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
          sink_(GetEmit2(index, emit_ofs + 0));
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp5(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit5(index, emit_ofs + 0));
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
    const auto op = GetOp6(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 8;
    switch ((op >> 4) & 15) {
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp8(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit8(index, emit_ofs + 0));
  }
  void DecodeStep6() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetOp9(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit9(index, emit_ofs + 0));
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
    const auto op = GetOp10(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit10(index, emit_ofs + 0));
  }
  void DecodeStep8() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetOp11(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit11(index, emit_ofs + 0));
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
    const auto op = GetOp12(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit12(index, emit_ofs + 0));
  }
  void DecodeStep10() {
    if (!RefillTo5()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 5)) & 0x1f;
    const auto op = GetOp13(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit13(index, emit_ofs + 0));
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
    if (!RefillTo12()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 12)) & 0xfff;
    const auto op = GetOp14(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 5;
    switch ((op >> 4) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit14(index, emit_ofs + 0));
        break;
      }
    }
  }
  bool RefillTo12() {
    switch (buffer_len_) {
      case 10:
      case 11:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9: {
        return Read1();
      }
      case 0:
      case 1:
      case 2:
      case 3: {
        return Read2();
      }
    }
    return true;
  }
  void Done() {
    if (buffer_len_ < 8) {
      buffer_ = (buffer_ << (8 - buffer_len_)) |
                ((uint64_t(1) << (8 - buffer_len_)) - 1);
      buffer_len_ = 8;
    }
    const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
    const auto op = GetOp1(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 6;
    switch ((op >> 4) & 3) {
      case 1:
      case 2: {
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
