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
static const uint8_t* const g_table1_emit[] = {
    g_table1_0_emit,
};
static const uint16_t* const g_table1_inner[] = {
    g_table1_0_inner,
};
static const uint8_t* const g_table1_outer[] = {
    g_table1_0_outer,
};
inline uint64_t GetOp1(size_t i) {
  return g_table1_inner[i >> 8][g_table1_outer[i >> 8][i & 0xff]];
}
inline uint64_t GetEmit1(size_t i, size_t emit) {
  return g_table1_emit[i >> 8][emit];
}
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
static const uint8_t g_table6_0_emit[44] = {
    0x7c, 0x30, 0x7c, 0x31, 0x7c, 0x32, 0x7c, 0x61, 0x7c, 0x63, 0x7c,
    0x65, 0x7c, 0x69, 0x7c, 0x6f, 0x7c, 0x73, 0x7c, 0x74, 0x7c, 0x20,
    0x7c, 0x25, 0x7c, 0x2d, 0x7c, 0x2e, 0x7c, 0x2f, 0x7c, 0x33, 0x7c,
    0x34, 0x7c, 0x35, 0x7c, 0x36, 0x7c, 0x37, 0x7c, 0x38, 0x7c, 0x39};
static const uint16_t g_table6_0_inner[22] = {
    0x0007, 0x0207, 0x0407, 0x0607, 0x0807, 0x0a07, 0x0c07, 0x0e07,
    0x1007, 0x1207, 0x1408, 0x1608, 0x1808, 0x1a08, 0x1c08, 0x1e08,
    0x2008, 0x2208, 0x2408, 0x2608, 0x2808, 0x2a08};
static const uint8_t g_table6_0_outer[64] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
    4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,
    8,  8,  8,  8,  9,  9,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13,
    14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21};
static const uint8_t g_table6_1_emit[92] = {
    0x7c, 0x3d, 0x7c, 0x41, 0x7c, 0x5f, 0x7c, 0x62, 0x7c, 0x64, 0x7c, 0x66,
    0x7c, 0x67, 0x7c, 0x68, 0x7c, 0x6c, 0x7c, 0x6d, 0x7c, 0x6e, 0x7c, 0x70,
    0x7c, 0x72, 0x7c, 0x75, 0x7c, 0x3a, 0x7c, 0x42, 0x7c, 0x43, 0x7c, 0x44,
    0x7c, 0x45, 0x7c, 0x46, 0x7c, 0x47, 0x7c, 0x48, 0x7c, 0x49, 0x7c, 0x4a,
    0x7c, 0x4b, 0x7c, 0x4c, 0x7c, 0x4d, 0x7c, 0x4e, 0x7c, 0x4f, 0x7c, 0x50,
    0x7c, 0x51, 0x7c, 0x52, 0x7c, 0x53, 0x7c, 0x54, 0x7c, 0x55, 0x7c, 0x56,
    0x7c, 0x57, 0x7c, 0x59, 0x7c, 0x6a, 0x7c, 0x6b, 0x7c, 0x71, 0x7c, 0x76,
    0x7c, 0x77, 0x7c, 0x78, 0x7c, 0x79, 0x7c, 0x7a};
static const uint16_t g_table6_1_inner[47] = {
    0x0008, 0x0208, 0x0408, 0x0608, 0x0808, 0x0a08, 0x0c08, 0x0e08,
    0x1008, 0x1208, 0x1408, 0x1608, 0x1808, 0x1a08, 0x1c09, 0x1e09,
    0x2009, 0x2209, 0x2409, 0x2609, 0x2809, 0x2a09, 0x2c09, 0x2e09,
    0x3009, 0x3209, 0x3409, 0x3609, 0x3809, 0x3a09, 0x3c09, 0x3e09,
    0x4009, 0x4209, 0x4409, 0x4609, 0x4809, 0x4a09, 0x4c09, 0x4e09,
    0x5009, 0x5209, 0x5409, 0x5609, 0x5809, 0x5a09, 0x0012};
