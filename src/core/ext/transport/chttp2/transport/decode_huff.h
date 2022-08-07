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
static const uint16_t g_table1_0_inner[78] = {
    0x0005, 0x0085, 0x0105, 0x0185, 0x0205, 0x0285, 0x0305, 0x0385, 0x0405,
    0x0485, 0x0506, 0x0586, 0x0606, 0x0686, 0x0706, 0x0786, 0x0806, 0x0886,
    0x0906, 0x0986, 0x0a06, 0x0a86, 0x0b06, 0x0b86, 0x0c06, 0x0c86, 0x0d06,
    0x0d86, 0x0e06, 0x0e86, 0x0f06, 0x0f86, 0x1006, 0x1086, 0x1106, 0x1186,
    0x1207, 0x1287, 0x1307, 0x1387, 0x1407, 0x1487, 0x1507, 0x1587, 0x1607,
    0x1687, 0x1707, 0x1787, 0x1807, 0x1887, 0x1907, 0x1987, 0x1a07, 0x1a87,
    0x1b07, 0x1b87, 0x1c07, 0x1c87, 0x1d07, 0x1d87, 0x1e07, 0x1e87, 0x1f07,
    0x1f87, 0x2007, 0x2087, 0x2107, 0x2187, 0x2208, 0x2288, 0x2308, 0x2388,
    0x2408, 0x2488, 0x0019, 0x0029, 0x0039, 0x0049};
static const uint8_t g_table1_0_outer[512] = {
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
inline uint64_t GetOp1(size_t i) {
  return g_table1_0_inner[g_table1_0_outer[i]];
}
inline uint64_t GetEmit1(size_t, size_t emit) { return g_table1_0_emit[emit]; }
static const uint8_t g_table2_0_emit[36] = {
    0x30, 0x30, 0x31, 0x30, 0x32, 0x30, 0x61, 0x30, 0x63, 0x30, 0x65, 0x30,
    0x69, 0x30, 0x6f, 0x30, 0x73, 0x30, 0x74, 0x31, 0x31, 0x32, 0x31, 0x61,
    0x31, 0x63, 0x31, 0x65, 0x31, 0x69, 0x31, 0x6f, 0x31, 0x73, 0x31, 0x74};
static const uint16_t g_table2_0_inner[22] = {
    0x000a, 0x008a, 0x018a, 0x028a, 0x038a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x0015, 0x010a, 0x098a, 0x0a0a, 0x0b0a, 0x0c0a,
    0x0d0a, 0x0e0a, 0x0f0a, 0x100a, 0x110a, 0x0115};
static const uint8_t g_table2_0_outer[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21};
static const uint8_t g_table2_1_emit[36] = {
    0x32, 0x30, 0x32, 0x31, 0x32, 0x32, 0x61, 0x32, 0x63, 0x32, 0x65, 0x32,
    0x69, 0x32, 0x6f, 0x32, 0x73, 0x32, 0x74, 0x61, 0x30, 0x61, 0x31, 0x61,
    0x61, 0x63, 0x61, 0x65, 0x61, 0x69, 0x61, 0x6f, 0x61, 0x73, 0x61, 0x74};
static const uint16_t g_table2_1_inner[22] = {
    0x000a, 0x010a, 0x020a, 0x028a, 0x038a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x0015, 0x098a, 0x0a8a, 0x030a, 0x0b8a, 0x0c0a,
    0x0d0a, 0x0e0a, 0x0f0a, 0x100a, 0x110a, 0x0315};
#define g_table2_1_outer g_table2_0_outer
static const uint8_t g_table2_2_emit[36] = {
    0x63, 0x30, 0x63, 0x31, 0x63, 0x32, 0x63, 0x61, 0x63, 0x63, 0x65, 0x63,
    0x69, 0x63, 0x6f, 0x63, 0x73, 0x63, 0x74, 0x65, 0x30, 0x65, 0x31, 0x65,
    0x32, 0x65, 0x61, 0x65, 0x65, 0x69, 0x65, 0x6f, 0x65, 0x73, 0x65, 0x74};
static const uint16_t g_table2_2_inner[22] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x0015, 0x098a, 0x0a8a, 0x0b8a, 0x0c8a, 0x050a,
    0x0d8a, 0x0e0a, 0x0f0a, 0x100a, 0x110a, 0x0515};
#define g_table2_2_outer g_table2_0_outer
static const uint8_t g_table2_3_emit[36] = {
    0x69, 0x30, 0x69, 0x31, 0x69, 0x32, 0x69, 0x61, 0x69, 0x63, 0x69, 0x65,
    0x69, 0x69, 0x6f, 0x69, 0x73, 0x69, 0x74, 0x6f, 0x30, 0x6f, 0x31, 0x6f,
    0x32, 0x6f, 0x61, 0x6f, 0x63, 0x6f, 0x65, 0x6f, 0x6f, 0x73, 0x6f, 0x74};
static const uint16_t g_table2_3_inner[22] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x068a,
    0x078a, 0x088a, 0x0015, 0x098a, 0x0a8a, 0x0b8a, 0x0c8a, 0x0d8a,
    0x0e8a, 0x070a, 0x0f8a, 0x100a, 0x110a, 0x0715};
#define g_table2_3_outer g_table2_0_outer
static const uint8_t g_table2_4_emit[38] = {
    0x73, 0x30, 0x73, 0x31, 0x73, 0x32, 0x73, 0x61, 0x73, 0x63,
    0x73, 0x65, 0x73, 0x69, 0x73, 0x6f, 0x73, 0x73, 0x74, 0x30,
    0x74, 0x31, 0x74, 0x32, 0x74, 0x61, 0x74, 0x63, 0x74, 0x65,
    0x74, 0x69, 0x74, 0x6f, 0x74, 0x73, 0x74, 0x74};
static const uint16_t g_table2_4_inner[22] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x070a,
    0x080a, 0x088a, 0x0015, 0x090a, 0x0a0a, 0x0b0a, 0x0c0a, 0x0d0a,
    0x0e0a, 0x0f0a, 0x100a, 0x110a, 0x120a, 0x0915};
#define g_table2_4_outer g_table2_0_outer
static const uint8_t g_table2_5_emit[4] = {0x20, 0x25, 0x2d, 0x2e};
static const uint16_t g_table2_5_inner[4] = {0x0016, 0x0096, 0x0116, 0x0196};
static const uint8_t g_table2_5_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
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
static const uint16_t g_table2_11_inner[6] = {0x0016, 0x0096, 0x0117,
                                              0x0197, 0x0217, 0x0297};
static const uint8_t g_table2_11_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
    3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5};
static const uint8_t g_table2_12_emit[8] = {0x45, 0x46, 0x47, 0x48,
                                            0x49, 0x4a, 0x4b, 0x4c};
static const uint16_t g_table2_12_inner[8] = {0x0017, 0x0097, 0x0117, 0x0197,
                                              0x0217, 0x0297, 0x0317, 0x0397};
static const uint8_t g_table2_12_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7};
static const uint8_t g_table2_13_emit[8] = {0x4d, 0x4e, 0x4f, 0x50,
                                            0x51, 0x52, 0x53, 0x54};
#define g_table2_13_inner g_table2_12_inner
#define g_table2_13_outer g_table2_12_outer
static const uint8_t g_table2_14_emit[8] = {0x55, 0x56, 0x57, 0x59,
                                            0x6a, 0x6b, 0x71, 0x76};
#define g_table2_14_inner g_table2_12_inner
#define g_table2_14_outer g_table2_12_outer
static const uint8_t g_table2_15_emit[15] = {0x77, 0x78, 0x79, 0x7a, 0x26,
                                             0x2a, 0x2c, 0x3b, 0x58, 0x5a,
                                             0x21, 0x22, 0x28, 0x29, 0x3f};
static const uint16_t g_table2_15_inner[18] = {
    0x0017, 0x0097, 0x0117, 0x0197, 0x0218, 0x0298, 0x0318, 0x0398, 0x0418,
    0x0498, 0x051a, 0x059a, 0x061a, 0x069a, 0x071a, 0x002a, 0x003a, 0x004a};
static const uint8_t g_table2_15_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,  1,  1,  1,  2,  2,  2,  2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  6,  6, 6, 6,
    7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 11, 12, 13, 14, 15, 16, 17};
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
  return g_table2_inner[i >> 6][g_table2_outer[i >> 6][i & 0x3f]];
}
inline uint64_t GetEmit2(size_t i, size_t emit) {
  return g_table2_emit[i >> 6][emit];
}
static const uint8_t g_table3_0_emit[2] = {0x27, 0x2b};
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
static const uint8_t g_table4_0_emit[1] = {0x00};
static const uint16_t g_table4_0_ops[1024] = {
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
    0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003};