static const uint8_t g_table6_1_outer[64] = {
    0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 46, 46, 46};
static const uint8_t g_table6_2_emit[72] = {
    0x23, 0x30, 0x23, 0x31, 0x23, 0x32, 0x23, 0x61, 0x23, 0x63, 0x23, 0x65,
    0x23, 0x69, 0x23, 0x6f, 0x23, 0x73, 0x23, 0x74, 0x23, 0x20, 0x23, 0x25,
    0x23, 0x2d, 0x23, 0x2e, 0x23, 0x2f, 0x23, 0x33, 0x23, 0x34, 0x23, 0x35,
    0x23, 0x36, 0x23, 0x37, 0x23, 0x38, 0x23, 0x39, 0x23, 0x3d, 0x23, 0x41,
    0x23, 0x5f, 0x23, 0x62, 0x23, 0x64, 0x23, 0x66, 0x23, 0x67, 0x23, 0x68,
    0x23, 0x6c, 0x23, 0x6d, 0x23, 0x6e, 0x23, 0x70, 0x23, 0x72, 0x23, 0x75};
static const uint16_t g_table6_2_inner[37] = {
    0x0008, 0x0208, 0x0408, 0x0608, 0x0808, 0x0a08, 0x0c08, 0x0e08,
    0x1008, 0x1208, 0x1409, 0x1609, 0x1809, 0x1a09, 0x1c09, 0x1e09,
    0x2009, 0x2209, 0x2409, 0x2609, 0x2809, 0x2a09, 0x2c09, 0x2e09,
    0x3009, 0x3209, 0x3409, 0x3609, 0x3809, 0x3a09, 0x3c09, 0x3e09,
    0x4009, 0x4209, 0x4409, 0x4609, 0x0013};
static const uint8_t g_table6_2_outer[64] = {
    0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36};
static const uint8_t g_table6_3_emit[72] = {
    0x3e, 0x30, 0x3e, 0x31, 0x3e, 0x32, 0x3e, 0x61, 0x3e, 0x63, 0x3e, 0x65,
    0x3e, 0x69, 0x3e, 0x6f, 0x3e, 0x73, 0x3e, 0x74, 0x3e, 0x20, 0x3e, 0x25,
    0x3e, 0x2d, 0x3e, 0x2e, 0x3e, 0x2f, 0x3e, 0x33, 0x3e, 0x34, 0x3e, 0x35,
    0x3e, 0x36, 0x3e, 0x37, 0x3e, 0x38, 0x3e, 0x39, 0x3e, 0x3d, 0x3e, 0x41,
    0x3e, 0x5f, 0x3e, 0x62, 0x3e, 0x64, 0x3e, 0x66, 0x3e, 0x67, 0x3e, 0x68,
    0x3e, 0x6c, 0x3e, 0x6d, 0x3e, 0x6e, 0x3e, 0x70, 0x3e, 0x72, 0x3e, 0x75};
#define g_table6_3_inner g_table6_2_inner
#define g_table6_3_outer g_table6_2_outer
static const uint8_t g_table6_4_emit[40] = {
    0x00, 0x30, 0x00, 0x31, 0x00, 0x32, 0x00, 0x61, 0x00, 0x63,
    0x00, 0x65, 0x00, 0x69, 0x00, 0x6f, 0x00, 0x73, 0x00, 0x74,
    0x24, 0x30, 0x24, 0x31, 0x24, 0x32, 0x24, 0x61, 0x24, 0x63,
    0x24, 0x65, 0x24, 0x69, 0x24, 0x6f, 0x24, 0x73, 0x24, 0x74};
static const uint16_t g_table6_4_inner[22] = {
    0x0009, 0x0209, 0x0409, 0x0609, 0x0809, 0x0a09, 0x0c09, 0x0e09,
    0x1009, 0x1209, 0x0014, 0x1409, 0x1609, 0x1809, 0x1a09, 0x1c09,
    0x1e09, 0x2009, 0x2209, 0x2409, 0x2609, 0x1414};