#define g_table4_1_emit g_table4_0_emit
#define g_table4_1_ops g_table4_0_ops
#define g_table4_2_emit g_table4_0_emit
#define g_table4_2_ops g_table4_0_ops
#define g_table4_3_emit g_table4_0_emit
#define g_table4_3_ops g_table4_0_ops
#define g_table4_4_emit g_table4_0_emit
#define g_table4_4_ops g_table4_0_ops
#define g_table4_5_emit g_table4_0_emit
#define g_table4_5_ops g_table4_0_ops
#define g_table4_6_emit g_table4_0_emit
#define g_table4_6_ops g_table4_0_ops
#define g_table4_7_emit g_table4_0_emit
#define g_table4_7_ops g_table4_0_ops
#define g_table4_8_emit g_table4_0_emit
#define g_table4_8_ops g_table4_0_ops
#define g_table4_9_emit g_table4_0_emit
#define g_table4_9_ops g_table4_0_ops
#define g_table4_10_emit g_table4_0_emit
#define g_table4_10_ops g_table4_0_ops
#define g_table4_11_emit g_table4_0_emit
#define g_table4_11_ops g_table4_0_ops
#define g_table4_12_emit g_table4_0_emit
#define g_table4_12_ops g_table4_0_ops
#define g_table4_13_emit g_table4_0_emit
#define g_table4_13_ops g_table4_0_ops
#define g_table4_14_emit g_table4_0_emit
#define g_table4_14_ops g_table4_0_ops
#define g_table4_15_emit g_table4_0_emit
#define g_table4_15_ops g_table4_0_ops
#define g_table4_16_emit g_table4_0_emit
#define g_table4_16_ops g_table4_0_ops
#define g_table4_17_emit g_table4_0_emit
#define g_table4_17_ops g_table4_0_ops
#define g_table4_18_emit g_table4_0_emit
#define g_table4_18_ops g_table4_0_ops
#define g_table4_19_emit g_table4_0_emit
#define g_table4_19_ops g_table4_0_ops
#define g_table4_20_emit g_table4_0_emit
#define g_table4_20_ops g_table4_0_ops
#define g_table4_21_emit g_table4_0_emit
#define g_table4_21_ops g_table4_0_ops
#define g_table4_22_emit g_table4_0_emit
#define g_table4_22_ops g_table4_0_ops
#define g_table4_23_emit g_table4_0_emit
#define g_table4_23_ops g_table4_0_ops
#define g_table4_24_emit g_table4_0_emit
#define g_table4_24_ops g_table4_0_ops
#define g_table4_25_emit g_table4_0_emit
#define g_table4_25_ops g_table4_0_ops
#define g_table4_26_emit g_table4_0_emit
#define g_table4_26_ops g_table4_0_ops
#define g_table4_27_emit g_table4_0_emit
#define g_table4_27_ops g_table4_0_ops
#define g_table4_28_emit g_table4_0_emit
#define g_table4_28_ops g_table4_0_ops
#define g_table4_29_emit g_table4_0_emit
#define g_table4_29_ops g_table4_0_ops
#define g_table4_30_emit g_table4_0_emit
#define g_table4_30_ops g_table4_0_ops
#define g_table4_31_emit g_table4_0_emit
#define g_table4_31_ops g_table4_0_ops
#define g_table4_32_emit g_table4_0_emit
#define g_table4_32_ops g_table4_0_ops
#define g_table4_33_emit g_table4_0_emit
#define g_table4_33_ops g_table4_0_ops
#define g_table4_34_emit g_table4_0_emit
#define g_table4_34_ops g_table4_0_ops
#define g_table4_35_emit g_table4_0_emit
#define g_table4_35_ops g_table4_0_ops
#define g_table4_36_emit g_table4_0_emit
#define g_table4_36_ops g_table4_0_ops
#define g_table4_37_emit g_table4_0_emit
#define g_table4_37_ops g_table4_0_ops
#define g_table4_38_emit g_table4_0_emit
#define g_table4_38_ops g_table4_0_ops
#define g_table4_39_emit g_table4_0_emit
#define g_table4_39_ops g_table4_0_ops
#define g_table4_40_emit g_table4_0_emit
#define g_table4_40_ops g_table4_0_ops
#define g_table4_41_emit g_table4_0_emit
#define g_table4_41_ops g_table4_0_ops
#define g_table4_42_emit g_table4_0_emit
#define g_table4_42_ops g_table4_0_ops
#define g_table4_43_emit g_table4_0_emit
#define g_table4_43_ops g_table4_0_ops
#define g_table4_44_emit g_table4_0_emit
#define g_table4_44_ops g_table4_0_ops
#define g_table4_45_emit g_table4_0_emit
#define g_table4_45_ops g_table4_0_ops
#define g_table4_46_emit g_table4_0_emit
#define g_table4_46_ops g_table4_0_ops
#define g_table4_47_emit g_table4_0_emit
#define g_table4_47_ops g_table4_0_ops
#define g_table4_48_emit g_table4_0_emit
#define g_table4_48_ops g_table4_0_ops
#define g_table4_49_emit g_table4_0_emit
#define g_table4_49_ops g_table4_0_ops
#define g_table4_50_emit g_table4_0_emit
#define g_table4_50_ops g_table4_0_ops
#define g_table4_51_emit g_table4_0_emit
#define g_table4_51_ops g_table4_0_ops
#define g_table4_52_emit g_table4_0_emit
#define g_table4_52_ops g_table4_0_ops
#define g_table4_53_emit g_table4_0_emit
#define g_table4_53_ops g_table4_0_ops
#define g_table4_54_emit g_table4_0_emit
#define g_table4_54_ops g_table4_0_ops
#define g_table4_55_emit g_table4_0_emit
#define g_table4_55_ops g_table4_0_ops
#define g_table4_56_emit g_table4_0_emit
#define g_table4_56_ops g_table4_0_ops
#define g_table4_57_emit g_table4_0_emit
#define g_table4_57_ops g_table4_0_ops
#define g_table4_58_emit g_table4_0_emit
#define g_table4_58_ops g_table4_0_ops
#define g_table4_59_emit g_table4_0_emit
#define g_table4_59_ops g_table4_0_ops
#define g_table4_60_emit g_table4_0_emit
#define g_table4_60_ops g_table4_0_ops
#define g_table4_61_emit g_table4_0_emit
#define g_table4_61_ops g_table4_0_ops
#define g_table4_62_emit g_table4_0_emit
#define g_table4_62_ops g_table4_0_ops
#define g_table4_63_emit g_table4_0_emit
#define g_table4_63_ops g_table4_0_ops
#define g_table4_64_emit g_table4_0_emit
#define g_table4_64_ops g_table4_0_ops
#define g_table4_65_emit g_table4_0_emit
#define g_table4_65_ops g_table4_0_ops
#define g_table4_66_emit g_table4_0_emit
#define g_table4_66_ops g_table4_0_ops
#define g_table4_67_emit g_table4_0_emit
#define g_table4_67_ops g_table4_0_ops
#define g_table4_68_emit g_table4_0_emit
#define g_table4_68_ops g_table4_0_ops
#define g_table4_69_emit g_table4_0_emit
#define g_table4_69_ops g_table4_0_ops
#define g_table4_70_emit g_table4_0_emit
#define g_table4_70_ops g_table4_0_ops
#define g_table4_71_emit g_table4_0_emit
#define g_table4_71_ops g_table4_0_ops
#define g_table4_72_emit g_table4_0_emit
#define g_table4_72_ops g_table4_0_ops
#define g_table4_73_emit g_table4_0_emit
#define g_table4_73_ops g_table4_0_ops
#define g_table4_74_emit g_table4_0_emit
#define g_table4_74_ops g_table4_0_ops
#define g_table4_75_emit g_table4_0_emit
#define g_table4_75_ops g_table4_0_ops
#define g_table4_76_emit g_table4_0_emit
#define g_table4_76_ops g_table4_0_ops
#define g_table4_77_emit g_table4_0_emit
#define g_table4_77_ops g_table4_0_ops
#define g_table4_78_emit g_table4_0_emit
#define g_table4_78_ops g_table4_0_ops
#define g_table4_79_emit g_table4_0_emit
#define g_table4_79_ops g_table4_0_ops
#define g_table4_80_emit g_table4_0_emit
#define g_table4_80_ops g_table4_0_ops
#define g_table4_81_emit g_table4_0_emit
#define g_table4_81_ops g_table4_0_ops
#define g_table4_82_emit g_table4_0_emit
#define g_table4_82_ops g_table4_0_ops
#define g_table4_83_emit g_table4_0_emit
#define g_table4_83_ops g_table4_0_ops
#define g_table4_84_emit g_table4_0_emit
#define g_table4_84_ops g_table4_0_ops
#define g_table4_85_emit g_table4_0_emit
#define g_table4_85_ops g_table4_0_ops
#define g_table4_86_emit g_table4_0_emit
#define g_table4_86_ops g_table4_0_ops
#define g_table4_87_emit g_table4_0_emit
#define g_table4_87_ops g_table4_0_ops
#define g_table4_88_emit g_table4_0_emit
#define g_table4_88_ops g_table4_0_ops
#define g_table4_89_emit g_table4_0_emit
#define g_table4_89_ops g_table4_0_ops
#define g_table4_90_emit g_table4_0_emit
#define g_table4_90_ops g_table4_0_ops
#define g_table4_91_emit g_table4_0_emit
#define g_table4_91_ops g_table4_0_ops
#define g_table4_92_emit g_table4_0_emit
#define g_table4_92_ops g_table4_0_ops
#define g_table4_93_emit g_table4_0_emit
#define g_table4_93_ops g_table4_0_ops
#define g_table4_94_emit g_table4_0_emit
#define g_table4_94_ops g_table4_0_ops
#define g_table4_95_emit g_table4_0_emit
#define g_table4_95_ops g_table4_0_ops
#define g_table4_96_emit g_table4_0_emit
#define g_table4_96_ops g_table4_0_ops
#define g_table4_97_emit g_table4_0_emit
#define g_table4_97_ops g_table4_0_ops
#define g_table4_98_emit g_table4_0_emit
#define g_table4_98_ops g_table4_0_ops
#define g_table4_99_emit g_table4_0_emit
#define g_table4_99_ops g_table4_0_ops
#define g_table4_100_emit g_table4_0_emit
#define g_table4_100_ops g_table4_0_ops
#define g_table4_101_emit g_table4_0_emit
#define g_table4_101_ops g_table4_0_ops
#define g_table4_102_emit g_table4_0_emit
#define g_table4_102_ops g_table4_0_ops
#define g_table4_103_emit g_table4_0_emit
#define g_table4_103_ops g_table4_0_ops
#define g_table4_104_emit g_table4_0_emit
#define g_table4_104_ops g_table4_0_ops
#define g_table4_105_emit g_table4_0_emit
#define g_table4_105_ops g_table4_0_ops
#define g_table4_106_emit g_table4_0_emit
#define g_table4_106_ops g_table4_0_ops
#define g_table4_107_emit g_table4_0_emit
#define g_table4_107_ops g_table4_0_ops
#define g_table4_108_emit g_table4_0_emit
#define g_table4_108_ops g_table4_0_ops
#define g_table4_109_emit g_table4_0_emit
#define g_table4_109_ops g_table4_0_ops
#define g_table4_110_emit g_table4_0_emit
#define g_table4_110_ops g_table4_0_ops
#define g_table4_111_emit g_table4_0_emit
#define g_table4_111_ops g_table4_0_ops
#define g_table4_112_emit g_table4_0_emit
#define g_table4_112_ops g_table4_0_ops
#define g_table4_113_emit g_table4_0_emit
#define g_table4_113_ops g_table4_0_ops
#define g_table4_114_emit g_table4_0_emit
#define g_table4_114_ops g_table4_0_ops
#define g_table4_115_emit g_table4_0_emit
#define g_table4_115_ops g_table4_0_ops
#define g_table4_116_emit g_table4_0_emit
#define g_table4_116_ops g_table4_0_ops
#define g_table4_117_emit g_table4_0_emit
#define g_table4_117_ops g_table4_0_ops
#define g_table4_118_emit g_table4_0_emit
#define g_table4_118_ops g_table4_0_ops
#define g_table4_119_emit g_table4_0_emit
#define g_table4_119_ops g_table4_0_ops
#define g_table4_120_emit g_table4_0_emit
#define g_table4_120_ops g_table4_0_ops
#define g_table4_121_emit g_table4_0_emit
#define g_table4_121_ops g_table4_0_ops
#define g_table4_122_emit g_table4_0_emit
#define g_table4_122_ops g_table4_0_ops
#define g_table4_123_emit g_table4_0_emit
#define g_table4_123_ops g_table4_0_ops
#define g_table4_124_emit g_table4_0_emit
#define g_table4_124_ops g_table4_0_ops
#define g_table4_125_emit g_table4_0_emit
#define g_table4_125_ops g_table4_0_ops
#define g_table4_126_emit g_table4_0_emit
#define g_table4_126_ops g_table4_0_ops
#define g_table4_127_emit g_table4_0_emit
#define g_table4_127_ops g_table4_0_ops
static const uint8_t g_table4_128_emit[1] = {0x24};
#define g_table4_128_ops g_table4_0_ops
#define g_table4_129_emit g_table4_128_emit
#define g_table4_129_ops g_table4_0_ops
#define g_table4_130_emit g_table4_128_emit
#define g_table4_130_ops g_table4_0_ops
#define g_table4_131_emit g_table4_128_emit
#define g_table4_131_ops g_table4_0_ops
#define g_table4_132_emit g_table4_128_emit
#define g_table4_132_ops g_table4_0_ops
#define g_table4_133_emit g_table4_128_emit
#define g_table4_133_ops g_table4_0_ops
#define g_table4_134_emit g_table4_128_emit
#define g_table4_134_ops g_table4_0_ops
#define g_table4_135_emit g_table4_128_emit
#define g_table4_135_ops g_table4_0_ops
#define g_table4_136_emit g_table4_128_emit
#define g_table4_136_ops g_table4_0_ops
#define g_table4_137_emit g_table4_128_emit
#define g_table4_137_ops g_table4_0_ops
#define g_table4_138_emit g_table4_128_emit
#define g_table4_138_ops g_table4_0_ops
#define g_table4_139_emit g_table4_128_emit
#define g_table4_139_ops g_table4_0_ops
#define g_table4_140_emit g_table4_128_emit
#define g_table4_140_ops g_table4_0_ops
#define g_table4_141_emit g_table4_128_emit
#define g_table4_141_ops g_table4_0_ops
#define g_table4_142_emit g_table4_128_emit
#define g_table4_142_ops g_table4_0_ops
#define g_table4_143_emit g_table4_128_emit
#define g_table4_143_ops g_table4_0_ops
#define g_table4_144_emit g_table4_128_emit
#define g_table4_144_ops g_table4_0_ops
#define g_table4_145_emit g_table4_128_emit
#define g_table4_145_ops g_table4_0_ops
#define g_table4_146_emit g_table4_128_emit
#define g_table4_146_ops g_table4_0_ops
#define g_table4_147_emit g_table4_128_emit
#define g_table4_147_ops g_table4_0_ops
#define g_table4_148_emit g_table4_128_emit
#define g_table4_148_ops g_table4_0_ops
#define g_table4_149_emit g_table4_128_emit
#define g_table4_149_ops g_table4_0_ops
#define g_table4_150_emit g_table4_128_emit
#define g_table4_150_ops g_table4_0_ops
#define g_table4_151_emit g_table4_128_emit
#define g_table4_151_ops g_table4_0_ops
#define g_table4_152_emit g_table4_128_emit
#define g_table4_152_ops g_table4_0_ops
#define g_table4_153_emit g_table4_128_emit
#define g_table4_153_ops g_table4_0_ops
#define g_table4_154_emit g_table4_128_emit
#define g_table4_154_ops g_table4_0_ops
#define g_table4_155_emit g_table4_128_emit
#define g_table4_155_ops g_table4_0_ops
#define g_table4_156_emit g_table4_128_emit
#define g_table4_156_ops g_table4_0_ops
#define g_table4_157_emit g_table4_128_emit
#define g_table4_157_ops g_table4_0_ops
#define g_table4_158_emit g_table4_128_emit
#define g_table4_158_ops g_table4_0_ops
#define g_table4_159_emit g_table4_128_emit
#define g_table4_159_ops g_table4_0_ops
#define g_table4_160_emit g_table4_128_emit
#define g_table4_160_ops g_table4_0_ops
#define g_table4_161_emit g_table4_128_emit
#define g_table4_161_ops g_table4_0_ops
#define g_table4_162_emit g_table4_128_emit
#define g_table4_162_ops g_table4_0_ops
#define g_table4_163_emit g_table4_128_emit
#define g_table4_163_ops g_table4_0_ops
#define g_table4_164_emit g_table4_128_emit
#define g_table4_164_ops g_table4_0_ops
#define g_table4_165_emit g_table4_128_emit
#define g_table4_165_ops g_table4_0_ops
#define g_table4_166_emit g_table4_128_emit
#define g_table4_166_ops g_table4_0_ops
#define g_table4_167_emit g_table4_128_emit
#define g_table4_167_ops g_table4_0_ops
#define g_table4_168_emit g_table4_128_emit
#define g_table4_168_ops g_table4_0_ops
#define g_table4_169_emit g_table4_128_emit
#define g_table4_169_ops g_table4_0_ops
#define g_table4_170_emit g_table4_128_emit
#define g_table4_170_ops g_table4_0_ops
#define g_table4_171_emit g_table4_128_emit
#define g_table4_171_ops g_table4_0_ops
#define g_table4_172_emit g_table4_128_emit
#define g_table4_172_ops g_table4_0_ops
#define g_table4_173_emit g_table4_128_emit
#define g_table4_173_ops g_table4_0_ops
#define g_table4_174_emit g_table4_128_emit
#define g_table4_174_ops g_table4_0_ops
#define g_table4_175_emit g_table4_128_emit
#define g_table4_175_ops g_table4_0_ops
#define g_table4_176_emit g_table4_128_emit
#define g_table4_176_ops g_table4_0_ops
#define g_table4_177_emit g_table4_128_emit
#define g_table4_177_ops g_table4_0_ops
#define g_table4_178_emit g_table4_128_emit
#define g_table4_178_ops g_table4_0_ops
#define g_table4_179_emit g_table4_128_emit
#define g_table4_179_ops g_table4_0_ops
#define g_table4_180_emit g_table4_128_emit
#define g_table4_180_ops g_table4_0_ops
#define g_table4_181_emit g_table4_128_emit
#define g_table4_181_ops g_table4_0_ops
#define g_table4_182_emit g_table4_128_emit
#define g_table4_182_ops g_table4_0_ops
#define g_table4_183_emit g_table4_128_emit
#define g_table4_183_ops g_table4_0_ops
#define g_table4_184_emit g_table4_128_emit
#define g_table4_184_ops g_table4_0_ops
#define g_table4_185_emit g_table4_128_emit
#define g_table4_185_ops g_table4_0_ops
#define g_table4_186_emit g_table4_128_emit
#define g_table4_186_ops g_table4_0_ops
#define g_table4_187_emit g_table4_128_emit
#define g_table4_187_ops g_table4_0_ops
#define g_table4_188_emit g_table4_128_emit
#define g_table4_188_ops g_table4_0_ops
#define g_table4_189_emit g_table4_128_emit
#define g_table4_189_ops g_table4_0_ops
#define g_table4_190_emit g_table4_128_emit
#define g_table4_190_ops g_table4_0_ops
#define g_table4_191_emit g_table4_128_emit
#define g_table4_191_ops g_table4_0_ops
#define g_table4_192_emit g_table4_128_emit
#define g_table4_192_ops g_table4_0_ops
#define g_table4_193_emit g_table4_128_emit
#define g_table4_193_ops g_table4_0_ops
#define g_table4_194_emit g_table4_128_emit
#define g_table4_194_ops g_table4_0_ops
#define g_table4_195_emit g_table4_128_emit
#define g_table4_195_ops g_table4_0_ops
#define g_table4_196_emit g_table4_128_emit
#define g_table4_196_ops g_table4_0_ops
#define g_table4_197_emit g_table4_128_emit
#define g_table4_197_ops g_table4_0_ops
#define g_table4_198_emit g_table4_128_emit
#define g_table4_198_ops g_table4_0_ops
#define g_table4_199_emit g_table4_128_emit
#define g_table4_199_ops g_table4_0_ops
#define g_table4_200_emit g_table4_128_emit
#define g_table4_200_ops g_table4_0_ops
#define g_table4_201_emit g_table4_128_emit
#define g_table4_201_ops g_table4_0_ops
#define g_table4_202_emit g_table4_128_emit
#define g_table4_202_ops g_table4_0_ops
#define g_table4_203_emit g_table4_128_emit
#define g_table4_203_ops g_table4_0_ops
#define g_table4_204_emit g_table4_128_emit
#define g_table4_204_ops g_table4_0_ops
#define g_table4_205_emit g_table4_128_emit
#define g_table4_205_ops g_table4_0_ops
#define g_table4_206_emit g_table4_128_emit
#define g_table4_206_ops g_table4_0_ops
#define g_table4_207_emit g_table4_128_emit
#define g_table4_207_ops g_table4_0_ops
#define g_table4_208_emit g_table4_128_emit
#define g_table4_208_ops g_table4_0_ops
#define g_table4_209_emit g_table4_128_emit
#define g_table4_209_ops g_table4_0_ops
#define g_table4_210_emit g_table4_128_emit
#define g_table4_210_ops g_table4_0_ops
#define g_table4_211_emit g_table4_128_emit
#define g_table4_211_ops g_table4_0_ops
#define g_table4_212_emit g_table4_128_emit
#define g_table4_212_ops g_table4_0_ops
#define g_table4_213_emit g_table4_128_emit
#define g_table4_213_ops g_table4_0_ops
#define g_table4_214_emit g_table4_128_emit
#define g_table4_214_ops g_table4_0_ops
#define g_table4_215_emit g_table4_128_emit
#define g_table4_215_ops g_table4_0_ops
#define g_table4_216_emit g_table4_128_emit
#define g_table4_216_ops g_table4_0_ops
#define g_table4_217_emit g_table4_128_emit
#define g_table4_217_ops g_table4_0_ops
#define g_table4_218_emit g_table4_128_emit
#define g_table4_218_ops g_table4_0_ops
#define g_table4_219_emit g_table4_128_emit
#define g_table4_219_ops g_table4_0_ops
#define g_table4_220_emit g_table4_128_emit
#define g_table4_220_ops g_table4_0_ops
#define g_table4_221_emit g_table4_128_emit
#define g_table4_221_ops g_table4_0_ops
#define g_table4_222_emit g_table4_128_emit
#define g_table4_222_ops g_table4_0_ops
#define g_table4_223_emit g_table4_128_emit
#define g_table4_223_ops g_table4_0_ops
#define g_table4_224_emit g_table4_128_emit
#define g_table4_224_ops g_table4_0_ops
#define g_table4_225_emit g_table4_128_emit
#define g_table4_225_ops g_table4_0_ops
#define g_table4_226_emit g_table4_128_emit
#define g_table4_226_ops g_table4_0_ops
#define g_table4_227_emit g_table4_128_emit
#define g_table4_227_ops g_table4_0_ops
#define g_table4_228_emit g_table4_128_emit
#define g_table4_228_ops g_table4_0_ops
#define g_table4_229_emit g_table4_128_emit
#define g_table4_229_ops g_table4_0_ops
#define g_table4_230_emit g_table4_128_emit
#define g_table4_230_ops g_table4_0_ops
#define g_table4_231_emit g_table4_128_emit
#define g_table4_231_ops g_table4_0_ops
#define g_table4_232_emit g_table4_128_emit
#define g_table4_232_ops g_table4_0_ops
#define g_table4_233_emit g_table4_128_emit
#define g_table4_233_ops g_table4_0_ops
#define g_table4_234_emit g_table4_128_emit
#define g_table4_234_ops g_table4_0_ops
#define g_table4_235_emit g_table4_128_emit
#define g_table4_235_ops g_table4_0_ops
#define g_table4_236_emit g_table4_128_emit
#define g_table4_236_ops g_table4_0_ops
#define g_table4_237_emit g_table4_128_emit
#define g_table4_237_ops g_table4_0_ops
#define g_table4_238_emit g_table4_128_emit
#define g_table4_238_ops g_table4_0_ops
#define g_table4_239_emit g_table4_128_emit
#define g_table4_239_ops g_table4_0_ops
#define g_table4_240_emit g_table4_128_emit
#define g_table4_240_ops g_table4_0_ops
#define g_table4_241_emit g_table4_128_emit
#define g_table4_241_ops g_table4_0_ops
#define g_table4_242_emit g_table4_128_emit
#define g_table4_242_ops g_table4_0_ops
#define g_table4_243_emit g_table4_128_emit
#define g_table4_243_ops g_table4_0_ops
#define g_table4_244_emit g_table4_128_emit
#define g_table4_244_ops g_table4_0_ops
#define g_table4_245_emit g_table4_128_emit
#define g_table4_245_ops g_table4_0_ops
#define g_table4_246_emit g_table4_128_emit
#define g_table4_246_ops g_table4_0_ops
#define g_table4_247_emit g_table4_128_emit
#define g_table4_247_ops g_table4_0_ops
#define g_table4_248_emit g_table4_128_emit
#define g_table4_248_ops g_table4_0_ops
#define g_table4_249_emit g_table4_128_emit
#define g_table4_249_ops g_table4_0_ops
#define g_table4_250_emit g_table4_128_emit
#define g_table4_250_ops g_table4_0_ops
#define g_table4_251_emit g_table4_128_emit
#define g_table4_251_ops g_table4_0_ops
#define g_table4_252_emit g_table4_128_emit
#define g_table4_252_ops g_table4_0_ops
#define g_table4_253_emit g_table4_128_emit
#define g_table4_253_ops g_table4_0_ops
#define g_table4_254_emit g_table4_128_emit
#define g_table4_254_ops g_table4_0_ops
#define g_table4_255_emit g_table4_128_emit
#define g_table4_255_ops g_table4_0_ops
static const uint8_t g_table4_256_emit[1] = {0x40};
#define g_table4_256_ops g_table4_0_ops
#define g_table4_257_emit g_table4_256_emit
#define g_table4_257_ops g_table4_0_ops
#define g_table4_258_emit g_table4_256_emit
#define g_table4_258_ops g_table4_0_ops
#define g_table4_259_emit g_table4_256_emit
#define g_table4_259_ops g_table4_0_ops
#define g_table4_260_emit g_table4_256_emit
#define g_table4_260_ops g_table4_0_ops
#define g_table4_261_emit g_table4_256_emit
#define g_table4_261_ops g_table4_0_ops
#define g_table4_262_emit g_table4_256_emit
#define g_table4_262_ops g_table4_0_ops
#define g_table4_263_emit g_table4_256_emit
#define g_table4_263_ops g_table4_0_ops
#define g_table4_264_emit g_table4_256_emit
#define g_table4_264_ops g_table4_0_ops
#define g_table4_265_emit g_table4_256_emit
#define g_table4_265_ops g_table4_0_ops
#define g_table4_266_emit g_table4_256_emit
#define g_table4_266_ops g_table4_0_ops
#define g_table4_267_emit g_table4_256_emit
#define g_table4_267_ops g_table4_0_ops
#define g_table4_268_emit g_table4_256_emit
#define g_table4_268_ops g_table4_0_ops
#define g_table4_269_emit g_table4_256_emit
#define g_table4_269_ops g_table4_0_ops
#define g_table4_270_emit g_table4_256_emit
#define g_table4_270_ops g_table4_0_ops
#define g_table4_271_emit g_table4_256_emit
#define g_table4_271_ops g_table4_0_ops
#define g_table4_272_emit g_table4_256_emit
#define g_table4_272_ops g_table4_0_ops
#define g_table4_273_emit g_table4_256_emit
#define g_table4_273_ops g_table4_0_ops
#define g_table4_274_emit g_table4_256_emit
#define g_table4_274_ops g_table4_0_ops
#define g_table4_275_emit g_table4_256_emit
#define g_table4_275_ops g_table4_0_ops
#define g_table4_276_emit g_table4_256_emit
#define g_table4_276_ops g_table4_0_ops
#define g_table4_277_emit g_table4_256_emit
#define g_table4_277_ops g_table4_0_ops
#define g_table4_278_emit g_table4_256_emit
#define g_table4_278_ops g_table4_0_ops
#define g_table4_279_emit g_table4_256_emit
#define g_table4_279_ops g_table4_0_ops
#define g_table4_280_emit g_table4_256_emit
#define g_table4_280_ops g_table4_0_ops
#define g_table4_281_emit g_table4_256_emit
#define g_table4_281_ops g_table4_0_ops
#define g_table4_282_emit g_table4_256_emit
#define g_table4_282_ops g_table4_0_ops
#define g_table4_283_emit g_table4_256_emit
#define g_table4_283_ops g_table4_0_ops
#define g_table4_284_emit g_table4_256_emit
#define g_table4_284_ops g_table4_0_ops
#define g_table4_285_emit g_table4_256_emit
#define g_table4_285_ops g_table4_0_ops
#define g_table4_286_emit g_table4_256_emit
#define g_table4_286_ops g_table4_0_ops
#define g_table4_287_emit g_table4_256_emit
#define g_table4_287_ops g_table4_0_ops
#define g_table4_288_emit g_table4_256_emit
#define g_table4_288_ops g_table4_0_ops
#define g_table4_289_emit g_table4_256_emit
#define g_table4_289_ops g_table4_0_ops
#define g_table4_290_emit g_table4_256_emit
#define g_table4_290_ops g_table4_0_ops
#define g_table4_291_emit g_table4_256_emit
#define g_table4_291_ops g_table4_0_ops
#define g_table4_292_emit g_table4_256_emit
#define g_table4_292_ops g_table4_0_ops
#define g_table4_293_emit g_table4_256_emit
#define g_table4_293_ops g_table4_0_ops
#define g_table4_294_emit g_table4_256_emit
#define g_table4_294_ops g_table4_0_ops
#define g_table4_295_emit g_table4_256_emit
#define g_table4_295_ops g_table4_0_ops
#define g_table4_296_emit g_table4_256_emit
#define g_table4_296_ops g_table4_0_ops
#define g_table4_297_emit g_table4_256_emit
#define g_table4_297_ops g_table4_0_ops
#define g_table4_298_emit g_table4_256_emit
#define g_table4_298_ops g_table4_0_ops
#define g_table4_299_emit g_table4_256_emit
#define g_table4_299_ops g_table4_0_ops
#define g_table4_300_emit g_table4_256_emit
#define g_table4_300_ops g_table4_0_ops
#define g_table4_301_emit g_table4_256_emit
#define g_table4_301_ops g_table4_0_ops
#define g_table4_302_emit g_table4_256_emit
#define g_table4_302_ops g_table4_0_ops
#define g_table4_303_emit g_table4_256_emit
#define g_table4_303_ops g_table4_0_ops
#define g_table4_304_emit g_table4_256_emit
#define g_table4_304_ops g_table4_0_ops
#define g_table4_305_emit g_table4_256_emit
#define g_table4_305_ops g_table4_0_ops
#define g_table4_306_emit g_table4_256_emit
#define g_table4_306_ops g_table4_0_ops
#define g_table4_307_emit g_table4_256_emit
#define g_table4_307_ops g_table4_0_ops
#define g_table4_308_emit g_table4_256_emit
#define g_table4_308_ops g_table4_0_ops
#define g_table4_309_emit g_table4_256_emit
#define g_table4_309_ops g_table4_0_ops
#define g_table4_310_emit g_table4_256_emit
#define g_table4_310_ops g_table4_0_ops
#define g_table4_311_emit g_table4_256_emit
#define g_table4_311_ops g_table4_0_ops
#define g_table4_312_emit g_table4_256_emit
#define g_table4_312_ops g_table4_0_ops
#define g_table4_313_emit g_table4_256_emit
#define g_table4_313_ops g_table4_0_ops
#define g_table4_314_emit g_table4_256_emit
#define g_table4_314_ops g_table4_0_ops
#define g_table4_315_emit g_table4_256_emit
#define g_table4_315_ops g_table4_0_ops
#define g_table4_316_emit g_table4_256_emit
#define g_table4_316_ops g_table4_0_ops
#define g_table4_317_emit g_table4_256_emit
#define g_table4_317_ops g_table4_0_ops
#define g_table4_318_emit g_table4_256_emit
#define g_table4_318_ops g_table4_0_ops
#define g_table4_319_emit g_table4_256_emit
#define g_table4_319_ops g_table4_0_ops
#define g_table4_320_emit g_table4_256_emit
#define g_table4_320_ops g_table4_0_ops
#define g_table4_321_emit g_table4_256_emit
#define g_table4_321_ops g_table4_0_ops
#define g_table4_322_emit g_table4_256_emit
#define g_table4_322_ops g_table4_0_ops
#define g_table4_323_emit g_table4_256_emit
#define g_table4_323_ops g_table4_0_ops
#define g_table4_324_emit g_table4_256_emit
#define g_table4_324_ops g_table4_0_ops
#define g_table4_325_emit g_table4_256_emit
#define g_table4_325_ops g_table4_0_ops
#define g_table4_326_emit g_table4_256_emit
#define g_table4_326_ops g_table4_0_ops
#define g_table4_327_emit g_table4_256_emit
#define g_table4_327_ops g_table4_0_ops
#define g_table4_328_emit g_table4_256_emit
#define g_table4_328_ops g_table4_0_ops
#define g_table4_329_emit g_table4_256_emit
#define g_table4_329_ops g_table4_0_ops
#define g_table4_330_emit g_table4_256_emit
#define g_table4_330_ops g_table4_0_ops
#define g_table4_331_emit g_table4_256_emit
#define g_table4_331_ops g_table4_0_ops
#define g_table4_332_emit g_table4_256_emit
#define g_table4_332_ops g_table4_0_ops
#define g_table4_333_emit g_table4_256_emit
#define g_table4_333_ops g_table4_0_ops
#define g_table4_334_emit g_table4_256_emit
#define g_table4_334_ops g_table4_0_ops
#define g_table4_335_emit g_table4_256_emit
#define g_table4_335_ops g_table4_0_ops
#define g_table4_336_emit g_table4_256_emit
#define g_table4_336_ops g_table4_0_ops
#define g_table4_337_emit g_table4_256_emit
#define g_table4_337_ops g_table4_0_ops
#define g_table4_338_emit g_table4_256_emit
#define g_table4_338_ops g_table4_0_ops
#define g_table4_339_emit g_table4_256_emit
#define g_table4_339_ops g_table4_0_ops
#define g_table4_340_emit g_table4_256_emit
#define g_table4_340_ops g_table4_0_ops
#define g_table4_341_emit g_table4_256_emit
#define g_table4_341_ops g_table4_0_ops
#define g_table4_342_emit g_table4_256_emit
#define g_table4_342_ops g_table4_0_ops
#define g_table4_343_emit g_table4_256_emit
#define g_table4_343_ops g_table4_0_ops
#define g_table4_344_emit g_table4_256_emit
#define g_table4_344_ops g_table4_0_ops
#define g_table4_345_emit g_table4_256_emit
#define g_table4_345_ops g_table4_0_ops
#define g_table4_346_emit g_table4_256_emit
#define g_table4_346_ops g_table4_0_ops
#define g_table4_347_emit g_table4_256_emit
#define g_table4_347_ops g_table4_0_ops
#define g_table4_348_emit g_table4_256_emit
#define g_table4_348_ops g_table4_0_ops
#define g_table4_349_emit g_table4_256_emit
#define g_table4_349_ops g_table4_0_ops
#define g_table4_350_emit g_table4_256_emit
#define g_table4_350_ops g_table4_0_ops
#define g_table4_351_emit g_table4_256_emit
#define g_table4_351_ops g_table4_0_ops
#define g_table4_352_emit g_table4_256_emit
#define g_table4_352_ops g_table4_0_ops
#define g_table4_353_emit g_table4_256_emit
#define g_table4_353_ops g_table4_0_ops
#define g_table4_354_emit g_table4_256_emit
#define g_table4_354_ops g_table4_0_ops
#define g_table4_355_emit g_table4_256_emit
#define g_table4_355_ops g_table4_0_ops
#define g_table4_356_emit g_table4_256_emit
#define g_table4_356_ops g_table4_0_ops
#define g_table4_357_emit g_table4_256_emit
#define g_table4_357_ops g_table4_0_ops
#define g_table4_358_emit g_table4_256_emit
#define g_table4_358_ops g_table4_0_ops
#define g_table4_359_emit g_table4_256_emit
#define g_table4_359_ops g_table4_0_ops
#define g_table4_360_emit g_table4_256_emit
#define g_table4_360_ops g_table4_0_ops
#define g_table4_361_emit g_table4_256_emit
#define g_table4_361_ops g_table4_0_ops
#define g_table4_362_emit g_table4_256_emit
#define g_table4_362_ops g_table4_0_ops
#define g_table4_363_emit g_table4_256_emit
#define g_table4_363_ops g_table4_0_ops
#define g_table4_364_emit g_table4_256_emit
#define g_table4_364_ops g_table4_0_ops
#define g_table4_365_emit g_table4_256_emit
#define g_table4_365_ops g_table4_0_ops
#define g_table4_366_emit g_table4_256_emit
#define g_table4_366_ops g_table4_0_ops
#define g_table4_367_emit g_table4_256_emit
#define g_table4_367_ops g_table4_0_ops
#define g_table4_368_emit g_table4_256_emit
#define g_table4_368_ops g_table4_0_ops
#define g_table4_369_emit g_table4_256_emit
#define g_table4_369_ops g_table4_0_ops
#define g_table4_370_emit g_table4_256_emit
#define g_table4_370_ops g_table4_0_ops
#define g_table4_371_emit g_table4_256_emit
#define g_table4_371_ops g_table4_0_ops
#define g_table4_372_emit g_table4_256_emit
#define g_table4_372_ops g_table4_0_ops
#define g_table4_373_emit g_table4_256_emit
#define g_table4_373_ops g_table4_0_ops
#define g_table4_374_emit g_table4_256_emit
#define g_table4_374_ops g_table4_0_ops
#define g_table4_375_emit g_table4_256_emit
#define g_table4_375_ops g_table4_0_ops
#define g_table4_376_emit g_table4_256_emit
#define g_table4_376_ops g_table4_0_ops
#define g_table4_377_emit g_table4_256_emit
#define g_table4_377_ops g_table4_0_ops
#define g_table4_378_emit g_table4_256_emit
#define g_table4_378_ops g_table4_0_ops
#define g_table4_379_emit g_table4_256_emit
#define g_table4_379_ops g_table4_0_ops
#define g_table4_380_emit g_table4_256_emit
#define g_table4_380_ops g_table4_0_ops
#define g_table4_381_emit g_table4_256_emit
#define g_table4_381_ops g_table4_0_ops
#define g_table4_382_emit g_table4_256_emit
#define g_table4_382_ops g_table4_0_ops
#define g_table4_383_emit g_table4_256_emit
#define g_table4_383_ops g_table4_0_ops
static const uint8_t g_table4_384_emit[1] = {0x5b};
#define g_table4_384_ops g_table4_0_ops
#define g_table4_385_emit g_table4_384_emit
#define g_table4_385_ops g_table4_0_ops
#define g_table4_386_emit g_table4_384_emit
#define g_table4_386_ops g_table4_0_ops
#define g_table4_387_emit g_table4_384_emit
#define g_table4_387_ops g_table4_0_ops
#define g_table4_388_emit g_table4_384_emit
#define g_table4_388_ops g_table4_0_ops
#define g_table4_389_emit g_table4_384_emit
#define g_table4_389_ops g_table4_0_ops
#define g_table4_390_emit g_table4_384_emit
#define g_table4_390_ops g_table4_0_ops
#define g_table4_391_emit g_table4_384_emit
#define g_table4_391_ops g_table4_0_ops
#define g_table4_392_emit g_table4_384_emit
#define g_table4_392_ops g_table4_0_ops
#define g_table4_393_emit g_table4_384_emit
#define g_table4_393_ops g_table4_0_ops
#define g_table4_394_emit g_table4_384_emit
#define g_table4_394_ops g_table4_0_ops
#define g_table4_395_emit g_table4_384_emit
#define g_table4_395_ops g_table4_0_ops
#define g_table4_396_emit g_table4_384_emit
#define g_table4_396_ops g_table4_0_ops
#define g_table4_397_emit g_table4_384_emit
#define g_table4_397_ops g_table4_0_ops
#define g_table4_398_emit g_table4_384_emit
#define g_table4_398_ops g_table4_0_ops
#define g_table4_399_emit g_table4_384_emit
#define g_table4_399_ops g_table4_0_ops
#define g_table4_400_emit g_table4_384_emit
#define g_table4_400_ops g_table4_0_ops
#define g_table4_401_emit g_table4_384_emit
#define g_table4_401_ops g_table4_0_ops
#define g_table4_402_emit g_table4_384_emit
#define g_table4_402_ops g_table4_0_ops
#define g_table4_403_emit g_table4_384_emit
#define g_table4_403_ops g_table4_0_ops
#define g_table4_404_emit g_table4_384_emit
#define g_table4_404_ops g_table4_0_ops
#define g_table4_405_emit g_table4_384_emit
#define g_table4_405_ops g_table4_0_ops
#define g_table4_406_emit g_table4_384_emit
#define g_table4_406_ops g_table4_0_ops
#define g_table4_407_emit g_table4_384_emit
#define g_table4_407_ops g_table4_0_ops
#define g_table4_408_emit g_table4_384_emit
#define g_table4_408_ops g_table4_0_ops
#define g_table4_409_emit g_table4_384_emit
#define g_table4_409_ops g_table4_0_ops
#define g_table4_410_emit g_table4_384_emit
#define g_table4_410_ops g_table4_0_ops
#define g_table4_411_emit g_table4_384_emit
#define g_table4_411_ops g_table4_0_ops
#define g_table4_412_emit g_table4_384_emit
#define g_table4_412_ops g_table4_0_ops
#define g_table4_413_emit g_table4_384_emit
#define g_table4_413_ops g_table4_0_ops
#define g_table4_414_emit g_table4_384_emit
#define g_table4_414_ops g_table4_0_ops
#define g_table4_415_emit g_table4_384_emit
#define g_table4_415_ops g_table4_0_ops
#define g_table4_416_emit g_table4_384_emit
#define g_table4_416_ops g_table4_0_ops
#define g_table4_417_emit g_table4_384_emit
#define g_table4_417_ops g_table4_0_ops
#define g_table4_418_emit g_table4_384_emit
#define g_table4_418_ops g_table4_0_ops
#define g_table4_419_emit g_table4_384_emit
#define g_table4_419_ops g_table4_0_ops
#define g_table4_420_emit g_table4_384_emit
#define g_table4_420_ops g_table4_0_ops
#define g_table4_421_emit g_table4_384_emit
#define g_table4_421_ops g_table4_0_ops
#define g_table4_422_emit g_table4_384_emit
#define g_table4_422_ops g_table4_0_ops
#define g_table4_423_emit g_table4_384_emit
#define g_table4_423_ops g_table4_0_ops
#define g_table4_424_emit g_table4_384_emit
#define g_table4_424_ops g_table4_0_ops
#define g_table4_425_emit g_table4_384_emit
#define g_table4_425_ops g_table4_0_ops
#define g_table4_426_emit g_table4_384_emit
#define g_table4_426_ops g_table4_0_ops
#define g_table4_427_emit g_table4_384_emit
#define g_table4_427_ops g_table4_0_ops
#define g_table4_428_emit g_table4_384_emit
#define g_table4_428_ops g_table4_0_ops
#define g_table4_429_emit g_table4_384_emit
#define g_table4_429_ops g_table4_0_ops
#define g_table4_430_emit g_table4_384_emit
#define g_table4_430_ops g_table4_0_ops
#define g_table4_431_emit g_table4_384_emit
#define g_table4_431_ops g_table4_0_ops
#define g_table4_432_emit g_table4_384_emit
#define g_table4_432_ops g_table4_0_ops
#define g_table4_433_emit g_table4_384_emit
#define g_table4_433_ops g_table4_0_ops
#define g_table4_434_emit g_table4_384_emit
#define g_table4_434_ops g_table4_0_ops
#define g_table4_435_emit g_table4_384_emit
#define g_table4_435_ops g_table4_0_ops
#define g_table4_436_emit g_table4_384_emit
#define g_table4_436_ops g_table4_0_ops
#define g_table4_437_emit g_table4_384_emit
#define g_table4_437_ops g_table4_0_ops
#define g_table4_438_emit g_table4_384_emit
#define g_table4_438_ops g_table4_0_ops
#define g_table4_439_emit g_table4_384_emit
#define g_table4_439_ops g_table4_0_ops
#define g_table4_440_emit g_table4_384_emit
#define g_table4_440_ops g_table4_0_ops
#define g_table4_441_emit g_table4_384_emit
#define g_table4_441_ops g_table4_0_ops
#define g_table4_442_emit g_table4_384_emit
#define g_table4_442_ops g_table4_0_ops
#define g_table4_443_emit g_table4_384_emit
#define g_table4_443_ops g_table4_0_ops
#define g_table4_444_emit g_table4_384_emit
#define g_table4_444_ops g_table4_0_ops
#define g_table4_445_emit g_table4_384_emit
#define g_table4_445_ops g_table4_0_ops
#define g_table4_446_emit g_table4_384_emit
#define g_table4_446_ops g_table4_0_ops
#define g_table4_447_emit g_table4_384_emit
#define g_table4_447_ops g_table4_0_ops
#define g_table4_448_emit g_table4_384_emit
#define g_table4_448_ops g_table4_0_ops
#define g_table4_449_emit g_table4_384_emit
#define g_table4_449_ops g_table4_0_ops
#define g_table4_450_emit g_table4_384_emit
#define g_table4_450_ops g_table4_0_ops
#define g_table4_451_emit g_table4_384_emit
#define g_table4_451_ops g_table4_0_ops
#define g_table4_452_emit g_table4_384_emit
#define g_table4_452_ops g_table4_0_ops
#define g_table4_453_emit g_table4_384_emit
#define g_table4_453_ops g_table4_0_ops
#define g_table4_454_emit g_table4_384_emit
#define g_table4_454_ops g_table4_0_ops
#define g_table4_455_emit g_table4_384_emit
#define g_table4_455_ops g_table4_0_ops
#define g_table4_456_emit g_table4_384_emit
#define g_table4_456_ops g_table4_0_ops
#define g_table4_457_emit g_table4_384_emit
#define g_table4_457_ops g_table4_0_ops
#define g_table4_458_emit g_table4_384_emit
#define g_table4_458_ops g_table4_0_ops
#define g_table4_459_emit g_table4_384_emit
#define g_table4_459_ops g_table4_0_ops
#define g_table4_460_emit g_table4_384_emit
#define g_table4_460_ops g_table4_0_ops
#define g_table4_461_emit g_table4_384_emit
#define g_table4_461_ops g_table4_0_ops
#define g_table4_462_emit g_table4_384_emit
#define g_table4_462_ops g_table4_0_ops
#define g_table4_463_emit g_table4_384_emit
#define g_table4_463_ops g_table4_0_ops
#define g_table4_464_emit g_table4_384_emit
#define g_table4_464_ops g_table4_0_ops
#define g_table4_465_emit g_table4_384_emit
#define g_table4_465_ops g_table4_0_ops
#define g_table4_466_emit g_table4_384_emit
#define g_table4_466_ops g_table4_0_ops
#define g_table4_467_emit g_table4_384_emit
#define g_table4_467_ops g_table4_0_ops
#define g_table4_468_emit g_table4_384_emit
#define g_table4_468_ops g_table4_0_ops
#define g_table4_469_emit g_table4_384_emit
#define g_table4_469_ops g_table4_0_ops
#define g_table4_470_emit g_table4_384_emit
#define g_table4_470_ops g_table4_0_ops
#define g_table4_471_emit g_table4_384_emit
#define g_table4_471_ops g_table4_0_ops
#define g_table4_472_emit g_table4_384_emit
#define g_table4_472_ops g_table4_0_ops
#define g_table4_473_emit g_table4_384_emit
#define g_table4_473_ops g_table4_0_ops
#define g_table4_474_emit g_table4_384_emit
#define g_table4_474_ops g_table4_0_ops
#define g_table4_475_emit g_table4_384_emit
#define g_table4_475_ops g_table4_0_ops
#define g_table4_476_emit g_table4_384_emit
#define g_table4_476_ops g_table4_0_ops
#define g_table4_477_emit g_table4_384_emit
#define g_table4_477_ops g_table4_0_ops
#define g_table4_478_emit g_table4_384_emit
#define g_table4_478_ops g_table4_0_ops
#define g_table4_479_emit g_table4_384_emit
#define g_table4_479_ops g_table4_0_ops
#define g_table4_480_emit g_table4_384_emit
#define g_table4_480_ops g_table4_0_ops
#define g_table4_481_emit g_table4_384_emit
#define g_table4_481_ops g_table4_0_ops
#define g_table4_482_emit g_table4_384_emit
#define g_table4_482_ops g_table4_0_ops
#define g_table4_483_emit g_table4_384_emit
#define g_table4_483_ops g_table4_0_ops
#define g_table4_484_emit g_table4_384_emit
#define g_table4_484_ops g_table4_0_ops
#define g_table4_485_emit g_table4_384_emit
#define g_table4_485_ops g_table4_0_ops
#define g_table4_486_emit g_table4_384_emit
#define g_table4_486_ops g_table4_0_ops
#define g_table4_487_emit g_table4_384_emit
#define g_table4_487_ops g_table4_0_ops
#define g_table4_488_emit g_table4_384_emit
#define g_table4_488_ops g_table4_0_ops
#define g_table4_489_emit g_table4_384_emit
#define g_table4_489_ops g_table4_0_ops
#define g_table4_490_emit g_table4_384_emit
#define g_table4_490_ops g_table4_0_ops
#define g_table4_491_emit g_table4_384_emit
#define g_table4_491_ops g_table4_0_ops
#define g_table4_492_emit g_table4_384_emit
#define g_table4_492_ops g_table4_0_ops
#define g_table4_493_emit g_table4_384_emit
#define g_table4_493_ops g_table4_0_ops
#define g_table4_494_emit g_table4_384_emit
#define g_table4_494_ops g_table4_0_ops
#define g_table4_495_emit g_table4_384_emit
#define g_table4_495_ops g_table4_0_ops
#define g_table4_496_emit g_table4_384_emit
#define g_table4_496_ops g_table4_0_ops
#define g_table4_497_emit g_table4_384_emit
#define g_table4_497_ops g_table4_0_ops
#define g_table4_498_emit g_table4_384_emit
#define g_table4_498_ops g_table4_0_ops
#define g_table4_499_emit g_table4_384_emit
#define g_table4_499_ops g_table4_0_ops
#define g_table4_500_emit g_table4_384_emit
#define g_table4_500_ops g_table4_0_ops
#define g_table4_501_emit g_table4_384_emit
#define g_table4_501_ops g_table4_0_ops
#define g_table4_502_emit g_table4_384_emit
#define g_table4_502_ops g_table4_0_ops
#define g_table4_503_emit g_table4_384_emit
#define g_table4_503_ops g_table4_0_ops
#define g_table4_504_emit g_table4_384_emit
#define g_table4_504_ops g_table4_0_ops
#define g_table4_505_emit g_table4_384_emit
#define g_table4_505_ops g_table4_0_ops
#define g_table4_506_emit g_table4_384_emit
#define g_table4_506_ops g_table4_0_ops
#define g_table4_507_emit g_table4_384_emit
#define g_table4_507_ops g_table4_0_ops
#define g_table4_508_emit g_table4_384_emit
#define g_table4_508_ops g_table4_0_ops
#define g_table4_509_emit g_table4_384_emit
#define g_table4_509_ops g_table4_0_ops
#define g_table4_510_emit g_table4_384_emit
#define g_table4_510_ops g_table4_0_ops
#define g_table4_511_emit g_table4_384_emit
#define g_table4_511_ops g_table4_0_ops
static const uint8_t g_table4_512_emit[1] = {0x5d};
#define g_table4_512_ops g_table4_0_ops
#define g_table4_513_emit g_table4_512_emit
#define g_table4_513_ops g_table4_0_ops
#define g_table4_514_emit g_table4_512_emit
#define g_table4_514_ops g_table4_0_ops
#define g_table4_515_emit g_table4_512_emit
#define g_table4_515_ops g_table4_0_ops
#define g_table4_516_emit g_table4_512_emit
#define g_table4_516_ops g_table4_0_ops
#define g_table4_517_emit g_table4_512_emit
#define g_table4_517_ops g_table4_0_ops
#define g_table4_518_emit g_table4_512_emit
#define g_table4_518_ops g_table4_0_ops
#define g_table4_519_emit g_table4_512_emit
#define g_table4_519_ops g_table4_0_ops
#define g_table4_520_emit g_table4_512_emit
#define g_table4_520_ops g_table4_0_ops
#define g_table4_521_emit g_table4_512_emit
#define g_table4_521_ops g_table4_0_ops
#define g_table4_522_emit g_table4_512_emit
#define g_table4_522_ops g_table4_0_ops
#define g_table4_523_emit g_table4_512_emit
#define g_table4_523_ops g_table4_0_ops
#define g_table4_524_emit g_table4_512_emit
#define g_table4_524_ops g_table4_0_ops
#define g_table4_525_emit g_table4_512_emit
#define g_table4_525_ops g_table4_0_ops
#define g_table4_526_emit g_table4_512_emit
#define g_table4_526_ops g_table4_0_ops
#define g_table4_527_emit g_table4_512_emit
#define g_table4_527_ops g_table4_0_ops
#define g_table4_528_emit g_table4_512_emit
#define g_table4_528_ops g_table4_0_ops
#define g_table4_529_emit g_table4_512_emit
#define g_table4_529_ops g_table4_0_ops
#define g_table4_530_emit g_table4_512_emit
#define g_table4_530_ops g_table4_0_ops
#define g_table4_531_emit g_table4_512_emit
#define g_table4_531_ops g_table4_0_ops
#define g_table4_532_emit g_table4_512_emit
#define g_table4_532_ops g_table4_0_ops
#define g_table4_533_emit g_table4_512_emit
#define g_table4_533_ops g_table4_0_ops
#define g_table4_534_emit g_table4_512_emit
#define g_table4_534_ops g_table4_0_ops
#define g_table4_535_emit g_table4_512_emit
#define g_table4_535_ops g_table4_0_ops
#define g_table4_536_emit g_table4_512_emit
#define g_table4_536_ops g_table4_0_ops
#define g_table4_537_emit g_table4_512_emit
#define g_table4_537_ops g_table4_0_ops
#define g_table4_538_emit g_table4_512_emit
#define g_table4_538_ops g_table4_0_ops
#define g_table4_539_emit g_table4_512_emit
#define g_table4_539_ops g_table4_0_ops
#define g_table4_540_emit g_table4_512_emit
#define g_table4_540_ops g_table4_0_ops
#define g_table4_541_emit g_table4_512_emit
#define g_table4_541_ops g_table4_0_ops
#define g_table4_542_emit g_table4_512_emit
#define g_table4_542_ops g_table4_0_ops
#define g_table4_543_emit g_table4_512_emit
#define g_table4_543_ops g_table4_0_ops
#define g_table4_544_emit g_table4_512_emit
#define g_table4_544_ops g_table4_0_ops
#define g_table4_545_emit g_table4_512_emit
#define g_table4_545_ops g_table4_0_ops
#define g_table4_546_emit g_table4_512_emit
#define g_table4_546_ops g_table4_0_ops
#define g_table4_547_emit g_table4_512_emit
#define g_table4_547_ops g_table4_0_ops
#define g_table4_548_emit g_table4_512_emit
#define g_table4_548_ops g_table4_0_ops
#define g_table4_549_emit g_table4_512_emit
#define g_table4_549_ops g_table4_0_ops
#define g_table4_550_emit g_table4_512_emit
#define g_table4_550_ops g_table4_0_ops
#define g_table4_551_emit g_table4_512_emit
#define g_table4_551_ops g_table4_0_ops
#define g_table4_552_emit g_table4_512_emit
#define g_table4_552_ops g_table4_0_ops
#define g_table4_553_emit g_table4_512_emit
#define g_table4_553_ops g_table4_0_ops
#define g_table4_554_emit g_table4_512_emit
#define g_table4_554_ops g_table4_0_ops
#define g_table4_555_emit g_table4_512_emit
#define g_table4_555_ops g_table4_0_ops
#define g_table4_556_emit g_table4_512_emit
#define g_table4_556_ops g_table4_0_ops
#define g_table4_557_emit g_table4_512_emit
#define g_table4_557_ops g_table4_0_ops
#define g_table4_558_emit g_table4_512_emit
#define g_table4_558_ops g_table4_0_ops
#define g_table4_559_emit g_table4_512_emit
#define g_table4_559_ops g_table4_0_ops
#define g_table4_560_emit g_table4_512_emit
#define g_table4_560_ops g_table4_0_ops
#define g_table4_561_emit g_table4_512_emit
#define g_table4_561_ops g_table4_0_ops
#define g_table4_562_emit g_table4_512_emit
#define g_table4_562_ops g_table4_0_ops
#define g_table4_563_emit g_table4_512_emit
#define g_table4_563_ops g_table4_0_ops
#define g_table4_564_emit g_table4_512_emit
#define g_table4_564_ops g_table4_0_ops
#define g_table4_565_emit g_table4_512_emit
#define g_table4_565_ops g_table4_0_ops
#define g_table4_566_emit g_table4_512_emit
#define g_table4_566_ops g_table4_0_ops
#define g_table4_567_emit g_table4_512_emit
#define g_table4_567_ops g_table4_0_ops
#define g_table4_568_emit g_table4_512_emit
#define g_table4_568_ops g_table4_0_ops
#define g_table4_569_emit g_table4_512_emit
#define g_table4_569_ops g_table4_0_ops
#define g_table4_570_emit g_table4_512_emit
#define g_table4_570_ops g_table4_0_ops
#define g_table4_571_emit g_table4_512_emit
#define g_table4_571_ops g_table4_0_ops
#define g_table4_572_emit g_table4_512_emit
#define g_table4_572_ops g_table4_0_ops
#define g_table4_573_emit g_table4_512_emit
#define g_table4_573_ops g_table4_0_ops
#define g_table4_574_emit g_table4_512_emit
#define g_table4_574_ops g_table4_0_ops
#define g_table4_575_emit g_table4_512_emit
#define g_table4_575_ops g_table4_0_ops
#define g_table4_576_emit g_table4_512_emit
#define g_table4_576_ops g_table4_0_ops
#define g_table4_577_emit g_table4_512_emit
#define g_table4_577_ops g_table4_0_ops
#define g_table4_578_emit g_table4_512_emit
#define g_table4_578_ops g_table4_0_ops
#define g_table4_579_emit g_table4_512_emit
#define g_table4_579_ops g_table4_0_ops
#define g_table4_580_emit g_table4_512_emit
#define g_table4_580_ops g_table4_0_ops
#define g_table4_581_emit g_table4_512_emit
#define g_table4_581_ops g_table4_0_ops
#define g_table4_582_emit g_table4_512_emit
#define g_table4_582_ops g_table4_0_ops
#define g_table4_583_emit g_table4_512_emit
#define g_table4_583_ops g_table4_0_ops
#define g_table4_584_emit g_table4_512_emit
#define g_table4_584_ops g_table4_0_ops
#define g_table4_585_emit g_table4_512_emit
#define g_table4_585_ops g_table4_0_ops
#define g_table4_586_emit g_table4_512_emit
#define g_table4_586_ops g_table4_0_ops
#define g_table4_587_emit g_table4_512_emit
#define g_table4_587_ops g_table4_0_ops
#define g_table4_588_emit g_table4_512_emit
#define g_table4_588_ops g_table4_0_ops
#define g_table4_589_emit g_table4_512_emit
#define g_table4_589_ops g_table4_0_ops
#define g_table4_590_emit g_table4_512_emit
#define g_table4_590_ops g_table4_0_ops
#define g_table4_591_emit g_table4_512_emit
#define g_table4_591_ops g_table4_0_ops
#define g_table4_592_emit g_table4_512_emit
#define g_table4_592_ops g_table4_0_ops
#define g_table4_593_emit g_table4_512_emit
#define g_table4_593_ops g_table4_0_ops
#define g_table4_594_emit g_table4_512_emit
#define g_table4_594_ops g_table4_0_ops
#define g_table4_595_emit g_table4_512_emit
#define g_table4_595_ops g_table4_0_ops
#define g_table4_596_emit g_table4_512_emit
#define g_table4_596_ops g_table4_0_ops
#define g_table4_597_emit g_table4_512_emit
#define g_table4_597_ops g_table4_0_ops
#define g_table4_598_emit g_table4_512_emit
#define g_table4_598_ops g_table4_0_ops
#define g_table4_599_emit g_table4_512_emit
#define g_table4_599_ops g_table4_0_ops
#define g_table4_600_emit g_table4_512_emit
#define g_table4_600_ops g_table4_0_ops
#define g_table4_601_emit g_table4_512_emit
#define g_table4_601_ops g_table4_0_ops
#define g_table4_602_emit g_table4_512_emit
#define g_table4_602_ops g_table4_0_ops
#define g_table4_603_emit g_table4_512_emit
#define g_table4_603_ops g_table4_0_ops
#define g_table4_604_emit g_table4_512_emit
#define g_table4_604_ops g_table4_0_ops
#define g_table4_605_emit g_table4_512_emit
#define g_table4_605_ops g_table4_0_ops
#define g_table4_606_emit g_table4_512_emit
#define g_table4_606_ops g_table4_0_ops
#define g_table4_607_emit g_table4_512_emit
#define g_table4_607_ops g_table4_0_ops
#define g_table4_608_emit g_table4_512_emit
#define g_table4_608_ops g_table4_0_ops
#define g_table4_609_emit g_table4_512_emit
#define g_table4_609_ops g_table4_0_ops
#define g_table4_610_emit g_table4_512_emit
#define g_table4_610_ops g_table4_0_ops
#define g_table4_611_emit g_table4_512_emit
#define g_table4_611_ops g_table4_0_ops
#define g_table4_612_emit g_table4_512_emit
#define g_table4_612_ops g_table4_0_ops
#define g_table4_613_emit g_table4_512_emit
#define g_table4_613_ops g_table4_0_ops
#define g_table4_614_emit g_table4_512_emit
#define g_table4_614_ops g_table4_0_ops
#define g_table4_615_emit g_table4_512_emit
#define g_table4_615_ops g_table4_0_ops
#define g_table4_616_emit g_table4_512_emit
#define g_table4_616_ops g_table4_0_ops
#define g_table4_617_emit g_table4_512_emit
#define g_table4_617_ops g_table4_0_ops
#define g_table4_618_emit g_table4_512_emit
#define g_table4_618_ops g_table4_0_ops
#define g_table4_619_emit g_table4_512_emit
#define g_table4_619_ops g_table4_0_ops
#define g_table4_620_emit g_table4_512_emit
#define g_table4_620_ops g_table4_0_ops
#define g_table4_621_emit g_table4_512_emit
#define g_table4_621_ops g_table4_0_ops
#define g_table4_622_emit g_table4_512_emit
#define g_table4_622_ops g_table4_0_ops
#define g_table4_623_emit g_table4_512_emit
#define g_table4_623_ops g_table4_0_ops
#define g_table4_624_emit g_table4_512_emit
#define g_table4_624_ops g_table4_0_ops
#define g_table4_625_emit g_table4_512_emit
#define g_table4_625_ops g_table4_0_ops
#define g_table4_626_emit g_table4_512_emit
#define g_table4_626_ops g_table4_0_ops
#define g_table4_627_emit g_table4_512_emit
#define g_table4_627_ops g_table4_0_ops
#define g_table4_628_emit g_table4_512_emit
#define g_table4_628_ops g_table4_0_ops
#define g_table4_629_emit g_table4_512_emit
#define g_table4_629_ops g_table4_0_ops
#define g_table4_630_emit g_table4_512_emit
#define g_table4_630_ops g_table4_0_ops
#define g_table4_631_emit g_table4_512_emit
#define g_table4_631_ops g_table4_0_ops
#define g_table4_632_emit g_table4_512_emit
#define g_table4_632_ops g_table4_0_ops
#define g_table4_633_emit g_table4_512_emit
#define g_table4_633_ops g_table4_0_ops
#define g_table4_634_emit g_table4_512_emit
#define g_table4_634_ops g_table4_0_ops
#define g_table4_635_emit g_table4_512_emit
#define g_table4_635_ops g_table4_0_ops
#define g_table4_636_emit g_table4_512_emit
#define g_table4_636_ops g_table4_0_ops
#define g_table4_637_emit g_table4_512_emit
#define g_table4_637_ops g_table4_0_ops
#define g_table4_638_emit g_table4_512_emit
#define g_table4_638_ops g_table4_0_ops
#define g_table4_639_emit g_table4_512_emit
#define g_table4_639_ops g_table4_0_ops
static const uint8_t g_table4_640_emit[1] = {0x7e};
#define g_table4_640_ops g_table4_0_ops
#define g_table4_641_emit g_table4_640_emit
#define g_table4_641_ops g_table4_0_ops
#define g_table4_642_emit g_table4_640_emit
#define g_table4_642_ops g_table4_0_ops
#define g_table4_643_emit g_table4_640_emit
#define g_table4_643_ops g_table4_0_ops
#define g_table4_644_emit g_table4_640_emit
#define g_table4_644_ops g_table4_0_ops
#define g_table4_645_emit g_table4_640_emit
#define g_table4_645_ops g_table4_0_ops
#define g_table4_646_emit g_table4_640_emit
#define g_table4_646_ops g_table4_0_ops
#define g_table4_647_emit g_table4_640_emit
#define g_table4_647_ops g_table4_0_ops
#define g_table4_648_emit g_table4_640_emit
#define g_table4_648_ops g_table4_0_ops
#define g_table4_649_emit g_table4_640_emit
#define g_table4_649_ops g_table4_0_ops
#define g_table4_650_emit g_table4_640_emit
#define g_table4_650_ops g_table4_0_ops
#define g_table4_651_emit g_table4_640_emit
#define g_table4_651_ops g_table4_0_ops
#define g_table4_652_emit g_table4_640_emit
#define g_table4_652_ops g_table4_0_ops
#define g_table4_653_emit g_table4_640_emit
#define g_table4_653_ops g_table4_0_ops
#define g_table4_654_emit g_table4_640_emit
#define g_table4_654_ops g_table4_0_ops
#define g_table4_655_emit g_table4_640_emit
#define g_table4_655_ops g_table4_0_ops
#define g_table4_656_emit g_table4_640_emit
#define g_table4_656_ops g_table4_0_ops
#define g_table4_657_emit g_table4_640_emit
#define g_table4_657_ops g_table4_0_ops
#define g_table4_658_emit g_table4_640_emit
#define g_table4_658_ops g_table4_0_ops
#define g_table4_659_emit g_table4_640_emit
#define g_table4_659_ops g_table4_0_ops
#define g_table4_660_emit g_table4_640_emit
#define g_table4_660_ops g_table4_0_ops
#define g_table4_661_emit g_table4_640_emit
#define g_table4_661_ops g_table4_0_ops
#define g_table4_662_emit g_table4_640_emit
#define g_table4_662_ops g_table4_0_ops
#define g_table4_663_emit g_table4_640_emit
#define g_table4_663_ops g_table4_0_ops
#define g_table4_664_emit g_table4_640_emit
#define g_table4_664_ops g_table4_0_ops
#define g_table4_665_emit g_table4_640_emit
#define g_table4_665_ops g_table4_0_ops
#define g_table4_666_emit g_table4_640_emit
#define g_table4_666_ops g_table4_0_ops
#define g_table4_667_emit g_table4_640_emit
#define g_table4_667_ops g_table4_0_ops
#define g_table4_668_emit g_table4_640_emit
#define g_table4_668_ops g_table4_0_ops
#define g_table4_669_emit g_table4_640_emit
#define g_table4_669_ops g_table4_0_ops
#define g_table4_670_emit g_table4_640_emit
#define g_table4_670_ops g_table4_0_ops
#define g_table4_671_emit g_table4_640_emit
#define g_table4_671_ops g_table4_0_ops
#define g_table4_672_emit g_table4_640_emit
#define g_table4_672_ops g_table4_0_ops
#define g_table4_673_emit g_table4_640_emit
#define g_table4_673_ops g_table4_0_ops
#define g_table4_674_emit g_table4_640_emit
#define g_table4_674_ops g_table4_0_ops
#define g_table4_675_emit g_table4_640_emit
#define g_table4_675_ops g_table4_0_ops
#define g_table4_676_emit g_table4_640_emit
#define g_table4_676_ops g_table4_0_ops
#define g_table4_677_emit g_table4_640_emit
#define g_table4_677_ops g_table4_0_ops
#define g_table4_678_emit g_table4_640_emit
#define g_table4_678_ops g_table4_0_ops
#define g_table4_679_emit g_table4_640_emit
#define g_table4_679_ops g_table4_0_ops
#define g_table4_680_emit g_table4_640_emit
#define g_table4_680_ops g_table4_0_ops
#define g_table4_681_emit g_table4_640_emit
#define g_table4_681_ops g_table4_0_ops
#define g_table4_682_emit g_table4_640_emit
#define g_table4_682_ops g_table4_0_ops
#define g_table4_683_emit g_table4_640_emit
#define g_table4_683_ops g_table4_0_ops
#define g_table4_684_emit g_table4_640_emit
#define g_table4_684_ops g_table4_0_ops
#define g_table4_685_emit g_table4_640_emit
#define g_table4_685_ops g_table4_0_ops
#define g_table4_686_emit g_table4_640_emit
#define g_table4_686_ops g_table4_0_ops
#define g_table4_687_emit g_table4_640_emit
#define g_table4_687_ops g_table4_0_ops
#define g_table4_688_emit g_table4_640_emit
#define g_table4_688_ops g_table4_0_ops
#define g_table4_689_emit g_table4_640_emit
#define g_table4_689_ops g_table4_0_ops
#define g_table4_690_emit g_table4_640_emit
#define g_table4_690_ops g_table4_0_ops
#define g_table4_691_emit g_table4_640_emit
#define g_table4_691_ops g_table4_0_ops
#define g_table4_692_emit g_table4_640_emit
#define g_table4_692_ops g_table4_0_ops
#define g_table4_693_emit g_table4_640_emit
#define g_table4_693_ops g_table4_0_ops
#define g_table4_694_emit g_table4_640_emit
#define g_table4_694_ops g_table4_0_ops
#define g_table4_695_emit g_table4_640_emit
#define g_table4_695_ops g_table4_0_ops
#define g_table4_696_emit g_table4_640_emit
#define g_table4_696_ops g_table4_0_ops
#define g_table4_697_emit g_table4_640_emit
#define g_table4_697_ops g_table4_0_ops
#define g_table4_698_emit g_table4_640_emit
#define g_table4_698_ops g_table4_0_ops
#define g_table4_699_emit g_table4_640_emit
#define g_table4_699_ops g_table4_0_ops
#define g_table4_700_emit g_table4_640_emit
#define g_table4_700_ops g_table4_0_ops
#define g_table4_701_emit g_table4_640_emit
#define g_table4_701_ops g_table4_0_ops
#define g_table4_702_emit g_table4_640_emit
#define g_table4_702_ops g_table4_0_ops
#define g_table4_703_emit g_table4_640_emit
#define g_table4_703_ops g_table4_0_ops
#define g_table4_704_emit g_table4_640_emit
#define g_table4_704_ops g_table4_0_ops
#define g_table4_705_emit g_table4_640_emit
#define g_table4_705_ops g_table4_0_ops
#define g_table4_706_emit g_table4_640_emit
#define g_table4_706_ops g_table4_0_ops
#define g_table4_707_emit g_table4_640_emit
#define g_table4_707_ops g_table4_0_ops
#define g_table4_708_emit g_table4_640_emit
#define g_table4_708_ops g_table4_0_ops
#define g_table4_709_emit g_table4_640_emit
#define g_table4_709_ops g_table4_0_ops
#define g_table4_710_emit g_table4_640_emit
#define g_table4_710_ops g_table4_0_ops
#define g_table4_711_emit g_table4_640_emit
#define g_table4_711_ops g_table4_0_ops
#define g_table4_712_emit g_table4_640_emit
#define g_table4_712_ops g_table4_0_ops
#define g_table4_713_emit g_table4_640_emit
#define g_table4_713_ops g_table4_0_ops
#define g_table4_714_emit g_table4_640_emit
#define g_table4_714_ops g_table4_0_ops
#define g_table4_715_emit g_table4_640_emit
#define g_table4_715_ops g_table4_0_ops
#define g_table4_716_emit g_table4_640_emit
#define g_table4_716_ops g_table4_0_ops
#define g_table4_717_emit g_table4_640_emit
#define g_table4_717_ops g_table4_0_ops
#define g_table4_718_emit g_table4_640_emit
#define g_table4_718_ops g_table4_0_ops
#define g_table4_719_emit g_table4_640_emit
#define g_table4_719_ops g_table4_0_ops
#define g_table4_720_emit g_table4_640_emit
#define g_table4_720_ops g_table4_0_ops
#define g_table4_721_emit g_table4_640_emit
#define g_table4_721_ops g_table4_0_ops
#define g_table4_722_emit g_table4_640_emit
#define g_table4_722_ops g_table4_0_ops
#define g_table4_723_emit g_table4_640_emit
#define g_table4_723_ops g_table4_0_ops
#define g_table4_724_emit g_table4_640_emit
#define g_table4_724_ops g_table4_0_ops
#define g_table4_725_emit g_table4_640_emit
#define g_table4_725_ops g_table4_0_ops
#define g_table4_726_emit g_table4_640_emit
#define g_table4_726_ops g_table4_0_ops
#define g_table4_727_emit g_table4_640_emit
#define g_table4_727_ops g_table4_0_ops
#define g_table4_728_emit g_table4_640_emit
#define g_table4_728_ops g_table4_0_ops
#define g_table4_729_emit g_table4_640_emit
#define g_table4_729_ops g_table4_0_ops
#define g_table4_730_emit g_table4_640_emit
#define g_table4_730_ops g_table4_0_ops
#define g_table4_731_emit g_table4_640_emit
#define g_table4_731_ops g_table4_0_ops
#define g_table4_732_emit g_table4_640_emit
#define g_table4_732_ops g_table4_0_ops
#define g_table4_733_emit g_table4_640_emit
#define g_table4_733_ops g_table4_0_ops
#define g_table4_734_emit g_table4_640_emit
#define g_table4_734_ops g_table4_0_ops
#define g_table4_735_emit g_table4_640_emit
#define g_table4_735_ops g_table4_0_ops
#define g_table4_736_emit g_table4_640_emit
#define g_table4_736_ops g_table4_0_ops
#define g_table4_737_emit g_table4_640_emit
#define g_table4_737_ops g_table4_0_ops
#define g_table4_738_emit g_table4_640_emit
#define g_table4_738_ops g_table4_0_ops
#define g_table4_739_emit g_table4_640_emit
#define g_table4_739_ops g_table4_0_ops
#define g_table4_740_emit g_table4_640_emit
#define g_table4_740_ops g_table4_0_ops
#define g_table4_741_emit g_table4_640_emit
#define g_table4_741_ops g_table4_0_ops
#define g_table4_742_emit g_table4_640_emit
#define g_table4_742_ops g_table4_0_ops
#define g_table4_743_emit g_table4_640_emit
#define g_table4_743_ops g_table4_0_ops
#define g_table4_744_emit g_table4_640_emit
#define g_table4_744_ops g_table4_0_ops
#define g_table4_745_emit g_table4_640_emit
#define g_table4_745_ops g_table4_0_ops
#define g_table4_746_emit g_table4_640_emit
#define g_table4_746_ops g_table4_0_ops
#define g_table4_747_emit g_table4_640_emit
#define g_table4_747_ops g_table4_0_ops
#define g_table4_748_emit g_table4_640_emit
#define g_table4_748_ops g_table4_0_ops
#define g_table4_749_emit g_table4_640_emit
#define g_table4_749_ops g_table4_0_ops
#define g_table4_750_emit g_table4_640_emit
#define g_table4_750_ops g_table4_0_ops
#define g_table4_751_emit g_table4_640_emit
#define g_table4_751_ops g_table4_0_ops
#define g_table4_752_emit g_table4_640_emit
#define g_table4_752_ops g_table4_0_ops
#define g_table4_753_emit g_table4_640_emit
#define g_table4_753_ops g_table4_0_ops
#define g_table4_754_emit g_table4_640_emit
#define g_table4_754_ops g_table4_0_ops
#define g_table4_755_emit g_table4_640_emit
#define g_table4_755_ops g_table4_0_ops
#define g_table4_756_emit g_table4_640_emit
#define g_table4_756_ops g_table4_0_ops
#define g_table4_757_emit g_table4_640_emit
#define g_table4_757_ops g_table4_0_ops
#define g_table4_758_emit g_table4_640_emit
#define g_table4_758_ops g_table4_0_ops
#define g_table4_759_emit g_table4_640_emit
#define g_table4_759_ops g_table4_0_ops
#define g_table4_760_emit g_table4_640_emit
#define g_table4_760_ops g_table4_0_ops
#define g_table4_761_emit g_table4_640_emit
#define g_table4_761_ops g_table4_0_ops
#define g_table4_762_emit g_table4_640_emit
#define g_table4_762_ops g_table4_0_ops
#define g_table4_763_emit g_table4_640_emit
#define g_table4_763_ops g_table4_0_ops
#define g_table4_764_emit g_table4_640_emit
#define g_table4_764_ops g_table4_0_ops
#define g_table4_765_emit g_table4_640_emit
#define g_table4_765_ops g_table4_0_ops
#define g_table4_766_emit g_table4_640_emit
#define g_table4_766_ops g_table4_0_ops
#define g_table4_767_emit g_table4_640_emit
#define g_table4_767_ops g_table4_0_ops
static const uint8_t g_table4_768_emit[1] = {0x5e};
static const uint16_t g_table4_768_ops[1024] = {
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
    0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004};
#define g_table4_769_emit g_table4_768_emit
#define g_table4_769_ops g_table4_768_ops
#define g_table4_770_emit g_table4_768_emit
#define g_table4_770_ops g_table4_768_ops
#define g_table4_771_emit g_table4_768_emit
#define g_table4_771_ops g_table4_768_ops
#define g_table4_772_emit g_table4_768_emit
#define g_table4_772_ops g_table4_768_ops
#define g_table4_773_emit g_table4_768_emit
#define g_table4_773_ops g_table4_768_ops
#define g_table4_774_emit g_table4_768_emit
#define g_table4_774_ops g_table4_768_ops
#define g_table4_775_emit g_table4_768_emit
#define g_table4_775_ops g_table4_768_ops
#define g_table4_776_emit g_table4_768_emit
#define g_table4_776_ops g_table4_768_ops
#define g_table4_777_emit g_table4_768_emit
#define g_table4_777_ops g_table4_768_ops
#define g_table4_778_emit g_table4_768_emit
#define g_table4_778_ops g_table4_768_ops
#define g_table4_779_emit g_table4_768_emit
#define g_table4_779_ops g_table4_768_ops
#define g_table4_780_emit g_table4_768_emit
#define g_table4_780_ops g_table4_768_ops
#define g_table4_781_emit g_table4_768_emit
#define g_table4_781_ops g_table4_768_ops
#define g_table4_782_emit g_table4_768_emit
#define g_table4_782_ops g_table4_768_ops
#define g_table4_783_emit g_table4_768_emit
#define g_table4_783_ops g_table4_768_ops
#define g_table4_784_emit g_table4_768_emit
#define g_table4_784_ops g_table4_768_ops
#define g_table4_785_emit g_table4_768_emit
#define g_table4_785_ops g_table4_768_ops
#define g_table4_786_emit g_table4_768_emit
#define g_table4_786_ops g_table4_768_ops
#define g_table4_787_emit g_table4_768_emit
#define g_table4_787_ops g_table4_768_ops
#define g_table4_788_emit g_table4_768_emit
#define g_table4_788_ops g_table4_768_ops
#define g_table4_789_emit g_table4_768_emit
#define g_table4_789_ops g_table4_768_ops
#define g_table4_790_emit g_table4_768_emit
#define g_table4_790_ops g_table4_768_ops
#define g_table4_791_emit g_table4_768_emit
#define g_table4_791_ops g_table4_768_ops
#define g_table4_792_emit g_table4_768_emit
#define g_table4_792_ops g_table4_768_ops
#define g_table4_793_emit g_table4_768_emit
#define g_table4_793_ops g_table4_768_ops
#define g_table4_794_emit g_table4_768_emit
#define g_table4_794_ops g_table4_768_ops
#define g_table4_795_emit g_table4_768_emit
#define g_table4_795_ops g_table4_768_ops
#define g_table4_796_emit g_table4_768_emit
#define g_table4_796_ops g_table4_768_ops
#define g_table4_797_emit g_table4_768_emit
#define g_table4_797_ops g_table4_768_ops
#define g_table4_798_emit g_table4_768_emit
#define g_table4_798_ops g_table4_768_ops
#define g_table4_799_emit g_table4_768_emit
#define g_table4_799_ops g_table4_768_ops
#define g_table4_800_emit g_table4_768_emit
#define g_table4_800_ops g_table4_768_ops
#define g_table4_801_emit g_table4_768_emit
#define g_table4_801_ops g_table4_768_ops
#define g_table4_802_emit g_table4_768_emit
#define g_table4_802_ops g_table4_768_ops
#define g_table4_803_emit g_table4_768_emit
#define g_table4_803_ops g_table4_768_ops
#define g_table4_804_emit g_table4_768_emit
#define g_table4_804_ops g_table4_768_ops
#define g_table4_805_emit g_table4_768_emit
#define g_table4_805_ops g_table4_768_ops
#define g_table4_806_emit g_table4_768_emit
#define g_table4_806_ops g_table4_768_ops
#define g_table4_807_emit g_table4_768_emit
#define g_table4_807_ops g_table4_768_ops
#define g_table4_808_emit g_table4_768_emit
#define g_table4_808_ops g_table4_768_ops
#define g_table4_809_emit g_table4_768_emit
#define g_table4_809_ops g_table4_768_ops
#define g_table4_810_emit g_table4_768_emit
#define g_table4_810_ops g_table4_768_ops
#define g_table4_811_emit g_table4_768_emit
#define g_table4_811_ops g_table4_768_ops
#define g_table4_812_emit g_table4_768_emit
#define g_table4_812_ops g_table4_768_ops
#define g_table4_813_emit g_table4_768_emit
#define g_table4_813_ops g_table4_768_ops
#define g_table4_814_emit g_table4_768_emit
#define g_table4_814_ops g_table4_768_ops
#define g_table4_815_emit g_table4_768_emit
#define g_table4_815_ops g_table4_768_ops
#define g_table4_816_emit g_table4_768_emit
#define g_table4_816_ops g_table4_768_ops
#define g_table4_817_emit g_table4_768_emit
#define g_table4_817_ops g_table4_768_ops
#define g_table4_818_emit g_table4_768_emit
#define g_table4_818_ops g_table4_768_ops
#define g_table4_819_emit g_table4_768_emit
#define g_table4_819_ops g_table4_768_ops
#define g_table4_820_emit g_table4_768_emit
#define g_table4_820_ops g_table4_768_ops
#define g_table4_821_emit g_table4_768_emit
#define g_table4_821_ops g_table4_768_ops
#define g_table4_822_emit g_table4_768_emit
#define g_table4_822_ops g_table4_768_ops
#define g_table4_823_emit g_table4_768_emit
#define g_table4_823_ops g_table4_768_ops
#define g_table4_824_emit g_table4_768_emit
#define g_table4_824_ops g_table4_768_ops
#define g_table4_825_emit g_table4_768_emit
#define g_table4_825_ops g_table4_768_ops
#define g_table4_826_emit g_table4_768_emit
#define g_table4_826_ops g_table4_768_ops
#define g_table4_827_emit g_table4_768_emit
#define g_table4_827_ops g_table4_768_ops
#define g_table4_828_emit g_table4_768_emit
#define g_table4_828_ops g_table4_768_ops
#define g_table4_829_emit g_table4_768_emit
#define g_table4_829_ops g_table4_768_ops
#define g_table4_830_emit g_table4_768_emit
#define g_table4_830_ops g_table4_768_ops
#define g_table4_831_emit g_table4_768_emit
#define g_table4_831_ops g_table4_768_ops
static const uint8_t g_table4_832_emit[1] = {0x7d};
#define g_table4_832_ops g_table4_768_ops
#define g_table4_833_emit g_table4_832_emit
#define g_table4_833_ops g_table4_768_ops
#define g_table4_834_emit g_table4_832_emit
#define g_table4_834_ops g_table4_768_ops
#define g_table4_835_emit g_table4_832_emit
#define g_table4_835_ops g_table4_768_ops
#define g_table4_836_emit g_table4_832_emit
#define g_table4_836_ops g_table4_768_ops
#define g_table4_837_emit g_table4_832_emit
#define g_table4_837_ops g_table4_768_ops
#define g_table4_838_emit g_table4_832_emit
#define g_table4_838_ops g_table4_768_ops
#define g_table4_839_emit g_table4_832_emit
#define g_table4_839_ops g_table4_768_ops
#define g_table4_840_emit g_table4_832_emit
#define g_table4_840_ops g_table4_768_ops
#define g_table4_841_emit g_table4_832_emit
#define g_table4_841_ops g_table4_768_ops
#define g_table4_842_emit g_table4_832_emit
#define g_table4_842_ops g_table4_768_ops
#define g_table4_843_emit g_table4_832_emit
#define g_table4_843_ops g_table4_768_ops
#define g_table4_844_emit g_table4_832_emit
#define g_table4_844_ops g_table4_768_ops
#define g_table4_845_emit g_table4_832_emit
#define g_table4_845_ops g_table4_768_ops
#define g_table4_846_emit g_table4_832_emit
#define g_table4_846_ops g_table4_768_ops
#define g_table4_847_emit g_table4_832_emit
#define g_table4_847_ops g_table4_768_ops
#define g_table4_848_emit g_table4_832_emit
#define g_table4_848_ops g_table4_768_ops
#define g_table4_849_emit g_table4_832_emit
#define g_table4_849_ops g_table4_768_ops
#define g_table4_850_emit g_table4_832_emit
#define g_table4_850_ops g_table4_768_ops
#define g_table4_851_emit g_table4_832_emit
#define g_table4_851_ops g_table4_768_ops
#define g_table4_852_emit g_table4_832_emit
#define g_table4_852_ops g_table4_768_ops
#define g_table4_853_emit g_table4_832_emit
#define g_table4_853_ops g_table4_768_ops
#define g_table4_854_emit g_table4_832_emit
#define g_table4_854_ops g_table4_768_ops
#define g_table4_855_emit g_table4_832_emit
#define g_table4_855_ops g_table4_768_ops
#define g_table4_856_emit g_table4_832_emit
#define g_table4_856_ops g_table4_768_ops
#define g_table4_857_emit g_table4_832_emit
#define g_table4_857_ops g_table4_768_ops
#define g_table4_858_emit g_table4_832_emit
#define g_table4_858_ops g_table4_768_ops
#define g_table4_859_emit g_table4_832_emit
#define g_table4_859_ops g_table4_768_ops
#define g_table4_860_emit g_table4_832_emit
#define g_table4_860_ops g_table4_768_ops
#define g_table4_861_emit g_table4_832_emit
#define g_table4_861_ops g_table4_768_ops
#define g_table4_862_emit g_table4_832_emit
#define g_table4_862_ops g_table4_768_ops
#define g_table4_863_emit g_table4_832_emit
#define g_table4_863_ops g_table4_768_ops
#define g_table4_864_emit g_table4_832_emit
#define g_table4_864_ops g_table4_768_ops
#define g_table4_865_emit g_table4_832_emit
#define g_table4_865_ops g_table4_768_ops
#define g_table4_866_emit g_table4_832_emit
#define g_table4_866_ops g_table4_768_ops
#define g_table4_867_emit g_table4_832_emit
#define g_table4_867_ops g_table4_768_ops
#define g_table4_868_emit g_table4_832_emit
#define g_table4_868_ops g_table4_768_ops
#define g_table4_869_emit g_table4_832_emit
#define g_table4_869_ops g_table4_768_ops
#define g_table4_870_emit g_table4_832_emit
#define g_table4_870_ops g_table4_768_ops
#define g_table4_871_emit g_table4_832_emit
#define g_table4_871_ops g_table4_768_ops
#define g_table4_872_emit g_table4_832_emit
#define g_table4_872_ops g_table4_768_ops
#define g_table4_873_emit g_table4_832_emit
#define g_table4_873_ops g_table4_768_ops
#define g_table4_874_emit g_table4_832_emit
#define g_table4_874_ops g_table4_768_ops
#define g_table4_875_emit g_table4_832_emit
#define g_table4_875_ops g_table4_768_ops
#define g_table4_876_emit g_table4_832_emit
#define g_table4_876_ops g_table4_768_ops
#define g_table4_877_emit g_table4_832_emit
#define g_table4_877_ops g_table4_768_ops
#define g_table4_878_emit g_table4_832_emit
#define g_table4_878_ops g_table4_768_ops
#define g_table4_879_emit g_table4_832_emit
#define g_table4_879_ops g_table4_768_ops
#define g_table4_880_emit g_table4_832_emit
#define g_table4_880_ops g_table4_768_ops
#define g_table4_881_emit g_table4_832_emit
#define g_table4_881_ops g_table4_768_ops
#define g_table4_882_emit g_table4_832_emit
#define g_table4_882_ops g_table4_768_ops
#define g_table4_883_emit g_table4_832_emit
#define g_table4_883_ops g_table4_768_ops
#define g_table4_884_emit g_table4_832_emit
#define g_table4_884_ops g_table4_768_ops
#define g_table4_885_emit g_table4_832_emit
#define g_table4_885_ops g_table4_768_ops
#define g_table4_886_emit g_table4_832_emit
#define g_table4_886_ops g_table4_768_ops
#define g_table4_887_emit g_table4_832_emit
#define g_table4_887_ops g_table4_768_ops
#define g_table4_888_emit g_table4_832_emit
#define g_table4_888_ops g_table4_768_ops
#define g_table4_889_emit g_table4_832_emit
#define g_table4_889_ops g_table4_768_ops
#define g_table4_890_emit g_table4_832_emit
#define g_table4_890_ops g_table4_768_ops
#define g_table4_891_emit g_table4_832_emit
#define g_table4_891_ops g_table4_768_ops
#define g_table4_892_emit g_table4_832_emit
#define g_table4_892_ops g_table4_768_ops
#define g_table4_893_emit g_table4_832_emit
#define g_table4_893_ops g_table4_768_ops
#define g_table4_894_emit g_table4_832_emit
#define g_table4_894_ops g_table4_768_ops
#define g_table4_895_emit g_table4_832_emit
#define g_table4_895_ops g_table4_768_ops
static const uint8_t g_table4_896_emit[1] = {0x3c};
static const uint16_t g_table4_896_ops[1024] = {
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005};
#define g_table4_897_emit g_table4_896_emit
#define g_table4_897_ops g_table4_896_ops
#define g_table4_898_emit g_table4_896_emit
#define g_table4_898_ops g_table4_896_ops
#define g_table4_899_emit g_table4_896_emit
#define g_table4_899_ops g_table4_896_ops
#define g_table4_900_emit g_table4_896_emit
#define g_table4_900_ops g_table4_896_ops
#define g_table4_901_emit g_table4_896_emit
#define g_table4_901_ops g_table4_896_ops
#define g_table4_902_emit g_table4_896_emit
#define g_table4_902_ops g_table4_896_ops
#define g_table4_903_emit g_table4_896_emit
#define g_table4_903_ops g_table4_896_ops
#define g_table4_904_emit g_table4_896_emit
#define g_table4_904_ops g_table4_896_ops
#define g_table4_905_emit g_table4_896_emit
#define g_table4_905_ops g_table4_896_ops
#define g_table4_906_emit g_table4_896_emit
#define g_table4_906_ops g_table4_896_ops
#define g_table4_907_emit g_table4_896_emit
#define g_table4_907_ops g_table4_896_ops
#define g_table4_908_emit g_table4_896_emit
#define g_table4_908_ops g_table4_896_ops
#define g_table4_909_emit g_table4_896_emit
#define g_table4_909_ops g_table4_896_ops
#define g_table4_910_emit g_table4_896_emit
#define g_table4_910_ops g_table4_896_ops
#define g_table4_911_emit g_table4_896_emit
#define g_table4_911_ops g_table4_896_ops
#define g_table4_912_emit g_table4_896_emit
#define g_table4_912_ops g_table4_896_ops
#define g_table4_913_emit g_table4_896_emit
#define g_table4_913_ops g_table4_896_ops
#define g_table4_914_emit g_table4_896_emit
#define g_table4_914_ops g_table4_896_ops
#define g_table4_915_emit g_table4_896_emit
#define g_table4_915_ops g_table4_896_ops
#define g_table4_916_emit g_table4_896_emit
#define g_table4_916_ops g_table4_896_ops
#define g_table4_917_emit g_table4_896_emit
#define g_table4_917_ops g_table4_896_ops
#define g_table4_918_emit g_table4_896_emit
#define g_table4_918_ops g_table4_896_ops
#define g_table4_919_emit g_table4_896_emit
#define g_table4_919_ops g_table4_896_ops
#define g_table4_920_emit g_table4_896_emit
#define g_table4_920_ops g_table4_896_ops
#define g_table4_921_emit g_table4_896_emit
#define g_table4_921_ops g_table4_896_ops
#define g_table4_922_emit g_table4_896_emit
#define g_table4_922_ops g_table4_896_ops
#define g_table4_923_emit g_table4_896_emit
#define g_table4_923_ops g_table4_896_ops
#define g_table4_924_emit g_table4_896_emit
#define g_table4_924_ops g_table4_896_ops
#define g_table4_925_emit g_table4_896_emit
#define g_table4_925_ops g_table4_896_ops
#define g_table4_926_emit g_table4_896_emit
#define g_table4_926_ops g_table4_896_ops
#define g_table4_927_emit g_table4_896_emit
#define g_table4_927_ops g_table4_896_ops
static const uint8_t g_table4_928_emit[1] = {0x60};
#define g_table4_928_ops g_table4_896_ops
#define g_table4_929_emit g_table4_928_emit
#define g_table4_929_ops g_table4_896_ops
#define g_table4_930_emit g_table4_928_emit
#define g_table4_930_ops g_table4_896_ops
#define g_table4_931_emit g_table4_928_emit
#define g_table4_931_ops g_table4_896_ops
#define g_table4_932_emit g_table4_928_emit
#define g_table4_932_ops g_table4_896_ops
#define g_table4_933_emit g_table4_928_emit
#define g_table4_933_ops g_table4_896_ops
#define g_table4_934_emit g_table4_928_emit
#define g_table4_934_ops g_table4_896_ops
#define g_table4_935_emit g_table4_928_emit
#define g_table4_935_ops g_table4_896_ops
#define g_table4_936_emit g_table4_928_emit
#define g_table4_936_ops g_table4_896_ops
#define g_table4_937_emit g_table4_928_emit
#define g_table4_937_ops g_table4_896_ops
#define g_table4_938_emit g_table4_928_emit
#define g_table4_938_ops g_table4_896_ops
#define g_table4_939_emit g_table4_928_emit
#define g_table4_939_ops g_table4_896_ops
#define g_table4_940_emit g_table4_928_emit
#define g_table4_940_ops g_table4_896_ops
#define g_table4_941_emit g_table4_928_emit
#define g_table4_941_ops g_table4_896_ops
#define g_table4_942_emit g_table4_928_emit
#define g_table4_942_ops g_table4_896_ops
#define g_table4_943_emit g_table4_928_emit
#define g_table4_943_ops g_table4_896_ops
#define g_table4_944_emit g_table4_928_emit
#define g_table4_944_ops g_table4_896_ops
#define g_table4_945_emit g_table4_928_emit
#define g_table4_945_ops g_table4_896_ops
#define g_table4_946_emit g_table4_928_emit
#define g_table4_946_ops g_table4_896_ops
#define g_table4_947_emit g_table4_928_emit
#define g_table4_947_ops g_table4_896_ops
#define g_table4_948_emit g_table4_928_emit
#define g_table4_948_ops g_table4_896_ops
#define g_table4_949_emit g_table4_928_emit
#define g_table4_949_ops g_table4_896_ops
#define g_table4_950_emit g_table4_928_emit
#define g_table4_950_ops g_table4_896_ops
#define g_table4_951_emit g_table4_928_emit
#define g_table4_951_ops g_table4_896_ops
#define g_table4_952_emit g_table4_928_emit
#define g_table4_952_ops g_table4_896_ops
#define g_table4_953_emit g_table4_928_emit
#define g_table4_953_ops g_table4_896_ops
#define g_table4_954_emit g_table4_928_emit
#define g_table4_954_ops g_table4_896_ops
#define g_table4_955_emit g_table4_928_emit
#define g_table4_955_ops g_table4_896_ops
#define g_table4_956_emit g_table4_928_emit
#define g_table4_956_ops g_table4_896_ops
#define g_table4_957_emit g_table4_928_emit
#define g_table4_957_ops g_table4_896_ops
#define g_table4_958_emit g_table4_928_emit
#define g_table4_958_ops g_table4_896_ops
#define g_table4_959_emit g_table4_928_emit
#define g_table4_959_ops g_table4_896_ops
static const uint8_t g_table4_960_emit[1] = {0x7b};
#define g_table4_960_ops g_table4_896_ops
#define g_table4_961_emit g_table4_960_emit
#define g_table4_961_ops g_table4_896_ops
#define g_table4_962_emit g_table4_960_emit
#define g_table4_962_ops g_table4_896_ops
#define g_table4_963_emit g_table4_960_emit
#define g_table4_963_ops g_table4_896_ops
#define g_table4_964_emit g_table4_960_emit
#define g_table4_964_ops g_table4_896_ops
#define g_table4_965_emit g_table4_960_emit
#define g_table4_965_ops g_table4_896_ops
#define g_table4_966_emit g_table4_960_emit
#define g_table4_966_ops g_table4_896_ops
#define g_table4_967_emit g_table4_960_emit
#define g_table4_967_ops g_table4_896_ops
#define g_table4_968_emit g_table4_960_emit
#define g_table4_968_ops g_table4_896_ops
#define g_table4_969_emit g_table4_960_emit
#define g_table4_969_ops g_table4_896_ops
#define g_table4_970_emit g_table4_960_emit
#define g_table4_970_ops g_table4_896_ops
#define g_table4_971_emit g_table4_960_emit
#define g_table4_971_ops g_table4_896_ops
#define g_table4_972_emit g_table4_960_emit
#define g_table4_972_ops g_table4_896_ops
#define g_table4_973_emit g_table4_960_emit
#define g_table4_973_ops g_table4_896_ops
#define g_table4_974_emit g_table4_960_emit
#define g_table4_974_ops g_table4_896_ops
#define g_table4_975_emit g_table4_960_emit
#define g_table4_975_ops g_table4_896_ops
#define g_table4_976_emit g_table4_960_emit
#define g_table4_976_ops g_table4_896_ops
#define g_table4_977_emit g_table4_960_emit
#define g_table4_977_ops g_table4_896_ops
#define g_table4_978_emit g_table4_960_emit
#define g_table4_978_ops g_table4_896_ops
#define g_table4_979_emit g_table4_960_emit
#define g_table4_979_ops g_table4_896_ops
#define g_table4_980_emit g_table4_960_emit
#define g_table4_980_ops g_table4_896_ops
#define g_table4_981_emit g_table4_960_emit
#define g_table4_981_ops g_table4_896_ops
#define g_table4_982_emit g_table4_960_emit
#define g_table4_982_ops g_table4_896_ops
#define g_table4_983_emit g_table4_960_emit
#define g_table4_983_ops g_table4_896_ops
#define g_table4_984_emit g_table4_960_emit
#define g_table4_984_ops g_table4_896_ops
#define g_table4_985_emit g_table4_960_emit
#define g_table4_985_ops g_table4_896_ops
#define g_table4_986_emit g_table4_960_emit
#define g_table4_986_ops g_table4_896_ops
#define g_table4_987_emit g_table4_960_emit
#define g_table4_987_ops g_table4_896_ops
#define g_table4_988_emit g_table4_960_emit
#define g_table4_988_ops g_table4_896_ops
#define g_table4_989_emit g_table4_960_emit
#define g_table4_989_ops g_table4_896_ops
#define g_table4_990_emit g_table4_960_emit
#define g_table4_990_ops g_table4_896_ops
#define g_table4_991_emit g_table4_960_emit
#define g_table4_991_ops g_table4_896_ops
static const uint8_t g_table4_992_emit[1] = {0x5c};
static const uint16_t g_table4_992_ops[1024] = {
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009};
#define g_table4_993_emit g_table4_992_emit
#define g_table4_993_ops g_table4_992_ops
static const uint8_t g_table4_994_emit[1] = {0xc3};
#define g_table4_994_ops g_table4_992_ops
#define g_table4_995_emit g_table4_994_emit
#define g_table4_995_ops g_table4_992_ops
static const uint8_t g_table4_996_emit[1] = {0xd0};
#define g_table4_996_ops g_table4_992_ops
#define g_table4_997_emit g_table4_996_emit
#define g_table4_997_ops g_table4_992_ops
static const uint8_t g_table4_998_emit[1] = {0x80};
static const uint16_t g_table4_998_ops[1024] = {
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a,
    0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x000a};