static const uint8_t g_table6_4_outer[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21};
static const uint8_t g_table6_5_emit[40] = {
    0x40, 0x30, 0x40, 0x31, 0x40, 0x32, 0x40, 0x61, 0x40, 0x63,
    0x40, 0x65, 0x40, 0x69, 0x40, 0x6f, 0x40, 0x73, 0x40, 0x74,
    0x5b, 0x30, 0x5b, 0x31, 0x5b, 0x32, 0x5b, 0x61, 0x5b, 0x63,
    0x5b, 0x65, 0x5b, 0x69, 0x5b, 0x6f, 0x5b, 0x73, 0x5b, 0x74};
#define g_table6_5_inner g_table6_4_inner
#define g_table6_5_outer g_table6_4_outer
static const uint8_t g_table6_6_emit[40] = {
    0x5d, 0x30, 0x5d, 0x31, 0x5d, 0x32, 0x5d, 0x61, 0x5d, 0x63,
    0x5d, 0x65, 0x5d, 0x69, 0x5d, 0x6f, 0x5d, 0x73, 0x5d, 0x74,
    0x7e, 0x30, 0x7e, 0x31, 0x7e, 0x32, 0x7e, 0x61, 0x7e, 0x63,
    0x7e, 0x65, 0x7e, 0x69, 0x7e, 0x6f, 0x7e, 0x73, 0x7e, 0x74};
#define g_table6_6_inner g_table6_4_inner
#define g_table6_6_outer g_table6_4_outer
static const uint8_t g_table6_7_emit[5] = {0x5e, 0x7d, 0x3c, 0x60, 0x7b};
static const uint16_t g_table6_7_inner[13] = {
    0x0015, 0x0115, 0x0216, 0x0316, 0x0416, 0x0029, 0x0039,
    0x0049, 0x0059, 0x0069, 0x0079, 0x0089, 0x0099};
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
static const uint8_t g_table9_0_ops[8] = {0x03, 0x07, 0x0b, 0x0f,
                                          0x13, 0x17, 0x1b, 0x1f};