static const uint8_t g_table4_999_emit[1] = {0x82};
#define g_table4_999_ops g_table4_998_ops
static const uint8_t g_table4_1000_emit[1] = {0x83};
#define g_table4_1000_ops g_table4_998_ops
static const uint8_t g_table4_1001_emit[1] = {0xa2};
#define g_table4_1001_ops g_table4_998_ops
static const uint8_t g_table4_1002_emit[1] = {0xb8};
#define g_table4_1002_ops g_table4_998_ops
static const uint8_t g_table4_1003_emit[1] = {0xc2};
#define g_table4_1003_ops g_table4_998_ops
static const uint8_t g_table4_1004_emit[1] = {0xe0};
#define g_table4_1004_ops g_table4_998_ops
static const uint8_t g_table4_1005_emit[1] = {0xe2};
#define g_table4_1005_ops g_table4_998_ops
static const uint8_t g_table4_1006_emit[2] = {0x99, 0xa1};
static const uint16_t g_table4_1006_ops[1024] = {
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b,
    0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b, 0x004b};
static const uint8_t g_table4_1007_emit[2] = {0xa7, 0xac};
#define g_table4_1007_ops g_table4_1006_ops
static const uint8_t g_table4_1008_emit[2] = {0xb0, 0xb1};
#define g_table4_1008_ops g_table4_1006_ops
static const uint8_t g_table4_1009_emit[2] = {0xb3, 0xd1};
#define g_table4_1009_ops g_table4_1006_ops
static const uint8_t g_table4_1010_emit[2] = {0xd8, 0xd9};
#define g_table4_1010_ops g_table4_1006_ops
static const uint8_t g_table4_1011_emit[2] = {0xe3, 0xe5};
#define g_table4_1011_ops g_table4_1006_ops
static const uint8_t g_table4_1012_emit[3] = {0xe6, 0x81, 0x84};
static const uint16_t g_table4_1012_ops[1024] = {
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b,
    0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x000b, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c};
static const uint8_t g_table4_1013_emit[4] = {0x85, 0x86, 0x88, 0x92};
static const uint16_t g_table4_1013_ops[1024] = {
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c,
    0x000c, 0x000c, 0x000c, 0x000c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c,
    0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x004c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c, 0x008c,
    0x008c, 0x008c, 0x008c, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc,
    0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc, 0x00cc};
static const uint8_t g_table4_1014_emit[4] = {0x9a, 0x9c, 0xa0, 0xa3};
#define g_table4_1014_ops g_table4_1013_ops
static const uint8_t g_table4_1015_emit[4] = {0xa4, 0xa9, 0xaa, 0xad};
#define g_table4_1015_ops g_table4_1013_ops
static const uint8_t g_table4_1016_emit[4] = {0xb2, 0xb5, 0xb9, 0xba};
#define g_table4_1016_ops g_table4_1013_ops
static const uint8_t g_table4_1017_emit[4] = {0xbb, 0xbd, 0xbe, 0xc4};
#define g_table4_1017_ops g_table4_1013_ops
static const uint8_t g_table4_1018_emit[4] = {0xc6, 0xe4, 0xe8, 0xe9};
#define g_table4_1018_ops g_table4_1013_ops
static const uint8_t g_table4_1019_emit[8] = {0x01, 0x87, 0x89, 0x8a,
                                              0x8b, 0x8c, 0x8d, 0x8f};