static const uint8_t* const g_table9_emit[] = {
    g_table9_0_emit,
};
static const uint8_t* const g_table9_ops[] = {
    g_table9_0_ops,
};
inline uint64_t GetOp9(size_t i) { return g_table9_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit9(size_t i, size_t emit) {
  return g_table9_emit[i >> 3][emit];
}
static const uint8_t g_table10_0_emit[3] = {0xd0, 0x80, 0x82};
#define g_table10_0_ops g_table5_0_ops
static const uint8_t* const g_table10_emit[] = {
    g_table10_0_emit,
};
static const uint8_t* const g_table10_ops[] = {
    g_table10_0_ops,
};
inline uint64_t GetOp10(size_t i) { return g_table10_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit10(size_t i, size_t emit) {
  return g_table10_emit[i >> 2][emit];
}
static const uint8_t g_table11_0_emit[15] = {0xe6, 0x81, 0x84, 0x85, 0x86,
                                             0x88, 0x92, 0x9a, 0x9c, 0xa0,
                                             0xa3, 0xa4, 0xa9, 0xaa, 0xad};
static const uint8_t g_table11_0_ops[16] = {0x03, 0x03, 0x0c, 0x14, 0x1c, 0x24,
                                            0x2c, 0x34, 0x3c, 0x44, 0x4c, 0x54,
                                            0x5c, 0x64, 0x6c, 0x74};
static const uint8_t* const g_table11_emit[] = {
    g_table11_0_emit,
};
static const uint8_t* const g_table11_ops[] = {
    g_table11_0_ops,
};
inline uint64_t GetOp11(size_t i) { return g_table11_ops[i >> 4][i & 0xf]; }
inline uint64_t GetEmit11(size_t i, size_t emit) {
  return g_table11_emit[i >> 4][emit];
}
static const uint8_t g_table12_0_emit[6] = {0xe0, 0xe2, 0x99, 0xa1, 0xa7, 0xac};
static const uint8_t g_table12_0_ops[8] = {0x02, 0x02, 0x06, 0x06,
                                           0x0b, 0x0f, 0x13, 0x17};
static const uint8_t* const g_table12_emit[] = {
    g_table12_0_emit,
};
static const uint8_t* const g_table12_ops[] = {
    g_table12_0_ops,
};
inline uint64_t GetOp12(size_t i) { return g_table12_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit12(size_t i, size_t emit) {
  return g_table12_emit[i >> 3][emit];
}
static const uint8_t g_table13_0_emit[20] = {
    0xb2, 0xb5, 0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4,
    0xe8, 0xe9, 0x01, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8f};
static const uint8_t g_table13_0_ops[32] = {
    0x04, 0x04, 0x0c, 0x0c, 0x14, 0x14, 0x1c, 0x1c, 0x24, 0x24, 0x2c,
    0x2c, 0x34, 0x34, 0x3c, 0x3c, 0x44, 0x44, 0x4c, 0x4c, 0x54, 0x54,
    0x5c, 0x5c, 0x65, 0x6d, 0x75, 0x7d, 0x85, 0x8d, 0x95, 0x9d};
static const uint8_t* const g_table13_emit[] = {
    g_table13_0_emit,
};
static const uint8_t* const g_table13_ops[] = {
    g_table13_0_ops,
};
inline uint64_t GetOp13(size_t i) { return g_table13_ops[i >> 5][i & 0x1f]; }
inline uint64_t GetEmit13(size_t i, size_t emit) {
  return g_table13_emit[i >> 5][emit];
}
static const uint8_t g_table14_0_emit[4] = {0x93, 0x95, 0x96, 0x97};
static const uint16_t g_table14_0_inner[4] = {0x0005, 0x0105, 0x0205, 0x0305};
static const uint8_t g_table14_0_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
static const uint8_t g_table14_1_emit[4] = {0x98, 0x9b, 0x9d, 0x9e};
#define g_table14_1_inner g_table14_0_inner
#define g_table14_1_outer g_table14_0_outer
static const uint8_t g_table14_2_emit[4] = {0xa5, 0xa6, 0xa8, 0xae};
#define g_table14_2_inner g_table14_0_inner
#define g_table14_2_outer g_table14_0_outer
static const uint8_t g_table14_3_emit[4] = {0xaf, 0xb4, 0xb6, 0xb7};
#define g_table14_3_inner g_table14_0_inner
#define g_table14_3_outer g_table14_0_outer
static const uint8_t g_table14_4_emit[4] = {0xbc, 0xbf, 0xc5, 0xe7};
#define g_table14_4_inner g_table14_0_inner
#define g_table14_4_outer g_table14_0_outer
static const uint8_t g_table14_5_emit[7] = {0xef, 0x09, 0x8e, 0x90,
                                            0x91, 0x94, 0x9f};
static const uint16_t g_table14_5_inner[7] = {0x0005, 0x0106, 0x0206, 0x0306,
                                              0x0406, 0x0506, 0x0606};
static const uint8_t g_table14_5_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
    4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6};
static const uint8_t g_table14_6_emit[10] = {0xab, 0xce, 0xd7, 0xe1, 0xec,
                                             0xed, 0xc7, 0xcf, 0xea, 0xeb};
static const uint16_t g_table14_6_inner[10] = {0x0006, 0x0106, 0x0206, 0x0306,
                                               0x0406, 0x0506, 0x0607, 0x0707,
                                               0x0807, 0x0907};
static const uint8_t g_table14_6_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9};
static const uint8_t g_table14_7_emit[34] = {
    0xc0, 0xc1, 0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda, 0xdb, 0xee, 0xf0,
    0xf2, 0xf3, 0xff, 0xcb, 0xcc, 0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf, 0xf1,
    0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe};
static const uint16_t g_table14_7_inner[49] = {
    0x0008, 0x0108, 0x0208, 0x0308, 0x0408, 0x0508, 0x0608, 0x0708, 0x0808,
    0x0908, 0x0a08, 0x0b08, 0x0c08, 0x0d08, 0x0e08, 0x0f09, 0x1009, 0x1109,
    0x1209, 0x1309, 0x1409, 0x1509, 0x1609, 0x1709, 0x1809, 0x1909, 0x1a09,
    0x1b09, 0x1c09, 0x1d09, 0x1e09, 0x1f09, 0x2009, 0x2109, 0x0019, 0x0029,
    0x0039, 0x0049, 0x0059, 0x0069, 0x0079, 0x0089, 0x0099, 0x00a9, 0x00b9,
    0x00c9, 0x00d9, 0x00e9, 0x00f9};
static const uint8_t g_table14_7_outer[64] = {
    0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48};
static const uint8_t* const g_table14_emit[] = {
    g_table14_0_emit, g_table14_1_emit, g_table14_2_emit, g_table14_3_emit,
    g_table14_4_emit, g_table14_5_emit, g_table14_6_emit, g_table14_7_emit,
};
static const uint16_t* const g_table14_inner[] = {
    g_table14_0_inner, g_table14_1_inner, g_table14_2_inner, g_table14_3_inner,
    g_table14_4_inner, g_table14_5_inner, g_table14_6_inner, g_table14_7_inner,
};
static const uint8_t* const g_table14_outer[] = {
    g_table14_0_outer, g_table14_1_outer, g_table14_2_outer, g_table14_3_outer,
    g_table14_4_outer, g_table14_5_outer, g_table14_6_outer, g_table14_7_outer,
};
inline uint64_t GetOp14(size_t i) {
  return g_table14_inner[i >> 6][g_table14_outer[i >> 6][i & 0x3f]];
}
inline uint64_t GetEmit14(size_t i, size_t emit) {
  return g_table14_emit[i >> 6][emit];
}
static const uint8_t g_table15_0_emit[2] = {0x02, 0x03};
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
static const uint8_t g_table16_0_emit[2] = {0x04, 0x05};
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
static const uint8_t g_table17_0_emit[2] = {0x06, 0x07};
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
static const uint8_t g_table18_0_emit[2] = {0x08, 0x0b};
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
static const uint8_t g_table19_0_emit[2] = {0x0c, 0x0e};
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
static const uint8_t g_table20_0_emit[2] = {0x0f, 0x10};
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
static const uint8_t g_table21_0_emit[2] = {0x11, 0x12};
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
static const uint8_t g_table22_0_emit[2] = {0x13, 0x14};
#define g_table22_0_ops g_table3_0_ops
static const uint8_t* const g_table22_emit[] = {
    g_table22_0_emit,
};
static const uint8_t* const g_table22_ops[] = {
    g_table22_0_ops,
};
inline uint64_t GetOp22(size_t i) { return g_table22_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit22(size_t i, size_t emit) {
  return g_table22_emit[i >> 1][emit];
}
static const uint8_t g_table23_0_emit[2] = {0x15, 0x17};
#define g_table23_0_ops g_table3_0_ops
static const uint8_t* const g_table23_emit[] = {
    g_table23_0_emit,
};
static const uint8_t* const g_table23_ops[] = {
    g_table23_0_ops,
};
inline uint64_t GetOp23(size_t i) { return g_table23_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit23(size_t i, size_t emit) {
  return g_table23_emit[i >> 1][emit];
}
static const uint8_t g_table24_0_emit[2] = {0x18, 0x19};
#define g_table24_0_ops g_table3_0_ops
static const uint8_t* const g_table24_emit[] = {
    g_table24_0_emit,
};
static const uint8_t* const g_table24_ops[] = {
    g_table24_0_ops,
};
inline uint64_t GetOp24(size_t i) { return g_table24_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit24(size_t i, size_t emit) {
  return g_table24_emit[i >> 1][emit];
}
static const uint8_t g_table25_0_emit[2] = {0x1a, 0x1b};
#define g_table25_0_ops g_table3_0_ops
static const uint8_t* const g_table25_emit[] = {
    g_table25_0_emit,
};
static const uint8_t* const g_table25_ops[] = {
    g_table25_0_ops,
};
inline uint64_t GetOp25(size_t i) { return g_table25_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit25(size_t i, size_t emit) {
  return g_table25_emit[i >> 1][emit];
}
static const uint8_t g_table26_0_emit[2] = {0x1c, 0x1d};
#define g_table26_0_ops g_table3_0_ops
static const uint8_t* const g_table26_emit[] = {
    g_table26_0_emit,
};
static const uint8_t* const g_table26_ops[] = {
    g_table26_0_ops,
};
inline uint64_t GetOp26(size_t i) { return g_table26_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit26(size_t i, size_t emit) {
  return g_table26_emit[i >> 1][emit];
}
static const uint8_t g_table27_0_emit[2] = {0x1e, 0x1f};
#define g_table27_0_ops g_table3_0_ops
static const uint8_t* const g_table27_emit[] = {
    g_table27_0_emit,
};
static const uint8_t* const g_table27_ops[] = {
    g_table27_0_ops,
};
inline uint64_t GetOp27(size_t i) { return g_table27_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit27(size_t i, size_t emit) {
  return g_table27_emit[i >> 1][emit];
}
static const uint8_t g_table28_0_emit[2] = {0x7f, 0xdc};
#define g_table28_0_ops g_table3_0_ops
static const uint8_t* const g_table28_emit[] = {
    g_table28_0_emit,
};
static const uint8_t* const g_table28_ops[] = {
    g_table28_0_ops,
};
inline uint64_t GetOp28(size_t i) { return g_table28_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit28(size_t i, size_t emit) {
  return g_table28_emit[i >> 1][emit];
}
static const uint8_t g_table29_0_emit[4] = {0xf9, 0x0a, 0x0d, 0x16};
static const uint8_t g_table29_0_ops[8] = {0x01, 0x01, 0x01, 0x01,
                                           0x0b, 0x13, 0x1b, 0x07};
static const uint8_t* const g_table29_emit[] = {
    g_table29_0_emit,
};
static const uint8_t* const g_table29_ops[] = {
    g_table29_0_ops,
};
inline uint64_t GetOp29(size_t i) { return g_table29_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit29(size_t i, size_t emit) {
  return g_table29_emit[i >> 3][emit];
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
        sink_(GetEmit6(index, emit_ofs + 0));
        break;
      }
      case 0: {
        sink_(GetEmit6(index, emit_ofs + 0));
        sink_(GetEmit6(index, emit_ofs + 1));
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
    if (!RefillTo9()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
    const auto op = GetOp14(index);
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
        sink_(GetEmit14(index, emit_ofs + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp22(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit22(index, emit_ofs + 0));
  }
  void DecodeStep20() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp23(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit23(index, emit_ofs + 0));
  }
  void DecodeStep21() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp24(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit24(index, emit_ofs + 0));
  }
  void DecodeStep22() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp25(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit25(index, emit_ofs + 0));
  }
  void DecodeStep23() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp26(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit26(index, emit_ofs + 0));
  }
  void DecodeStep24() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp27(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit27(index, emit_ofs + 0));
  }
  void DecodeStep25() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp28(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit28(index, emit_ofs + 0));
  }
  void DecodeStep26() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetOp29(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 3;
    switch ((op >> 2) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit29(index, emit_ofs + 0));
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