static const uint16_t g_table4_1019_ops[1024] = {
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d, 0x014d,
    0x014d, 0x014d, 0x014d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x018d,
    0x018d, 0x018d, 0x018d, 0x018d, 0x018d, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd,
    0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd, 0x01cd};
static const uint8_t g_table4_1020_emit[8] = {0x93, 0x95, 0x96, 0x97,
                                              0x98, 0x9b, 0x9d, 0x9e};
#define g_table4_1020_ops g_table4_1019_ops
static const uint8_t g_table4_1021_emit[8] = {0xa5, 0xa6, 0xa8, 0xae,
                                              0xaf, 0xb4, 0xb6, 0xb7};
#define g_table4_1021_ops g_table4_1019_ops
static const uint8_t g_table4_1022_emit[11] = {
    0xbc, 0xbf, 0xc5, 0xe7, 0xef, 0x09, 0x8e, 0x90, 0x91, 0x94, 0x9f};
static const uint16_t g_table4_1022_ops[1024] = {
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d, 0x000d,
    0x000d, 0x000d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d, 0x004d,
    0x004d, 0x004d, 0x004d, 0x004d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d,
    0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x008d, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd,
    0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x00cd, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d,
    0x010d, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e, 0x018e,
    0x018e, 0x018e, 0x018e, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x01ce,
    0x01ce, 0x01ce, 0x01ce, 0x01ce, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x020e,
    0x020e, 0x020e, 0x020e, 0x020e, 0x020e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e,
    0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x024e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e,
    0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e, 0x028e};
static const uint8_t g_table4_1023_emit[76] = {
    0xab, 0xce, 0xd7, 0xe1, 0xec, 0xed, 0xc7, 0xcf, 0xea, 0xeb, 0xc0,
    0xc1, 0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda, 0xdb, 0xee, 0xf0,
    0xf2, 0xf3, 0xff, 0xcb, 0xcc, 0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf,
    0xf1, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16};
static const uint16_t g_table4_1023_ops[1024] = {
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e, 0x000e,
    0x000e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e, 0x004e,
    0x004e, 0x004e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e, 0x008e,
    0x008e, 0x008e, 0x008e, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x00ce,
    0x00ce, 0x00ce, 0x00ce, 0x00ce, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x010e,
    0x010e, 0x010e, 0x010e, 0x010e, 0x010e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e,
    0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x014e, 0x018f, 0x018f, 0x018f,
    0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f,
    0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f,
    0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f, 0x018f,
    0x018f, 0x018f, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf,
    0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf,
    0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf,
    0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x01cf, 0x020f, 0x020f,
    0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f,
    0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f,
    0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f, 0x020f,
    0x020f, 0x020f, 0x020f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f,
    0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f,
    0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f,
    0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x024f, 0x0290,
    0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x0290,
    0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x0290, 0x02d0, 0x02d0, 0x02d0,
    0x02d0, 0x02d0, 0x02d0, 0x02d0, 0x02d0, 0x02d0, 0x02d0, 0x02d0, 0x02d0,
    0x02d0, 0x02d0, 0x02d0, 0x02d0, 0x0310, 0x0310, 0x0310, 0x0310, 0x0310,
    0x0310, 0x0310, 0x0310, 0x0310, 0x0310, 0x0310, 0x0310, 0x0310, 0x0310,
    0x0310, 0x0310, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350,
    0x0350, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350, 0x0350,
    0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x0390,
    0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x0390, 0x03d0, 0x03d0,
    0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x03d0,
    0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x03d0, 0x0410, 0x0410, 0x0410, 0x0410,
    0x0410, 0x0410, 0x0410, 0x0410, 0x0410, 0x0410, 0x0410, 0x0410, 0x0410,
    0x0410, 0x0410, 0x0410, 0x0450, 0x0450, 0x0450, 0x0450, 0x0450, 0x0450,
    0x0450, 0x0450, 0x0450, 0x0450, 0x0450, 0x0450, 0x0450, 0x0450, 0x0450,
    0x0450, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490,
    0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x0490, 0x04d0,
    0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0,
    0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x04d0, 0x0510, 0x0510, 0x0510,
    0x0510, 0x0510, 0x0510, 0x0510, 0x0510, 0x0510, 0x0510, 0x0510, 0x0510,
    0x0510, 0x0510, 0x0510, 0x0510, 0x0550, 0x0550, 0x0550, 0x0550, 0x0550,
    0x0550, 0x0550, 0x0550, 0x0550, 0x0550, 0x0550, 0x0550, 0x0550, 0x0550,
    0x0550, 0x0550, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590,
    0x0590, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590, 0x0590,
    0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0,
    0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x05d0, 0x0610, 0x0610,
    0x0610, 0x0610, 0x0610, 0x0610, 0x0610, 0x0610, 0x0610, 0x0610, 0x0610,
    0x0610, 0x0610, 0x0610, 0x0610, 0x0610, 0x0651, 0x0651, 0x0651, 0x0651,
    0x0651, 0x0651, 0x0651, 0x0651, 0x0691, 0x0691, 0x0691, 0x0691, 0x0691,
    0x0691, 0x0691, 0x0691, 0x06d1, 0x06d1, 0x06d1, 0x06d1, 0x06d1, 0x06d1,
    0x06d1, 0x06d1, 0x0711, 0x0711, 0x0711, 0x0711, 0x0711, 0x0711, 0x0711,
    0x0711, 0x0751, 0x0751, 0x0751, 0x0751, 0x0751, 0x0751, 0x0751, 0x0751,
    0x0791, 0x0791, 0x0791, 0x0791, 0x0791, 0x0791, 0x0791, 0x0791, 0x07d1,
    0x07d1, 0x07d1, 0x07d1, 0x07d1, 0x07d1, 0x07d1, 0x07d1, 0x0811, 0x0811,
    0x0811, 0x0811, 0x0811, 0x0811, 0x0811, 0x0811, 0x0851, 0x0851, 0x0851,
    0x0851, 0x0851, 0x0851, 0x0851, 0x0851, 0x0891, 0x0891, 0x0891, 0x0891,
    0x0891, 0x0891, 0x0891, 0x0891, 0x08d1, 0x08d1, 0x08d1, 0x08d1, 0x08d1,
    0x08d1, 0x08d1, 0x08d1, 0x0911, 0x0911, 0x0911, 0x0911, 0x0911, 0x0911,
    0x0911, 0x0911, 0x0951, 0x0951, 0x0951, 0x0951, 0x0951, 0x0951, 0x0951,
    0x0951, 0x0991, 0x0991, 0x0991, 0x0991, 0x0991, 0x0991, 0x0991, 0x0991,
    0x09d1, 0x09d1, 0x09d1, 0x09d1, 0x09d1, 0x09d1, 0x09d1, 0x09d1, 0x0a11,
    0x0a11, 0x0a11, 0x0a11, 0x0a11, 0x0a11, 0x0a11, 0x0a11, 0x0a51, 0x0a51,
    0x0a51, 0x0a51, 0x0a51, 0x0a51, 0x0a51, 0x0a51, 0x0a91, 0x0a91, 0x0a91,
    0x0a91, 0x0a91, 0x0a91, 0x0a91, 0x0a91, 0x0ad1, 0x0ad1, 0x0ad1, 0x0ad1,
    0x0ad1, 0x0ad1, 0x0ad1, 0x0ad1, 0x0b12, 0x0b12, 0x0b12, 0x0b12, 0x0b52,
    0x0b52, 0x0b52, 0x0b52, 0x0b92, 0x0b92, 0x0b92, 0x0b92, 0x0bd2, 0x0bd2,
    0x0bd2, 0x0bd2, 0x0c12, 0x0c12, 0x0c12, 0x0c12, 0x0c52, 0x0c52, 0x0c52,
    0x0c52, 0x0c92, 0x0c92, 0x0c92, 0x0c92, 0x0cd2, 0x0cd2, 0x0cd2, 0x0cd2,
    0x0d12, 0x0d12, 0x0d12, 0x0d12, 0x0d52, 0x0d52, 0x0d52, 0x0d52, 0x0d92,
    0x0d92, 0x0d92, 0x0d92, 0x0dd2, 0x0dd2, 0x0dd2, 0x0dd2, 0x0e12, 0x0e12,
    0x0e12, 0x0e12, 0x0e52, 0x0e52, 0x0e52, 0x0e52, 0x0e92, 0x0e92, 0x0e92,
    0x0e92, 0x0ed2, 0x0ed2, 0x0ed2, 0x0ed2, 0x0f12, 0x0f12, 0x0f12, 0x0f12,
    0x0f52, 0x0f52, 0x0f52, 0x0f52, 0x0f92, 0x0f92, 0x0f92, 0x0f92, 0x0fd2,
    0x0fd2, 0x0fd2, 0x0fd2, 0x1012, 0x1012, 0x1012, 0x1012, 0x1052, 0x1052,
    0x1052, 0x1052, 0x1092, 0x1092, 0x1092, 0x1092, 0x10d2, 0x10d2, 0x10d2,
    0x10d2, 0x1112, 0x1112, 0x1112, 0x1112, 0x1152, 0x1152, 0x1152, 0x1152,
    0x1192, 0x1192, 0x1192, 0x1192, 0x11d2, 0x11d2, 0x11d2, 0x11d2, 0x1212,
    0x1212, 0x1212, 0x1212, 0x1254, 0x1294, 0x12d4, 0x0034};
static const uint8_t* const g_table4_emit[] = {
    g_table4_0_emit,    g_table4_1_emit,    g_table4_2_emit,
    g_table4_3_emit,    g_table4_4_emit,    g_table4_5_emit,
    g_table4_6_emit,    g_table4_7_emit,    g_table4_8_emit,
    g_table4_9_emit,    g_table4_10_emit,   g_table4_11_emit,
    g_table4_12_emit,   g_table4_13_emit,   g_table4_14_emit,
    g_table4_15_emit,   g_table4_16_emit,   g_table4_17_emit,
    g_table4_18_emit,   g_table4_19_emit,   g_table4_20_emit,
    g_table4_21_emit,   g_table4_22_emit,   g_table4_23_emit,
    g_table4_24_emit,   g_table4_25_emit,   g_table4_26_emit,
    g_table4_27_emit,   g_table4_28_emit,   g_table4_29_emit,
    g_table4_30_emit,   g_table4_31_emit,   g_table4_32_emit,
    g_table4_33_emit,   g_table4_34_emit,   g_table4_35_emit,
    g_table4_36_emit,   g_table4_37_emit,   g_table4_38_emit,
    g_table4_39_emit,   g_table4_40_emit,   g_table4_41_emit,
    g_table4_42_emit,   g_table4_43_emit,   g_table4_44_emit,
    g_table4_45_emit,   g_table4_46_emit,   g_table4_47_emit,
    g_table4_48_emit,   g_table4_49_emit,   g_table4_50_emit,
    g_table4_51_emit,   g_table4_52_emit,   g_table4_53_emit,
    g_table4_54_emit,   g_table4_55_emit,   g_table4_56_emit,
    g_table4_57_emit,   g_table4_58_emit,   g_table4_59_emit,
    g_table4_60_emit,   g_table4_61_emit,   g_table4_62_emit,
    g_table4_63_emit,   g_table4_64_emit,   g_table4_65_emit,
    g_table4_66_emit,   g_table4_67_emit,   g_table4_68_emit,
    g_table4_69_emit,   g_table4_70_emit,   g_table4_71_emit,
    g_table4_72_emit,   g_table4_73_emit,   g_table4_74_emit,
    g_table4_75_emit,   g_table4_76_emit,   g_table4_77_emit,
    g_table4_78_emit,   g_table4_79_emit,   g_table4_80_emit,
    g_table4_81_emit,   g_table4_82_emit,   g_table4_83_emit,
    g_table4_84_emit,   g_table4_85_emit,   g_table4_86_emit,
    g_table4_87_emit,   g_table4_88_emit,   g_table4_89_emit,
    g_table4_90_emit,   g_table4_91_emit,   g_table4_92_emit,
    g_table4_93_emit,   g_table4_94_emit,   g_table4_95_emit,
    g_table4_96_emit,   g_table4_97_emit,   g_table4_98_emit,
    g_table4_99_emit,   g_table4_100_emit,  g_table4_101_emit,
    g_table4_102_emit,  g_table4_103_emit,  g_table4_104_emit,
    g_table4_105_emit,  g_table4_106_emit,  g_table4_107_emit,
    g_table4_108_emit,  g_table4_109_emit,  g_table4_110_emit,
    g_table4_111_emit,  g_table4_112_emit,  g_table4_113_emit,
    g_table4_114_emit,  g_table4_115_emit,  g_table4_116_emit,
    g_table4_117_emit,  g_table4_118_emit,  g_table4_119_emit,
    g_table4_120_emit,  g_table4_121_emit,  g_table4_122_emit,
    g_table4_123_emit,  g_table4_124_emit,  g_table4_125_emit,
    g_table4_126_emit,  g_table4_127_emit,  g_table4_128_emit,
    g_table4_129_emit,  g_table4_130_emit,  g_table4_131_emit,
    g_table4_132_emit,  g_table4_133_emit,  g_table4_134_emit,
    g_table4_135_emit,  g_table4_136_emit,  g_table4_137_emit,
    g_table4_138_emit,  g_table4_139_emit,  g_table4_140_emit,
    g_table4_141_emit,  g_table4_142_emit,  g_table4_143_emit,
    g_table4_144_emit,  g_table4_145_emit,  g_table4_146_emit,
    g_table4_147_emit,  g_table4_148_emit,  g_table4_149_emit,
    g_table4_150_emit,  g_table4_151_emit,  g_table4_152_emit,
    g_table4_153_emit,  g_table4_154_emit,  g_table4_155_emit,
    g_table4_156_emit,  g_table4_157_emit,  g_table4_158_emit,
    g_table4_159_emit,  g_table4_160_emit,  g_table4_161_emit,
    g_table4_162_emit,  g_table4_163_emit,  g_table4_164_emit,
    g_table4_165_emit,  g_table4_166_emit,  g_table4_167_emit,
    g_table4_168_emit,  g_table4_169_emit,  g_table4_170_emit,
    g_table4_171_emit,  g_table4_172_emit,  g_table4_173_emit,
    g_table4_174_emit,  g_table4_175_emit,  g_table4_176_emit,
    g_table4_177_emit,  g_table4_178_emit,  g_table4_179_emit,
    g_table4_180_emit,  g_table4_181_emit,  g_table4_182_emit,
    g_table4_183_emit,  g_table4_184_emit,  g_table4_185_emit,
    g_table4_186_emit,  g_table4_187_emit,  g_table4_188_emit,
    g_table4_189_emit,  g_table4_190_emit,  g_table4_191_emit,
    g_table4_192_emit,  g_table4_193_emit,  g_table4_194_emit,
    g_table4_195_emit,  g_table4_196_emit,  g_table4_197_emit,
    g_table4_198_emit,  g_table4_199_emit,  g_table4_200_emit,
    g_table4_201_emit,  g_table4_202_emit,  g_table4_203_emit,
    g_table4_204_emit,  g_table4_205_emit,  g_table4_206_emit,
    g_table4_207_emit,  g_table4_208_emit,  g_table4_209_emit,
    g_table4_210_emit,  g_table4_211_emit,  g_table4_212_emit,
    g_table4_213_emit,  g_table4_214_emit,  g_table4_215_emit,
    g_table4_216_emit,  g_table4_217_emit,  g_table4_218_emit,
    g_table4_219_emit,  g_table4_220_emit,  g_table4_221_emit,
    g_table4_222_emit,  g_table4_223_emit,  g_table4_224_emit,
    g_table4_225_emit,  g_table4_226_emit,  g_table4_227_emit,
    g_table4_228_emit,  g_table4_229_emit,  g_table4_230_emit,
    g_table4_231_emit,  g_table4_232_emit,  g_table4_233_emit,
    g_table4_234_emit,  g_table4_235_emit,  g_table4_236_emit,
    g_table4_237_emit,  g_table4_238_emit,  g_table4_239_emit,
    g_table4_240_emit,  g_table4_241_emit,  g_table4_242_emit,
    g_table4_243_emit,  g_table4_244_emit,  g_table4_245_emit,
    g_table4_246_emit,  g_table4_247_emit,  g_table4_248_emit,
    g_table4_249_emit,  g_table4_250_emit,  g_table4_251_emit,
    g_table4_252_emit,  g_table4_253_emit,  g_table4_254_emit,
    g_table4_255_emit,  g_table4_256_emit,  g_table4_257_emit,
    g_table4_258_emit,  g_table4_259_emit,  g_table4_260_emit,
    g_table4_261_emit,  g_table4_262_emit,  g_table4_263_emit,
    g_table4_264_emit,  g_table4_265_emit,  g_table4_266_emit,
    g_table4_267_emit,  g_table4_268_emit,  g_table4_269_emit,
    g_table4_270_emit,  g_table4_271_emit,  g_table4_272_emit,
    g_table4_273_emit,  g_table4_274_emit,  g_table4_275_emit,
    g_table4_276_emit,  g_table4_277_emit,  g_table4_278_emit,
    g_table4_279_emit,  g_table4_280_emit,  g_table4_281_emit,
    g_table4_282_emit,  g_table4_283_emit,  g_table4_284_emit,
    g_table4_285_emit,  g_table4_286_emit,  g_table4_287_emit,
    g_table4_288_emit,  g_table4_289_emit,  g_table4_290_emit,
    g_table4_291_emit,  g_table4_292_emit,  g_table4_293_emit,
    g_table4_294_emit,  g_table4_295_emit,  g_table4_296_emit,
    g_table4_297_emit,  g_table4_298_emit,  g_table4_299_emit,
    g_table4_300_emit,  g_table4_301_emit,  g_table4_302_emit,
    g_table4_303_emit,  g_table4_304_emit,  g_table4_305_emit,
    g_table4_306_emit,  g_table4_307_emit,  g_table4_308_emit,
    g_table4_309_emit,  g_table4_310_emit,  g_table4_311_emit,
    g_table4_312_emit,  g_table4_313_emit,  g_table4_314_emit,
    g_table4_315_emit,  g_table4_316_emit,  g_table4_317_emit,
    g_table4_318_emit,  g_table4_319_emit,  g_table4_320_emit,
    g_table4_321_emit,  g_table4_322_emit,  g_table4_323_emit,
    g_table4_324_emit,  g_table4_325_emit,  g_table4_326_emit,
    g_table4_327_emit,  g_table4_328_emit,  g_table4_329_emit,
    g_table4_330_emit,  g_table4_331_emit,  g_table4_332_emit,
    g_table4_333_emit,  g_table4_334_emit,  g_table4_335_emit,
    g_table4_336_emit,  g_table4_337_emit,  g_table4_338_emit,
    g_table4_339_emit,  g_table4_340_emit,  g_table4_341_emit,
    g_table4_342_emit,  g_table4_343_emit,  g_table4_344_emit,
    g_table4_345_emit,  g_table4_346_emit,  g_table4_347_emit,
    g_table4_348_emit,  g_table4_349_emit,  g_table4_350_emit,
    g_table4_351_emit,  g_table4_352_emit,  g_table4_353_emit,
    g_table4_354_emit,  g_table4_355_emit,  g_table4_356_emit,
    g_table4_357_emit,  g_table4_358_emit,  g_table4_359_emit,
    g_table4_360_emit,  g_table4_361_emit,  g_table4_362_emit,
    g_table4_363_emit,  g_table4_364_emit,  g_table4_365_emit,
    g_table4_366_emit,  g_table4_367_emit,  g_table4_368_emit,
    g_table4_369_emit,  g_table4_370_emit,  g_table4_371_emit,
    g_table4_372_emit,  g_table4_373_emit,  g_table4_374_emit,
    g_table4_375_emit,  g_table4_376_emit,  g_table4_377_emit,
    g_table4_378_emit,  g_table4_379_emit,  g_table4_380_emit,
    g_table4_381_emit,  g_table4_382_emit,  g_table4_383_emit,
    g_table4_384_emit,  g_table4_385_emit,  g_table4_386_emit,
    g_table4_387_emit,  g_table4_388_emit,  g_table4_389_emit,
    g_table4_390_emit,  g_table4_391_emit,  g_table4_392_emit,
    g_table4_393_emit,  g_table4_394_emit,  g_table4_395_emit,
    g_table4_396_emit,  g_table4_397_emit,  g_table4_398_emit,
    g_table4_399_emit,  g_table4_400_emit,  g_table4_401_emit,
    g_table4_402_emit,  g_table4_403_emit,  g_table4_404_emit,
    g_table4_405_emit,  g_table4_406_emit,  g_table4_407_emit,
    g_table4_408_emit,  g_table4_409_emit,  g_table4_410_emit,
    g_table4_411_emit,  g_table4_412_emit,  g_table4_413_emit,
    g_table4_414_emit,  g_table4_415_emit,  g_table4_416_emit,
    g_table4_417_emit,  g_table4_418_emit,  g_table4_419_emit,
    g_table4_420_emit,  g_table4_421_emit,  g_table4_422_emit,
    g_table4_423_emit,  g_table4_424_emit,  g_table4_425_emit,
    g_table4_426_emit,  g_table4_427_emit,  g_table4_428_emit,
    g_table4_429_emit,  g_table4_430_emit,  g_table4_431_emit,
    g_table4_432_emit,  g_table4_433_emit,  g_table4_434_emit,
    g_table4_435_emit,  g_table4_436_emit,  g_table4_437_emit,
    g_table4_438_emit,  g_table4_439_emit,  g_table4_440_emit,
    g_table4_441_emit,  g_table4_442_emit,  g_table4_443_emit,
    g_table4_444_emit,  g_table4_445_emit,  g_table4_446_emit,
    g_table4_447_emit,  g_table4_448_emit,  g_table4_449_emit,
    g_table4_450_emit,  g_table4_451_emit,  g_table4_452_emit,
    g_table4_453_emit,  g_table4_454_emit,  g_table4_455_emit,
    g_table4_456_emit,  g_table4_457_emit,  g_table4_458_emit,
    g_table4_459_emit,  g_table4_460_emit,  g_table4_461_emit,
    g_table4_462_emit,  g_table4_463_emit,  g_table4_464_emit,
    g_table4_465_emit,  g_table4_466_emit,  g_table4_467_emit,
    g_table4_468_emit,  g_table4_469_emit,  g_table4_470_emit,
    g_table4_471_emit,  g_table4_472_emit,  g_table4_473_emit,
    g_table4_474_emit,  g_table4_475_emit,  g_table4_476_emit,
    g_table4_477_emit,  g_table4_478_emit,  g_table4_479_emit,
    g_table4_480_emit,  g_table4_481_emit,  g_table4_482_emit,
    g_table4_483_emit,  g_table4_484_emit,  g_table4_485_emit,
    g_table4_486_emit,  g_table4_487_emit,  g_table4_488_emit,
    g_table4_489_emit,  g_table4_490_emit,  g_table4_491_emit,
    g_table4_492_emit,  g_table4_493_emit,  g_table4_494_emit,
    g_table4_495_emit,  g_table4_496_emit,  g_table4_497_emit,
    g_table4_498_emit,  g_table4_499_emit,  g_table4_500_emit,
    g_table4_501_emit,  g_table4_502_emit,  g_table4_503_emit,
    g_table4_504_emit,  g_table4_505_emit,  g_table4_506_emit,
    g_table4_507_emit,  g_table4_508_emit,  g_table4_509_emit,
    g_table4_510_emit,  g_table4_511_emit,  g_table4_512_emit,
    g_table4_513_emit,  g_table4_514_emit,  g_table4_515_emit,
    g_table4_516_emit,  g_table4_517_emit,  g_table4_518_emit,
    g_table4_519_emit,  g_table4_520_emit,  g_table4_521_emit,
    g_table4_522_emit,  g_table4_523_emit,  g_table4_524_emit,
    g_table4_525_emit,  g_table4_526_emit,  g_table4_527_emit,
    g_table4_528_emit,  g_table4_529_emit,  g_table4_530_emit,
    g_table4_531_emit,  g_table4_532_emit,  g_table4_533_emit,
    g_table4_534_emit,  g_table4_535_emit,  g_table4_536_emit,
    g_table4_537_emit,  g_table4_538_emit,  g_table4_539_emit,
    g_table4_540_emit,  g_table4_541_emit,  g_table4_542_emit,
    g_table4_543_emit,  g_table4_544_emit,  g_table4_545_emit,
    g_table4_546_emit,  g_table4_547_emit,  g_table4_548_emit,
    g_table4_549_emit,  g_table4_550_emit,  g_table4_551_emit,
    g_table4_552_emit,  g_table4_553_emit,  g_table4_554_emit,
    g_table4_555_emit,  g_table4_556_emit,  g_table4_557_emit,
    g_table4_558_emit,  g_table4_559_emit,  g_table4_560_emit,
    g_table4_561_emit,  g_table4_562_emit,  g_table4_563_emit,
    g_table4_564_emit,  g_table4_565_emit,  g_table4_566_emit,
    g_table4_567_emit,  g_table4_568_emit,  g_table4_569_emit,
    g_table4_570_emit,  g_table4_571_emit,  g_table4_572_emit,
    g_table4_573_emit,  g_table4_574_emit,  g_table4_575_emit,
    g_table4_576_emit,  g_table4_577_emit,  g_table4_578_emit,
    g_table4_579_emit,  g_table4_580_emit,  g_table4_581_emit,
    g_table4_582_emit,  g_table4_583_emit,  g_table4_584_emit,
    g_table4_585_emit,  g_table4_586_emit,  g_table4_587_emit,
    g_table4_588_emit,  g_table4_589_emit,  g_table4_590_emit,
    g_table4_591_emit,  g_table4_592_emit,  g_table4_593_emit,
    g_table4_594_emit,  g_table4_595_emit,  g_table4_596_emit,
    g_table4_597_emit,  g_table4_598_emit,  g_table4_599_emit,
    g_table4_600_emit,  g_table4_601_emit,  g_table4_602_emit,
    g_table4_603_emit,  g_table4_604_emit,  g_table4_605_emit,
    g_table4_606_emit,  g_table4_607_emit,  g_table4_608_emit,
    g_table4_609_emit,  g_table4_610_emit,  g_table4_611_emit,
    g_table4_612_emit,  g_table4_613_emit,  g_table4_614_emit,
    g_table4_615_emit,  g_table4_616_emit,  g_table4_617_emit,
    g_table4_618_emit,  g_table4_619_emit,  g_table4_620_emit,
    g_table4_621_emit,  g_table4_622_emit,  g_table4_623_emit,
    g_table4_624_emit,  g_table4_625_emit,  g_table4_626_emit,
    g_table4_627_emit,  g_table4_628_emit,  g_table4_629_emit,
    g_table4_630_emit,  g_table4_631_emit,  g_table4_632_emit,
    g_table4_633_emit,  g_table4_634_emit,  g_table4_635_emit,
    g_table4_636_emit,  g_table4_637_emit,  g_table4_638_emit,
    g_table4_639_emit,  g_table4_640_emit,  g_table4_641_emit,
    g_table4_642_emit,  g_table4_643_emit,  g_table4_644_emit,
    g_table4_645_emit,  g_table4_646_emit,  g_table4_647_emit,
    g_table4_648_emit,  g_table4_649_emit,  g_table4_650_emit,
    g_table4_651_emit,  g_table4_652_emit,  g_table4_653_emit,
    g_table4_654_emit,  g_table4_655_emit,  g_table4_656_emit,
    g_table4_657_emit,  g_table4_658_emit,  g_table4_659_emit,
    g_table4_660_emit,  g_table4_661_emit,  g_table4_662_emit,
    g_table4_663_emit,  g_table4_664_emit,  g_table4_665_emit,
    g_table4_666_emit,  g_table4_667_emit,  g_table4_668_emit,
    g_table4_669_emit,  g_table4_670_emit,  g_table4_671_emit,
    g_table4_672_emit,  g_table4_673_emit,  g_table4_674_emit,
    g_table4_675_emit,  g_table4_676_emit,  g_table4_677_emit,
    g_table4_678_emit,  g_table4_679_emit,  g_table4_680_emit,
    g_table4_681_emit,  g_table4_682_emit,  g_table4_683_emit,
    g_table4_684_emit,  g_table4_685_emit,  g_table4_686_emit,
    g_table4_687_emit,  g_table4_688_emit,  g_table4_689_emit,
    g_table4_690_emit,  g_table4_691_emit,  g_table4_692_emit,
    g_table4_693_emit,  g_table4_694_emit,  g_table4_695_emit,
    g_table4_696_emit,  g_table4_697_emit,  g_table4_698_emit,
    g_table4_699_emit,  g_table4_700_emit,  g_table4_701_emit,
    g_table4_702_emit,  g_table4_703_emit,  g_table4_704_emit,
    g_table4_705_emit,  g_table4_706_emit,  g_table4_707_emit,
    g_table4_708_emit,  g_table4_709_emit,  g_table4_710_emit,
    g_table4_711_emit,  g_table4_712_emit,  g_table4_713_emit,
    g_table4_714_emit,  g_table4_715_emit,  g_table4_716_emit,
    g_table4_717_emit,  g_table4_718_emit,  g_table4_719_emit,
    g_table4_720_emit,  g_table4_721_emit,  g_table4_722_emit,
    g_table4_723_emit,  g_table4_724_emit,  g_table4_725_emit,
    g_table4_726_emit,  g_table4_727_emit,  g_table4_728_emit,
    g_table4_729_emit,  g_table4_730_emit,  g_table4_731_emit,
    g_table4_732_emit,  g_table4_733_emit,  g_table4_734_emit,
    g_table4_735_emit,  g_table4_736_emit,  g_table4_737_emit,
    g_table4_738_emit,  g_table4_739_emit,  g_table4_740_emit,
    g_table4_741_emit,  g_table4_742_emit,  g_table4_743_emit,
    g_table4_744_emit,  g_table4_745_emit,  g_table4_746_emit,
    g_table4_747_emit,  g_table4_748_emit,  g_table4_749_emit,
    g_table4_750_emit,  g_table4_751_emit,  g_table4_752_emit,
    g_table4_753_emit,  g_table4_754_emit,  g_table4_755_emit,
    g_table4_756_emit,  g_table4_757_emit,  g_table4_758_emit,
    g_table4_759_emit,  g_table4_760_emit,  g_table4_761_emit,
    g_table4_762_emit,  g_table4_763_emit,  g_table4_764_emit,
    g_table4_765_emit,  g_table4_766_emit,  g_table4_767_emit,
    g_table4_768_emit,  g_table4_769_emit,  g_table4_770_emit,
    g_table4_771_emit,  g_table4_772_emit,  g_table4_773_emit,
    g_table4_774_emit,  g_table4_775_emit,  g_table4_776_emit,
    g_table4_777_emit,  g_table4_778_emit,  g_table4_779_emit,
    g_table4_780_emit,  g_table4_781_emit,  g_table4_782_emit,
    g_table4_783_emit,  g_table4_784_emit,  g_table4_785_emit,
    g_table4_786_emit,  g_table4_787_emit,  g_table4_788_emit,
    g_table4_789_emit,  g_table4_790_emit,  g_table4_791_emit,
    g_table4_792_emit,  g_table4_793_emit,  g_table4_794_emit,
    g_table4_795_emit,  g_table4_796_emit,  g_table4_797_emit,
    g_table4_798_emit,  g_table4_799_emit,  g_table4_800_emit,
    g_table4_801_emit,  g_table4_802_emit,  g_table4_803_emit,
    g_table4_804_emit,  g_table4_805_emit,  g_table4_806_emit,
    g_table4_807_emit,  g_table4_808_emit,  g_table4_809_emit,
    g_table4_810_emit,  g_table4_811_emit,  g_table4_812_emit,
    g_table4_813_emit,  g_table4_814_emit,  g_table4_815_emit,
    g_table4_816_emit,  g_table4_817_emit,  g_table4_818_emit,
    g_table4_819_emit,  g_table4_820_emit,  g_table4_821_emit,
    g_table4_822_emit,  g_table4_823_emit,  g_table4_824_emit,
    g_table4_825_emit,  g_table4_826_emit,  g_table4_827_emit,
    g_table4_828_emit,  g_table4_829_emit,  g_table4_830_emit,
    g_table4_831_emit,  g_table4_832_emit,  g_table4_833_emit,
    g_table4_834_emit,  g_table4_835_emit,  g_table4_836_emit,
    g_table4_837_emit,  g_table4_838_emit,  g_table4_839_emit,
    g_table4_840_emit,  g_table4_841_emit,  g_table4_842_emit,
    g_table4_843_emit,  g_table4_844_emit,  g_table4_845_emit,
    g_table4_846_emit,  g_table4_847_emit,  g_table4_848_emit,
    g_table4_849_emit,  g_table4_850_emit,  g_table4_851_emit,
    g_table4_852_emit,  g_table4_853_emit,  g_table4_854_emit,
    g_table4_855_emit,  g_table4_856_emit,  g_table4_857_emit,
    g_table4_858_emit,  g_table4_859_emit,  g_table4_860_emit,
    g_table4_861_emit,  g_table4_862_emit,  g_table4_863_emit,
    g_table4_864_emit,  g_table4_865_emit,  g_table4_866_emit,
    g_table4_867_emit,  g_table4_868_emit,  g_table4_869_emit,
    g_table4_870_emit,  g_table4_871_emit,  g_table4_872_emit,
    g_table4_873_emit,  g_table4_874_emit,  g_table4_875_emit,
    g_table4_876_emit,  g_table4_877_emit,  g_table4_878_emit,
    g_table4_879_emit,  g_table4_880_emit,  g_table4_881_emit,
    g_table4_882_emit,  g_table4_883_emit,  g_table4_884_emit,
    g_table4_885_emit,  g_table4_886_emit,  g_table4_887_emit,
    g_table4_888_emit,  g_table4_889_emit,  g_table4_890_emit,
    g_table4_891_emit,  g_table4_892_emit,  g_table4_893_emit,
    g_table4_894_emit,  g_table4_895_emit,  g_table4_896_emit,
    g_table4_897_emit,  g_table4_898_emit,  g_table4_899_emit,
    g_table4_900_emit,  g_table4_901_emit,  g_table4_902_emit,
    g_table4_903_emit,  g_table4_904_emit,  g_table4_905_emit,
    g_table4_906_emit,  g_table4_907_emit,  g_table4_908_emit,
    g_table4_909_emit,  g_table4_910_emit,  g_table4_911_emit,
    g_table4_912_emit,  g_table4_913_emit,  g_table4_914_emit,
    g_table4_915_emit,  g_table4_916_emit,  g_table4_917_emit,
    g_table4_918_emit,  g_table4_919_emit,  g_table4_920_emit,
    g_table4_921_emit,  g_table4_922_emit,  g_table4_923_emit,
    g_table4_924_emit,  g_table4_925_emit,  g_table4_926_emit,
    g_table4_927_emit,  g_table4_928_emit,  g_table4_929_emit,
    g_table4_930_emit,  g_table4_931_emit,  g_table4_932_emit,
    g_table4_933_emit,  g_table4_934_emit,  g_table4_935_emit,
    g_table4_936_emit,  g_table4_937_emit,  g_table4_938_emit,
    g_table4_939_emit,  g_table4_940_emit,  g_table4_941_emit,
    g_table4_942_emit,  g_table4_943_emit,  g_table4_944_emit,
    g_table4_945_emit,  g_table4_946_emit,  g_table4_947_emit,
    g_table4_948_emit,  g_table4_949_emit,  g_table4_950_emit,
    g_table4_951_emit,  g_table4_952_emit,  g_table4_953_emit,
    g_table4_954_emit,  g_table4_955_emit,  g_table4_956_emit,
    g_table4_957_emit,  g_table4_958_emit,  g_table4_959_emit,
    g_table4_960_emit,  g_table4_961_emit,  g_table4_962_emit,
    g_table4_963_emit,  g_table4_964_emit,  g_table4_965_emit,
    g_table4_966_emit,  g_table4_967_emit,  g_table4_968_emit,
    g_table4_969_emit,  g_table4_970_emit,  g_table4_971_emit,
    g_table4_972_emit,  g_table4_973_emit,  g_table4_974_emit,
    g_table4_975_emit,  g_table4_976_emit,  g_table4_977_emit,
    g_table4_978_emit,  g_table4_979_emit,  g_table4_980_emit,
    g_table4_981_emit,  g_table4_982_emit,  g_table4_983_emit,
    g_table4_984_emit,  g_table4_985_emit,  g_table4_986_emit,
    g_table4_987_emit,  g_table4_988_emit,  g_table4_989_emit,
    g_table4_990_emit,  g_table4_991_emit,  g_table4_992_emit,
    g_table4_993_emit,  g_table4_994_emit,  g_table4_995_emit,
    g_table4_996_emit,  g_table4_997_emit,  g_table4_998_emit,
    g_table4_999_emit,  g_table4_1000_emit, g_table4_1001_emit,
    g_table4_1002_emit, g_table4_1003_emit, g_table4_1004_emit,
    g_table4_1005_emit, g_table4_1006_emit, g_table4_1007_emit,
    g_table4_1008_emit, g_table4_1009_emit, g_table4_1010_emit,
    g_table4_1011_emit, g_table4_1012_emit, g_table4_1013_emit,
    g_table4_1014_emit, g_table4_1015_emit, g_table4_1016_emit,
    g_table4_1017_emit, g_table4_1018_emit, g_table4_1019_emit,
    g_table4_1020_emit, g_table4_1021_emit, g_table4_1022_emit,
    g_table4_1023_emit,
};
static const uint16_t* const g_table4_ops[] = {
    g_table4_0_ops,    g_table4_1_ops,    g_table4_2_ops,    g_table4_3_ops,
    g_table4_4_ops,    g_table4_5_ops,    g_table4_6_ops,    g_table4_7_ops,
    g_table4_8_ops,    g_table4_9_ops,    g_table4_10_ops,   g_table4_11_ops,
    g_table4_12_ops,   g_table4_13_ops,   g_table4_14_ops,   g_table4_15_ops,
    g_table4_16_ops,   g_table4_17_ops,   g_table4_18_ops,   g_table4_19_ops,
    g_table4_20_ops,   g_table4_21_ops,   g_table4_22_ops,   g_table4_23_ops,
    g_table4_24_ops,   g_table4_25_ops,   g_table4_26_ops,   g_table4_27_ops,
    g_table4_28_ops,   g_table4_29_ops,   g_table4_30_ops,   g_table4_31_ops,
    g_table4_32_ops,   g_table4_33_ops,   g_table4_34_ops,   g_table4_35_ops,
    g_table4_36_ops,   g_table4_37_ops,   g_table4_38_ops,   g_table4_39_ops,
    g_table4_40_ops,   g_table4_41_ops,   g_table4_42_ops,   g_table4_43_ops,
    g_table4_44_ops,   g_table4_45_ops,   g_table4_46_ops,   g_table4_47_ops,
    g_table4_48_ops,   g_table4_49_ops,   g_table4_50_ops,   g_table4_51_ops,
    g_table4_52_ops,   g_table4_53_ops,   g_table4_54_ops,   g_table4_55_ops,
    g_table4_56_ops,   g_table4_57_ops,   g_table4_58_ops,   g_table4_59_ops,
    g_table4_60_ops,   g_table4_61_ops,   g_table4_62_ops,   g_table4_63_ops,
    g_table4_64_ops,   g_table4_65_ops,   g_table4_66_ops,   g_table4_67_ops,
    g_table4_68_ops,   g_table4_69_ops,   g_table4_70_ops,   g_table4_71_ops,
    g_table4_72_ops,   g_table4_73_ops,   g_table4_74_ops,   g_table4_75_ops,
    g_table4_76_ops,   g_table4_77_ops,   g_table4_78_ops,   g_table4_79_ops,
    g_table4_80_ops,   g_table4_81_ops,   g_table4_82_ops,   g_table4_83_ops,
    g_table4_84_ops,   g_table4_85_ops,   g_table4_86_ops,   g_table4_87_ops,
    g_table4_88_ops,   g_table4_89_ops,   g_table4_90_ops,   g_table4_91_ops,
    g_table4_92_ops,   g_table4_93_ops,   g_table4_94_ops,   g_table4_95_ops,
    g_table4_96_ops,   g_table4_97_ops,   g_table4_98_ops,   g_table4_99_ops,
    g_table4_100_ops,  g_table4_101_ops,  g_table4_102_ops,  g_table4_103_ops,
    g_table4_104_ops,  g_table4_105_ops,  g_table4_106_ops,  g_table4_107_ops,
    g_table4_108_ops,  g_table4_109_ops,  g_table4_110_ops,  g_table4_111_ops,
    g_table4_112_ops,  g_table4_113_ops,  g_table4_114_ops,  g_table4_115_ops,
    g_table4_116_ops,  g_table4_117_ops,  g_table4_118_ops,  g_table4_119_ops,
    g_table4_120_ops,  g_table4_121_ops,  g_table4_122_ops,  g_table4_123_ops,
    g_table4_124_ops,  g_table4_125_ops,  g_table4_126_ops,  g_table4_127_ops,
    g_table4_128_ops,  g_table4_129_ops,  g_table4_130_ops,  g_table4_131_ops,
    g_table4_132_ops,  g_table4_133_ops,  g_table4_134_ops,  g_table4_135_ops,
    g_table4_136_ops,  g_table4_137_ops,  g_table4_138_ops,  g_table4_139_ops,
    g_table4_140_ops,  g_table4_141_ops,  g_table4_142_ops,  g_table4_143_ops,
    g_table4_144_ops,  g_table4_145_ops,  g_table4_146_ops,  g_table4_147_ops,
    g_table4_148_ops,  g_table4_149_ops,  g_table4_150_ops,  g_table4_151_ops,
    g_table4_152_ops,  g_table4_153_ops,  g_table4_154_ops,  g_table4_155_ops,
    g_table4_156_ops,  g_table4_157_ops,  g_table4_158_ops,  g_table4_159_ops,
    g_table4_160_ops,  g_table4_161_ops,  g_table4_162_ops,  g_table4_163_ops,
    g_table4_164_ops,  g_table4_165_ops,  g_table4_166_ops,  g_table4_167_ops,
    g_table4_168_ops,  g_table4_169_ops,  g_table4_170_ops,  g_table4_171_ops,
    g_table4_172_ops,  g_table4_173_ops,  g_table4_174_ops,  g_table4_175_ops,
    g_table4_176_ops,  g_table4_177_ops,  g_table4_178_ops,  g_table4_179_ops,
    g_table4_180_ops,  g_table4_181_ops,  g_table4_182_ops,  g_table4_183_ops,
    g_table4_184_ops,  g_table4_185_ops,  g_table4_186_ops,  g_table4_187_ops,
    g_table4_188_ops,  g_table4_189_ops,  g_table4_190_ops,  g_table4_191_ops,
    g_table4_192_ops,  g_table4_193_ops,  g_table4_194_ops,  g_table4_195_ops,
    g_table4_196_ops,  g_table4_197_ops,  g_table4_198_ops,  g_table4_199_ops,
    g_table4_200_ops,  g_table4_201_ops,  g_table4_202_ops,  g_table4_203_ops,
    g_table4_204_ops,  g_table4_205_ops,  g_table4_206_ops,  g_table4_207_ops,
    g_table4_208_ops,  g_table4_209_ops,  g_table4_210_ops,  g_table4_211_ops,
    g_table4_212_ops,  g_table4_213_ops,  g_table4_214_ops,  g_table4_215_ops,
    g_table4_216_ops,  g_table4_217_ops,  g_table4_218_ops,  g_table4_219_ops,
    g_table4_220_ops,  g_table4_221_ops,  g_table4_222_ops,  g_table4_223_ops,
    g_table4_224_ops,  g_table4_225_ops,  g_table4_226_ops,  g_table4_227_ops,
    g_table4_228_ops,  g_table4_229_ops,  g_table4_230_ops,  g_table4_231_ops,
    g_table4_232_ops,  g_table4_233_ops,  g_table4_234_ops,  g_table4_235_ops,
    g_table4_236_ops,  g_table4_237_ops,  g_table4_238_ops,  g_table4_239_ops,
    g_table4_240_ops,  g_table4_241_ops,  g_table4_242_ops,  g_table4_243_ops,
    g_table4_244_ops,  g_table4_245_ops,  g_table4_246_ops,  g_table4_247_ops,
    g_table4_248_ops,  g_table4_249_ops,  g_table4_250_ops,  g_table4_251_ops,
    g_table4_252_ops,  g_table4_253_ops,  g_table4_254_ops,  g_table4_255_ops,
    g_table4_256_ops,  g_table4_257_ops,  g_table4_258_ops,  g_table4_259_ops,
    g_table4_260_ops,  g_table4_261_ops,  g_table4_262_ops,  g_table4_263_ops,
    g_table4_264_ops,  g_table4_265_ops,  g_table4_266_ops,  g_table4_267_ops,
    g_table4_268_ops,  g_table4_269_ops,  g_table4_270_ops,  g_table4_271_ops,
    g_table4_272_ops,  g_table4_273_ops,  g_table4_274_ops,  g_table4_275_ops,
    g_table4_276_ops,  g_table4_277_ops,  g_table4_278_ops,  g_table4_279_ops,
    g_table4_280_ops,  g_table4_281_ops,  g_table4_282_ops,  g_table4_283_ops,
    g_table4_284_ops,  g_table4_285_ops,  g_table4_286_ops,  g_table4_287_ops,
    g_table4_288_ops,  g_table4_289_ops,  g_table4_290_ops,  g_table4_291_ops,
    g_table4_292_ops,  g_table4_293_ops,  g_table4_294_ops,  g_table4_295_ops,
    g_table4_296_ops,  g_table4_297_ops,  g_table4_298_ops,  g_table4_299_ops,
    g_table4_300_ops,  g_table4_301_ops,  g_table4_302_ops,  g_table4_303_ops,
    g_table4_304_ops,  g_table4_305_ops,  g_table4_306_ops,  g_table4_307_ops,
    g_table4_308_ops,  g_table4_309_ops,  g_table4_310_ops,  g_table4_311_ops,
    g_table4_312_ops,  g_table4_313_ops,  g_table4_314_ops,  g_table4_315_ops,
    g_table4_316_ops,  g_table4_317_ops,  g_table4_318_ops,  g_table4_319_ops,
    g_table4_320_ops,  g_table4_321_ops,  g_table4_322_ops,  g_table4_323_ops,
    g_table4_324_ops,  g_table4_325_ops,  g_table4_326_ops,  g_table4_327_ops,
    g_table4_328_ops,  g_table4_329_ops,  g_table4_330_ops,  g_table4_331_ops,
    g_table4_332_ops,  g_table4_333_ops,  g_table4_334_ops,  g_table4_335_ops,
    g_table4_336_ops,  g_table4_337_ops,  g_table4_338_ops,  g_table4_339_ops,
    g_table4_340_ops,  g_table4_341_ops,  g_table4_342_ops,  g_table4_343_ops,
    g_table4_344_ops,  g_table4_345_ops,  g_table4_346_ops,  g_table4_347_ops,
    g_table4_348_ops,  g_table4_349_ops,  g_table4_350_ops,  g_table4_351_ops,
    g_table4_352_ops,  g_table4_353_ops,  g_table4_354_ops,  g_table4_355_ops,
    g_table4_356_ops,  g_table4_357_ops,  g_table4_358_ops,  g_table4_359_ops,
    g_table4_360_ops,  g_table4_361_ops,  g_table4_362_ops,  g_table4_363_ops,
    g_table4_364_ops,  g_table4_365_ops,  g_table4_366_ops,  g_table4_367_ops,
    g_table4_368_ops,  g_table4_369_ops,  g_table4_370_ops,  g_table4_371_ops,
    g_table4_372_ops,  g_table4_373_ops,  g_table4_374_ops,  g_table4_375_ops,
    g_table4_376_ops,  g_table4_377_ops,  g_table4_378_ops,  g_table4_379_ops,
    g_table4_380_ops,  g_table4_381_ops,  g_table4_382_ops,  g_table4_383_ops,
    g_table4_384_ops,  g_table4_385_ops,  g_table4_386_ops,  g_table4_387_ops,
    g_table4_388_ops,  g_table4_389_ops,  g_table4_390_ops,  g_table4_391_ops,
    g_table4_392_ops,  g_table4_393_ops,  g_table4_394_ops,  g_table4_395_ops,
    g_table4_396_ops,  g_table4_397_ops,  g_table4_398_ops,  g_table4_399_ops,
    g_table4_400_ops,  g_table4_401_ops,  g_table4_402_ops,  g_table4_403_ops,
    g_table4_404_ops,  g_table4_405_ops,  g_table4_406_ops,  g_table4_407_ops,
    g_table4_408_ops,  g_table4_409_ops,  g_table4_410_ops,  g_table4_411_ops,
    g_table4_412_ops,  g_table4_413_ops,  g_table4_414_ops,  g_table4_415_ops,
    g_table4_416_ops,  g_table4_417_ops,  g_table4_418_ops,  g_table4_419_ops,
    g_table4_420_ops,  g_table4_421_ops,  g_table4_422_ops,  g_table4_423_ops,
    g_table4_424_ops,  g_table4_425_ops,  g_table4_426_ops,  g_table4_427_ops,
    g_table4_428_ops,  g_table4_429_ops,  g_table4_430_ops,  g_table4_431_ops,
    g_table4_432_ops,  g_table4_433_ops,  g_table4_434_ops,  g_table4_435_ops,
    g_table4_436_ops,  g_table4_437_ops,  g_table4_438_ops,  g_table4_439_ops,
    g_table4_440_ops,  g_table4_441_ops,  g_table4_442_ops,  g_table4_443_ops,
    g_table4_444_ops,  g_table4_445_ops,  g_table4_446_ops,  g_table4_447_ops,
    g_table4_448_ops,  g_table4_449_ops,  g_table4_450_ops,  g_table4_451_ops,
    g_table4_452_ops,  g_table4_453_ops,  g_table4_454_ops,  g_table4_455_ops,
    g_table4_456_ops,  g_table4_457_ops,  g_table4_458_ops,  g_table4_459_ops,
    g_table4_460_ops,  g_table4_461_ops,  g_table4_462_ops,  g_table4_463_ops,
    g_table4_464_ops,  g_table4_465_ops,  g_table4_466_ops,  g_table4_467_ops,
    g_table4_468_ops,  g_table4_469_ops,  g_table4_470_ops,  g_table4_471_ops,
    g_table4_472_ops,  g_table4_473_ops,  g_table4_474_ops,  g_table4_475_ops,
    g_table4_476_ops,  g_table4_477_ops,  g_table4_478_ops,  g_table4_479_ops,
    g_table4_480_ops,  g_table4_481_ops,  g_table4_482_ops,  g_table4_483_ops,
    g_table4_484_ops,  g_table4_485_ops,  g_table4_486_ops,  g_table4_487_ops,
    g_table4_488_ops,  g_table4_489_ops,  g_table4_490_ops,  g_table4_491_ops,
    g_table4_492_ops,  g_table4_493_ops,  g_table4_494_ops,  g_table4_495_ops,
    g_table4_496_ops,  g_table4_497_ops,  g_table4_498_ops,  g_table4_499_ops,
    g_table4_500_ops,  g_table4_501_ops,  g_table4_502_ops,  g_table4_503_ops,
    g_table4_504_ops,  g_table4_505_ops,  g_table4_506_ops,  g_table4_507_ops,
    g_table4_508_ops,  g_table4_509_ops,  g_table4_510_ops,  g_table4_511_ops,
    g_table4_512_ops,  g_table4_513_ops,  g_table4_514_ops,  g_table4_515_ops,
    g_table4_516_ops,  g_table4_517_ops,  g_table4_518_ops,  g_table4_519_ops,
    g_table4_520_ops,  g_table4_521_ops,  g_table4_522_ops,  g_table4_523_ops,
    g_table4_524_ops,  g_table4_525_ops,  g_table4_526_ops,  g_table4_527_ops,
    g_table4_528_ops,  g_table4_529_ops,  g_table4_530_ops,  g_table4_531_ops,
    g_table4_532_ops,  g_table4_533_ops,  g_table4_534_ops,  g_table4_535_ops,
    g_table4_536_ops,  g_table4_537_ops,  g_table4_538_ops,  g_table4_539_ops,
    g_table4_540_ops,  g_table4_541_ops,  g_table4_542_ops,  g_table4_543_ops,
    g_table4_544_ops,  g_table4_545_ops,  g_table4_546_ops,  g_table4_547_ops,
    g_table4_548_ops,  g_table4_549_ops,  g_table4_550_ops,  g_table4_551_ops,
    g_table4_552_ops,  g_table4_553_ops,  g_table4_554_ops,  g_table4_555_ops,
    g_table4_556_ops,  g_table4_557_ops,  g_table4_558_ops,  g_table4_559_ops,
    g_table4_560_ops,  g_table4_561_ops,  g_table4_562_ops,  g_table4_563_ops,
    g_table4_564_ops,  g_table4_565_ops,  g_table4_566_ops,  g_table4_567_ops,
    g_table4_568_ops,  g_table4_569_ops,  g_table4_570_ops,  g_table4_571_ops,
    g_table4_572_ops,  g_table4_573_ops,  g_table4_574_ops,  g_table4_575_ops,
    g_table4_576_ops,  g_table4_577_ops,  g_table4_578_ops,  g_table4_579_ops,
    g_table4_580_ops,  g_table4_581_ops,  g_table4_582_ops,  g_table4_583_ops,
    g_table4_584_ops,  g_table4_585_ops,  g_table4_586_ops,  g_table4_587_ops,
    g_table4_588_ops,  g_table4_589_ops,  g_table4_590_ops,  g_table4_591_ops,
    g_table4_592_ops,  g_table4_593_ops,  g_table4_594_ops,  g_table4_595_ops,
    g_table4_596_ops,  g_table4_597_ops,  g_table4_598_ops,  g_table4_599_ops,
    g_table4_600_ops,  g_table4_601_ops,  g_table4_602_ops,  g_table4_603_ops,
    g_table4_604_ops,  g_table4_605_ops,  g_table4_606_ops,  g_table4_607_ops,
    g_table4_608_ops,  g_table4_609_ops,  g_table4_610_ops,  g_table4_611_ops,
    g_table4_612_ops,  g_table4_613_ops,  g_table4_614_ops,  g_table4_615_ops,
    g_table4_616_ops,  g_table4_617_ops,  g_table4_618_ops,  g_table4_619_ops,
    g_table4_620_ops,  g_table4_621_ops,  g_table4_622_ops,  g_table4_623_ops,
    g_table4_624_ops,  g_table4_625_ops,  g_table4_626_ops,  g_table4_627_ops,
    g_table4_628_ops,  g_table4_629_ops,  g_table4_630_ops,  g_table4_631_ops,
    g_table4_632_ops,  g_table4_633_ops,  g_table4_634_ops,  g_table4_635_ops,
    g_table4_636_ops,  g_table4_637_ops,  g_table4_638_ops,  g_table4_639_ops,
    g_table4_640_ops,  g_table4_641_ops,  g_table4_642_ops,  g_table4_643_ops,
    g_table4_644_ops,  g_table4_645_ops,  g_table4_646_ops,  g_table4_647_ops,
    g_table4_648_ops,  g_table4_649_ops,  g_table4_650_ops,  g_table4_651_ops,
    g_table4_652_ops,  g_table4_653_ops,  g_table4_654_ops,  g_table4_655_ops,
    g_table4_656_ops,  g_table4_657_ops,  g_table4_658_ops,  g_table4_659_ops,
    g_table4_660_ops,  g_table4_661_ops,  g_table4_662_ops,  g_table4_663_ops,
    g_table4_664_ops,  g_table4_665_ops,  g_table4_666_ops,  g_table4_667_ops,
    g_table4_668_ops,  g_table4_669_ops,  g_table4_670_ops,  g_table4_671_ops,
    g_table4_672_ops,  g_table4_673_ops,  g_table4_674_ops,  g_table4_675_ops,
    g_table4_676_ops,  g_table4_677_ops,  g_table4_678_ops,  g_table4_679_ops,
    g_table4_680_ops,  g_table4_681_ops,  g_table4_682_ops,  g_table4_683_ops,
    g_table4_684_ops,  g_table4_685_ops,  g_table4_686_ops,  g_table4_687_ops,
    g_table4_688_ops,  g_table4_689_ops,  g_table4_690_ops,  g_table4_691_ops,
    g_table4_692_ops,  g_table4_693_ops,  g_table4_694_ops,  g_table4_695_ops,
    g_table4_696_ops,  g_table4_697_ops,  g_table4_698_ops,  g_table4_699_ops,
    g_table4_700_ops,  g_table4_701_ops,  g_table4_702_ops,  g_table4_703_ops,
    g_table4_704_ops,  g_table4_705_ops,  g_table4_706_ops,  g_table4_707_ops,
    g_table4_708_ops,  g_table4_709_ops,  g_table4_710_ops,  g_table4_711_ops,
    g_table4_712_ops,  g_table4_713_ops,  g_table4_714_ops,  g_table4_715_ops,
    g_table4_716_ops,  g_table4_717_ops,  g_table4_718_ops,  g_table4_719_ops,
    g_table4_720_ops,  g_table4_721_ops,  g_table4_722_ops,  g_table4_723_ops,
    g_table4_724_ops,  g_table4_725_ops,  g_table4_726_ops,  g_table4_727_ops,
    g_table4_728_ops,  g_table4_729_ops,  g_table4_730_ops,  g_table4_731_ops,
    g_table4_732_ops,  g_table4_733_ops,  g_table4_734_ops,  g_table4_735_ops,
    g_table4_736_ops,  g_table4_737_ops,  g_table4_738_ops,  g_table4_739_ops,
    g_table4_740_ops,  g_table4_741_ops,  g_table4_742_ops,  g_table4_743_ops,
    g_table4_744_ops,  g_table4_745_ops,  g_table4_746_ops,  g_table4_747_ops,
    g_table4_748_ops,  g_table4_749_ops,  g_table4_750_ops,  g_table4_751_ops,
    g_table4_752_ops,  g_table4_753_ops,  g_table4_754_ops,  g_table4_755_ops,
    g_table4_756_ops,  g_table4_757_ops,  g_table4_758_ops,  g_table4_759_ops,
    g_table4_760_ops,  g_table4_761_ops,  g_table4_762_ops,  g_table4_763_ops,
    g_table4_764_ops,  g_table4_765_ops,  g_table4_766_ops,  g_table4_767_ops,
    g_table4_768_ops,  g_table4_769_ops,  g_table4_770_ops,  g_table4_771_ops,
    g_table4_772_ops,  g_table4_773_ops,  g_table4_774_ops,  g_table4_775_ops,
    g_table4_776_ops,  g_table4_777_ops,  g_table4_778_ops,  g_table4_779_ops,
    g_table4_780_ops,  g_table4_781_ops,  g_table4_782_ops,  g_table4_783_ops,
    g_table4_784_ops,  g_table4_785_ops,  g_table4_786_ops,  g_table4_787_ops,
    g_table4_788_ops,  g_table4_789_ops,  g_table4_790_ops,  g_table4_791_ops,
    g_table4_792_ops,  g_table4_793_ops,  g_table4_794_ops,  g_table4_795_ops,
    g_table4_796_ops,  g_table4_797_ops,  g_table4_798_ops,  g_table4_799_ops,
    g_table4_800_ops,  g_table4_801_ops,  g_table4_802_ops,  g_table4_803_ops,
    g_table4_804_ops,  g_table4_805_ops,  g_table4_806_ops,  g_table4_807_ops,
    g_table4_808_ops,  g_table4_809_ops,  g_table4_810_ops,  g_table4_811_ops,
    g_table4_812_ops,  g_table4_813_ops,  g_table4_814_ops,  g_table4_815_ops,
    g_table4_816_ops,  g_table4_817_ops,  g_table4_818_ops,  g_table4_819_ops,
    g_table4_820_ops,  g_table4_821_ops,  g_table4_822_ops,  g_table4_823_ops,
    g_table4_824_ops,  g_table4_825_ops,  g_table4_826_ops,  g_table4_827_ops,
    g_table4_828_ops,  g_table4_829_ops,  g_table4_830_ops,  g_table4_831_ops,
    g_table4_832_ops,  g_table4_833_ops,  g_table4_834_ops,  g_table4_835_ops,
    g_table4_836_ops,  g_table4_837_ops,  g_table4_838_ops,  g_table4_839_ops,
    g_table4_840_ops,  g_table4_841_ops,  g_table4_842_ops,  g_table4_843_ops,
    g_table4_844_ops,  g_table4_845_ops,  g_table4_846_ops,  g_table4_847_ops,
    g_table4_848_ops,  g_table4_849_ops,  g_table4_850_ops,  g_table4_851_ops,
    g_table4_852_ops,  g_table4_853_ops,  g_table4_854_ops,  g_table4_855_ops,
    g_table4_856_ops,  g_table4_857_ops,  g_table4_858_ops,  g_table4_859_ops,
    g_table4_860_ops,  g_table4_861_ops,  g_table4_862_ops,  g_table4_863_ops,
    g_table4_864_ops,  g_table4_865_ops,  g_table4_866_ops,  g_table4_867_ops,
    g_table4_868_ops,  g_table4_869_ops,  g_table4_870_ops,  g_table4_871_ops,
    g_table4_872_ops,  g_table4_873_ops,  g_table4_874_ops,  g_table4_875_ops,
    g_table4_876_ops,  g_table4_877_ops,  g_table4_878_ops,  g_table4_879_ops,
    g_table4_880_ops,  g_table4_881_ops,  g_table4_882_ops,  g_table4_883_ops,
    g_table4_884_ops,  g_table4_885_ops,  g_table4_886_ops,  g_table4_887_ops,
    g_table4_888_ops,  g_table4_889_ops,  g_table4_890_ops,  g_table4_891_ops,
    g_table4_892_ops,  g_table4_893_ops,  g_table4_894_ops,  g_table4_895_ops,
    g_table4_896_ops,  g_table4_897_ops,  g_table4_898_ops,  g_table4_899_ops,
    g_table4_900_ops,  g_table4_901_ops,  g_table4_902_ops,  g_table4_903_ops,
    g_table4_904_ops,  g_table4_905_ops,  g_table4_906_ops,  g_table4_907_ops,
    g_table4_908_ops,  g_table4_909_ops,  g_table4_910_ops,  g_table4_911_ops,
    g_table4_912_ops,  g_table4_913_ops,  g_table4_914_ops,  g_table4_915_ops,
    g_table4_916_ops,  g_table4_917_ops,  g_table4_918_ops,  g_table4_919_ops,
    g_table4_920_ops,  g_table4_921_ops,  g_table4_922_ops,  g_table4_923_ops,
    g_table4_924_ops,  g_table4_925_ops,  g_table4_926_ops,  g_table4_927_ops,
    g_table4_928_ops,  g_table4_929_ops,  g_table4_930_ops,  g_table4_931_ops,
    g_table4_932_ops,  g_table4_933_ops,  g_table4_934_ops,  g_table4_935_ops,
    g_table4_936_ops,  g_table4_937_ops,  g_table4_938_ops,  g_table4_939_ops,
    g_table4_940_ops,  g_table4_941_ops,  g_table4_942_ops,  g_table4_943_ops,
    g_table4_944_ops,  g_table4_945_ops,  g_table4_946_ops,  g_table4_947_ops,
    g_table4_948_ops,  g_table4_949_ops,  g_table4_950_ops,  g_table4_951_ops,
    g_table4_952_ops,  g_table4_953_ops,  g_table4_954_ops,  g_table4_955_ops,
    g_table4_956_ops,  g_table4_957_ops,  g_table4_958_ops,  g_table4_959_ops,
    g_table4_960_ops,  g_table4_961_ops,  g_table4_962_ops,  g_table4_963_ops,
    g_table4_964_ops,  g_table4_965_ops,  g_table4_966_ops,  g_table4_967_ops,
    g_table4_968_ops,  g_table4_969_ops,  g_table4_970_ops,  g_table4_971_ops,
    g_table4_972_ops,  g_table4_973_ops,  g_table4_974_ops,  g_table4_975_ops,
    g_table4_976_ops,  g_table4_977_ops,  g_table4_978_ops,  g_table4_979_ops,
    g_table4_980_ops,  g_table4_981_ops,  g_table4_982_ops,  g_table4_983_ops,
    g_table4_984_ops,  g_table4_985_ops,  g_table4_986_ops,  g_table4_987_ops,
    g_table4_988_ops,  g_table4_989_ops,  g_table4_990_ops,  g_table4_991_ops,
    g_table4_992_ops,  g_table4_993_ops,  g_table4_994_ops,  g_table4_995_ops,
    g_table4_996_ops,  g_table4_997_ops,  g_table4_998_ops,  g_table4_999_ops,
    g_table4_1000_ops, g_table4_1001_ops, g_table4_1002_ops, g_table4_1003_ops,
    g_table4_1004_ops, g_table4_1005_ops, g_table4_1006_ops, g_table4_1007_ops,
    g_table4_1008_ops, g_table4_1009_ops, g_table4_1010_ops, g_table4_1011_ops,
    g_table4_1012_ops, g_table4_1013_ops, g_table4_1014_ops, g_table4_1015_ops,
    g_table4_1016_ops, g_table4_1017_ops, g_table4_1018_ops, g_table4_1019_ops,
    g_table4_1020_ops, g_table4_1021_ops, g_table4_1022_ops, g_table4_1023_ops,
};
inline uint64_t GetOp4(size_t i) { return g_table4_ops[i >> 10][i & 0x3ff]; }
inline uint64_t GetEmit4(size_t i, size_t emit) {
  return g_table4_emit[i >> 10][emit];
}
static const uint8_t g_table5_0_emit[3] = {0x7c, 0x23, 0x3e};
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
      const auto op = GetOp2(index);
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
          DecodeStep2();
          break;
        }
        case 1: {
          sink_(GetEmit2(index, emit_ofs + 0));
          break;
        }
        case 0: {
          sink_(GetEmit2(index, emit_ofs + 0));
          sink_(GetEmit2(index, emit_ofs + 1));
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
    if (!RefillTo20()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 20)) & 0xfffff;
    const auto op = GetOp4(index);
    buffer_len_ -= op & 31;
    const auto emit_ofs = op >> 6;
    switch ((op >> 5) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit4(index, emit_ofs + 0));
        break;
      }
    }
  }
  bool RefillTo20() {
    switch (buffer_len_) {
      case 12:
      case 13:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19: {
        return Read1();
      }
      case 10:
      case 11:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9: {
        return Read2();
      }
      case 0:
      case 1:
      case 2:
      case 3: {
        return Read3();
      }
    }
    return true;
  }
  bool Read3() {
    if (begin_ + 3 > end_) return false;
    buffer_ <<= 24;
    buffer_ |= static_cast<uint64_t>(*begin_++) << 16;
    buffer_ |= static_cast<uint64_t>(*begin_++) << 8;
    buffer_ |= static_cast<uint64_t>(*begin_++) << 0;
    buffer_len_ += 24;
    return true;
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
  void Done() {
    if (buffer_len_ < 9) {
      buffer_ = (buffer_ << (9 - buffer_len_)) |
                ((uint64_t(1) << (9 - buffer_len_)) - 1);
      buffer_len_ = 9;
    }
    const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
    const auto op = GetOp1(index);
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
