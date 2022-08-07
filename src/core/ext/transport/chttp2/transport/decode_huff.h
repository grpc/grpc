#include <stdlib.h>

#include <cstddef>
#include <cstdint>
static const uint8_t g_table1_0_emit[2] = {0x30, 0x31};
static const uint16_t g_table1_0_inner[2] = {0x0005, 0x0045};
static const uint8_t g_table1_0_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t g_table1_1_emit[2] = {0x32, 0x61};
#define g_table1_1_inner g_table1_0_inner
#define g_table1_1_outer g_table1_0_outer
static const uint8_t g_table1_2_emit[2] = {0x63, 0x65};
#define g_table1_2_inner g_table1_0_inner
#define g_table1_2_outer g_table1_0_outer
static const uint8_t g_table1_3_emit[2] = {0x69, 0x6f};
#define g_table1_3_inner g_table1_0_inner
#define g_table1_3_outer g_table1_0_outer
static const uint8_t g_table1_4_emit[2] = {0x73, 0x74};
#define g_table1_4_inner g_table1_0_inner
#define g_table1_4_outer g_table1_0_outer
static const uint8_t g_table1_5_emit[4] = {0x20, 0x25, 0x2d, 0x2e};
static const uint16_t g_table1_5_inner[4] = {0x0006, 0x0046, 0x0086, 0x00c6};
static const uint8_t g_table1_5_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
static const uint8_t g_table1_6_emit[4] = {0x2f, 0x33, 0x34, 0x35};
#define g_table1_6_inner g_table1_5_inner
#define g_table1_6_outer g_table1_5_outer
static const uint8_t g_table1_7_emit[4] = {0x36, 0x37, 0x38, 0x39};
#define g_table1_7_inner g_table1_5_inner
#define g_table1_7_outer g_table1_5_outer
static const uint8_t g_table1_8_emit[4] = {0x3d, 0x41, 0x5f, 0x62};
#define g_table1_8_inner g_table1_5_inner
#define g_table1_8_outer g_table1_5_outer
static const uint8_t g_table1_9_emit[4] = {0x64, 0x66, 0x67, 0x68};
#define g_table1_9_inner g_table1_5_inner
#define g_table1_9_outer g_table1_5_outer
static const uint8_t g_table1_10_emit[4] = {0x6c, 0x6d, 0x6e, 0x70};
#define g_table1_10_inner g_table1_5_inner
#define g_table1_10_outer g_table1_5_outer
static const uint8_t g_table1_11_emit[6] = {0x72, 0x75, 0x3a, 0x42, 0x43, 0x44};
static const uint16_t g_table1_11_inner[6] = {0x0006, 0x0046, 0x0087,
                                              0x00c7, 0x0107, 0x0147};
static const uint8_t g_table1_11_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
    3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5};
static const uint8_t g_table1_12_emit[8] = {0x45, 0x46, 0x47, 0x48,
                                            0x49, 0x4a, 0x4b, 0x4c};
static const uint16_t g_table1_12_inner[8] = {0x0007, 0x0047, 0x0087, 0x00c7,
                                              0x0107, 0x0147, 0x0187, 0x01c7};
static const uint8_t g_table1_12_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7};
static const uint8_t g_table1_13_emit[8] = {0x4d, 0x4e, 0x4f, 0x50,
                                            0x51, 0x52, 0x53, 0x54};
#define g_table1_13_inner g_table1_12_inner
#define g_table1_13_outer g_table1_12_outer
static const uint8_t g_table1_14_emit[8] = {0x55, 0x56, 0x57, 0x59,
                                            0x6a, 0x6b, 0x71, 0x76};
#define g_table1_14_inner g_table1_12_inner
#define g_table1_14_outer g_table1_12_outer
static const uint8_t g_table1_15_emit[15] = {0x77, 0x78, 0x79, 0x7a, 0x26,
                                             0x2a, 0x2c, 0x3b, 0x58, 0x5a,
                                             0x21, 0x22, 0x28, 0x29, 0x3f};
static const uint16_t g_table1_15_inner[18] = {
    0x0007, 0x0047, 0x0087, 0x00c7, 0x0108, 0x0148, 0x0188, 0x01c8, 0x0208,
    0x0248, 0x028a, 0x02ca, 0x030a, 0x034a, 0x038a, 0x001a, 0x002a, 0x003a};
static const uint8_t g_table1_15_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,  1,  1,  1,  2,  2,  2,  2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  6,  6, 6, 6,
    7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static const uint8_t* const g_table1_emit[] = {
    g_table1_0_emit,  g_table1_1_emit,  g_table1_2_emit,  g_table1_3_emit,
    g_table1_4_emit,  g_table1_5_emit,  g_table1_6_emit,  g_table1_7_emit,
    g_table1_8_emit,  g_table1_9_emit,  g_table1_10_emit, g_table1_11_emit,
    g_table1_12_emit, g_table1_13_emit, g_table1_14_emit, g_table1_15_emit,
};
static const uint16_t* const g_table1_inner[] = {
    g_table1_0_inner,  g_table1_1_inner,  g_table1_2_inner,  g_table1_3_inner,
    g_table1_4_inner,  g_table1_5_inner,  g_table1_6_inner,  g_table1_7_inner,
    g_table1_8_inner,  g_table1_9_inner,  g_table1_10_inner, g_table1_11_inner,
    g_table1_12_inner, g_table1_13_inner, g_table1_14_inner, g_table1_15_inner,
};
static const uint8_t* const g_table1_outer[] = {
    g_table1_0_outer,  g_table1_1_outer,  g_table1_2_outer,  g_table1_3_outer,
    g_table1_4_outer,  g_table1_5_outer,  g_table1_6_outer,  g_table1_7_outer,
    g_table1_8_outer,  g_table1_9_outer,  g_table1_10_outer, g_table1_11_outer,
    g_table1_12_outer, g_table1_13_outer, g_table1_14_outer, g_table1_15_outer,
};
inline uint64_t GetOp1(size_t i) {
  return g_table1_inner[i >> 6][g_table1_outer[i >> 6][i & 0x3f]];
}
inline uint64_t GetEmit1(size_t i, size_t emit) {
  return g_table1_emit[i >> 6][emit];
}
static const uint8_t g_table2_0_emit[71] = {
    0x30, 0x30, 0x31, 0x30, 0x32, 0x30, 0x61, 0x30, 0x63, 0x30, 0x65, 0x30,
    0x69, 0x30, 0x6f, 0x30, 0x73, 0x30, 0x74, 0x30, 0x20, 0x30, 0x25, 0x30,
    0x2d, 0x30, 0x2e, 0x30, 0x2f, 0x30, 0x33, 0x30, 0x34, 0x30, 0x35, 0x30,
    0x36, 0x30, 0x37, 0x30, 0x38, 0x30, 0x39, 0x30, 0x3d, 0x30, 0x41, 0x30,
    0x5f, 0x30, 0x62, 0x30, 0x64, 0x30, 0x66, 0x30, 0x67, 0x30, 0x68, 0x30,
    0x6c, 0x30, 0x6d, 0x30, 0x6e, 0x30, 0x70, 0x30, 0x72, 0x30, 0x75};
static const uint16_t g_table2_0_inner[37] = {
    0x000a, 0x008a, 0x018a, 0x028a, 0x038a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
static const uint8_t g_table2_0_outer[64] = {
    0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36};
static const uint8_t g_table2_1_emit[71] = {
    0x31, 0x30, 0x31, 0x31, 0x32, 0x31, 0x61, 0x31, 0x63, 0x31, 0x65, 0x31,
    0x69, 0x31, 0x6f, 0x31, 0x73, 0x31, 0x74, 0x31, 0x20, 0x31, 0x25, 0x31,
    0x2d, 0x31, 0x2e, 0x31, 0x2f, 0x31, 0x33, 0x31, 0x34, 0x31, 0x35, 0x31,
    0x36, 0x31, 0x37, 0x31, 0x38, 0x31, 0x39, 0x31, 0x3d, 0x31, 0x41, 0x31,
    0x5f, 0x31, 0x62, 0x31, 0x64, 0x31, 0x66, 0x31, 0x67, 0x31, 0x68, 0x31,
    0x6c, 0x31, 0x6d, 0x31, 0x6e, 0x31, 0x70, 0x31, 0x72, 0x31, 0x75};
static const uint16_t g_table2_1_inner[37] = {
    0x000a, 0x010a, 0x018a, 0x028a, 0x038a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_1_outer g_table2_0_outer
static const uint8_t g_table2_2_emit[71] = {
    0x32, 0x30, 0x32, 0x31, 0x32, 0x32, 0x61, 0x32, 0x63, 0x32, 0x65, 0x32,
    0x69, 0x32, 0x6f, 0x32, 0x73, 0x32, 0x74, 0x32, 0x20, 0x32, 0x25, 0x32,
    0x2d, 0x32, 0x2e, 0x32, 0x2f, 0x32, 0x33, 0x32, 0x34, 0x32, 0x35, 0x32,
    0x36, 0x32, 0x37, 0x32, 0x38, 0x32, 0x39, 0x32, 0x3d, 0x32, 0x41, 0x32,
    0x5f, 0x32, 0x62, 0x32, 0x64, 0x32, 0x66, 0x32, 0x67, 0x32, 0x68, 0x32,
    0x6c, 0x32, 0x6d, 0x32, 0x6e, 0x32, 0x70, 0x32, 0x72, 0x32, 0x75};
static const uint16_t g_table2_2_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x028a, 0x038a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_2_outer g_table2_0_outer
static const uint8_t g_table2_3_emit[71] = {
    0x61, 0x30, 0x61, 0x31, 0x61, 0x32, 0x61, 0x61, 0x63, 0x61, 0x65, 0x61,
    0x69, 0x61, 0x6f, 0x61, 0x73, 0x61, 0x74, 0x61, 0x20, 0x61, 0x25, 0x61,
    0x2d, 0x61, 0x2e, 0x61, 0x2f, 0x61, 0x33, 0x61, 0x34, 0x61, 0x35, 0x61,
    0x36, 0x61, 0x37, 0x61, 0x38, 0x61, 0x39, 0x61, 0x3d, 0x61, 0x41, 0x61,
    0x5f, 0x61, 0x62, 0x61, 0x64, 0x61, 0x66, 0x61, 0x67, 0x61, 0x68, 0x61,
    0x6c, 0x61, 0x6d, 0x61, 0x6e, 0x61, 0x70, 0x61, 0x72, 0x61, 0x75};
static const uint16_t g_table2_3_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x038a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_3_outer g_table2_0_outer
static const uint8_t g_table2_4_emit[71] = {
    0x63, 0x30, 0x63, 0x31, 0x63, 0x32, 0x63, 0x61, 0x63, 0x63, 0x65, 0x63,
    0x69, 0x63, 0x6f, 0x63, 0x73, 0x63, 0x74, 0x63, 0x20, 0x63, 0x25, 0x63,
    0x2d, 0x63, 0x2e, 0x63, 0x2f, 0x63, 0x33, 0x63, 0x34, 0x63, 0x35, 0x63,
    0x36, 0x63, 0x37, 0x63, 0x38, 0x63, 0x39, 0x63, 0x3d, 0x63, 0x41, 0x63,
    0x5f, 0x63, 0x62, 0x63, 0x64, 0x63, 0x66, 0x63, 0x67, 0x63, 0x68, 0x63,
    0x6c, 0x63, 0x6d, 0x63, 0x6e, 0x63, 0x70, 0x63, 0x72, 0x63, 0x75};
static const uint16_t g_table2_4_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x048a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_4_outer g_table2_0_outer
static const uint8_t g_table2_5_emit[71] = {
    0x65, 0x30, 0x65, 0x31, 0x65, 0x32, 0x65, 0x61, 0x65, 0x63, 0x65, 0x65,
    0x69, 0x65, 0x6f, 0x65, 0x73, 0x65, 0x74, 0x65, 0x20, 0x65, 0x25, 0x65,
    0x2d, 0x65, 0x2e, 0x65, 0x2f, 0x65, 0x33, 0x65, 0x34, 0x65, 0x35, 0x65,
    0x36, 0x65, 0x37, 0x65, 0x38, 0x65, 0x39, 0x65, 0x3d, 0x65, 0x41, 0x65,
    0x5f, 0x65, 0x62, 0x65, 0x64, 0x65, 0x66, 0x65, 0x67, 0x65, 0x68, 0x65,
    0x6c, 0x65, 0x6d, 0x65, 0x6e, 0x65, 0x70, 0x65, 0x72, 0x65, 0x75};
static const uint16_t g_table2_5_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x058a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_5_outer g_table2_0_outer
static const uint8_t g_table2_6_emit[71] = {
    0x69, 0x30, 0x69, 0x31, 0x69, 0x32, 0x69, 0x61, 0x69, 0x63, 0x69, 0x65,
    0x69, 0x69, 0x6f, 0x69, 0x73, 0x69, 0x74, 0x69, 0x20, 0x69, 0x25, 0x69,
    0x2d, 0x69, 0x2e, 0x69, 0x2f, 0x69, 0x33, 0x69, 0x34, 0x69, 0x35, 0x69,
    0x36, 0x69, 0x37, 0x69, 0x38, 0x69, 0x39, 0x69, 0x3d, 0x69, 0x41, 0x69,
    0x5f, 0x69, 0x62, 0x69, 0x64, 0x69, 0x66, 0x69, 0x67, 0x69, 0x68, 0x69,
    0x6c, 0x69, 0x6d, 0x69, 0x6e, 0x69, 0x70, 0x69, 0x72, 0x69, 0x75};
static const uint16_t g_table2_6_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x068a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_6_outer g_table2_0_outer
static const uint8_t g_table2_7_emit[71] = {
    0x6f, 0x30, 0x6f, 0x31, 0x6f, 0x32, 0x6f, 0x61, 0x6f, 0x63, 0x6f, 0x65,
    0x6f, 0x69, 0x6f, 0x6f, 0x73, 0x6f, 0x74, 0x6f, 0x20, 0x6f, 0x25, 0x6f,
    0x2d, 0x6f, 0x2e, 0x6f, 0x2f, 0x6f, 0x33, 0x6f, 0x34, 0x6f, 0x35, 0x6f,
    0x36, 0x6f, 0x37, 0x6f, 0x38, 0x6f, 0x39, 0x6f, 0x3d, 0x6f, 0x41, 0x6f,
    0x5f, 0x6f, 0x62, 0x6f, 0x64, 0x6f, 0x66, 0x6f, 0x67, 0x6f, 0x68, 0x6f,
    0x6c, 0x6f, 0x6d, 0x6f, 0x6e, 0x6f, 0x70, 0x6f, 0x72, 0x6f, 0x75};
static const uint16_t g_table2_7_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x070a,
    0x078a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_7_outer g_table2_0_outer
static const uint8_t g_table2_8_emit[71] = {
    0x73, 0x30, 0x73, 0x31, 0x73, 0x32, 0x73, 0x61, 0x73, 0x63, 0x73, 0x65,
    0x73, 0x69, 0x73, 0x6f, 0x73, 0x73, 0x74, 0x73, 0x20, 0x73, 0x25, 0x73,
    0x2d, 0x73, 0x2e, 0x73, 0x2f, 0x73, 0x33, 0x73, 0x34, 0x73, 0x35, 0x73,
    0x36, 0x73, 0x37, 0x73, 0x38, 0x73, 0x39, 0x73, 0x3d, 0x73, 0x41, 0x73,
    0x5f, 0x73, 0x62, 0x73, 0x64, 0x73, 0x66, 0x73, 0x67, 0x73, 0x68, 0x73,
    0x6c, 0x73, 0x6d, 0x73, 0x6e, 0x73, 0x70, 0x73, 0x72, 0x73, 0x75};
static const uint16_t g_table2_8_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x070a,
    0x080a, 0x088a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_8_outer g_table2_0_outer
static const uint8_t g_table2_9_emit[71] = {
    0x74, 0x30, 0x74, 0x31, 0x74, 0x32, 0x74, 0x61, 0x74, 0x63, 0x74, 0x65,
    0x74, 0x69, 0x74, 0x6f, 0x74, 0x73, 0x74, 0x74, 0x20, 0x74, 0x25, 0x74,
    0x2d, 0x74, 0x2e, 0x74, 0x2f, 0x74, 0x33, 0x74, 0x34, 0x74, 0x35, 0x74,
    0x36, 0x74, 0x37, 0x74, 0x38, 0x74, 0x39, 0x74, 0x3d, 0x74, 0x41, 0x74,
    0x5f, 0x74, 0x62, 0x74, 0x64, 0x74, 0x66, 0x74, 0x67, 0x74, 0x68, 0x74,
    0x6c, 0x74, 0x6d, 0x74, 0x6e, 0x74, 0x70, 0x74, 0x72, 0x74, 0x75};
static const uint16_t g_table2_9_inner[37] = {
    0x000a, 0x010a, 0x020a, 0x030a, 0x040a, 0x050a, 0x060a, 0x070a,
    0x080a, 0x090a, 0x098b, 0x0a8b, 0x0b8b, 0x0c8b, 0x0d8b, 0x0e8b,
    0x0f8b, 0x108b, 0x118b, 0x128b, 0x138b, 0x148b, 0x158b, 0x168b,
    0x178b, 0x188b, 0x198b, 0x1a8b, 0x1b8b, 0x1c8b, 0x1d8b, 0x1e8b,
    0x1f8b, 0x208b, 0x218b, 0x228b, 0x0015};
#define g_table2_9_outer g_table2_0_outer
static const uint8_t g_table2_10_emit[40] = {
    0x20, 0x30, 0x20, 0x31, 0x20, 0x32, 0x20, 0x61, 0x20, 0x63,
    0x20, 0x65, 0x20, 0x69, 0x20, 0x6f, 0x20, 0x73, 0x20, 0x74,
    0x25, 0x30, 0x25, 0x31, 0x25, 0x32, 0x25, 0x61, 0x25, 0x63,
    0x25, 0x65, 0x25, 0x69, 0x25, 0x6f, 0x25, 0x73, 0x25, 0x74};
static const uint16_t g_table2_10_inner[22] = {
    0x000b, 0x010b, 0x020b, 0x030b, 0x040b, 0x050b, 0x060b, 0x070b,
    0x080b, 0x090b, 0x0016, 0x0a0b, 0x0b0b, 0x0c0b, 0x0d0b, 0x0e0b,
    0x0f0b, 0x100b, 0x110b, 0x120b, 0x130b, 0x0a16};
static const uint8_t g_table2_10_outer[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21};
static const uint8_t g_table2_11_emit[40] = {
    0x2d, 0x30, 0x2d, 0x31, 0x2d, 0x32, 0x2d, 0x61, 0x2d, 0x63,
    0x2d, 0x65, 0x2d, 0x69, 0x2d, 0x6f, 0x2d, 0x73, 0x2d, 0x74,
    0x2e, 0x30, 0x2e, 0x31, 0x2e, 0x32, 0x2e, 0x61, 0x2e, 0x63,
    0x2e, 0x65, 0x2e, 0x69, 0x2e, 0x6f, 0x2e, 0x73, 0x2e, 0x74};
#define g_table2_11_inner g_table2_10_inner
#define g_table2_11_outer g_table2_10_outer
static const uint8_t g_table2_12_emit[40] = {
    0x2f, 0x30, 0x2f, 0x31, 0x2f, 0x32, 0x2f, 0x61, 0x2f, 0x63,
    0x2f, 0x65, 0x2f, 0x69, 0x2f, 0x6f, 0x2f, 0x73, 0x2f, 0x74,
    0x33, 0x30, 0x33, 0x31, 0x33, 0x32, 0x33, 0x61, 0x33, 0x63,
    0x33, 0x65, 0x33, 0x69, 0x33, 0x6f, 0x33, 0x73, 0x33, 0x74};
#define g_table2_12_inner g_table2_10_inner
#define g_table2_12_outer g_table2_10_outer
static const uint8_t g_table2_13_emit[40] = {
    0x34, 0x30, 0x34, 0x31, 0x34, 0x32, 0x34, 0x61, 0x34, 0x63,
    0x34, 0x65, 0x34, 0x69, 0x34, 0x6f, 0x34, 0x73, 0x34, 0x74,
    0x35, 0x30, 0x35, 0x31, 0x35, 0x32, 0x35, 0x61, 0x35, 0x63,
    0x35, 0x65, 0x35, 0x69, 0x35, 0x6f, 0x35, 0x73, 0x35, 0x74};
#define g_table2_13_inner g_table2_10_inner
#define g_table2_13_outer g_table2_10_outer
static const uint8_t g_table2_14_emit[40] = {
    0x36, 0x30, 0x36, 0x31, 0x36, 0x32, 0x36, 0x61, 0x36, 0x63,
    0x36, 0x65, 0x36, 0x69, 0x36, 0x6f, 0x36, 0x73, 0x36, 0x74,
    0x37, 0x30, 0x37, 0x31, 0x37, 0x32, 0x37, 0x61, 0x37, 0x63,
    0x37, 0x65, 0x37, 0x69, 0x37, 0x6f, 0x37, 0x73, 0x37, 0x74};
#define g_table2_14_inner g_table2_10_inner
#define g_table2_14_outer g_table2_10_outer
static const uint8_t g_table2_15_emit[40] = {
    0x38, 0x30, 0x38, 0x31, 0x38, 0x32, 0x38, 0x61, 0x38, 0x63,
    0x38, 0x65, 0x38, 0x69, 0x38, 0x6f, 0x38, 0x73, 0x38, 0x74,
    0x39, 0x30, 0x39, 0x31, 0x39, 0x32, 0x39, 0x61, 0x39, 0x63,
    0x39, 0x65, 0x39, 0x69, 0x39, 0x6f, 0x39, 0x73, 0x39, 0x74};
#define g_table2_15_inner g_table2_10_inner
#define g_table2_15_outer g_table2_10_outer
static const uint8_t g_table2_16_emit[40] = {
    0x3d, 0x30, 0x3d, 0x31, 0x3d, 0x32, 0x3d, 0x61, 0x3d, 0x63,
    0x3d, 0x65, 0x3d, 0x69, 0x3d, 0x6f, 0x3d, 0x73, 0x3d, 0x74,
    0x41, 0x30, 0x41, 0x31, 0x41, 0x32, 0x41, 0x61, 0x41, 0x63,
    0x41, 0x65, 0x41, 0x69, 0x41, 0x6f, 0x41, 0x73, 0x41, 0x74};
#define g_table2_16_inner g_table2_10_inner
#define g_table2_16_outer g_table2_10_outer
static const uint8_t g_table2_17_emit[40] = {
    0x5f, 0x30, 0x5f, 0x31, 0x5f, 0x32, 0x5f, 0x61, 0x5f, 0x63,
    0x5f, 0x65, 0x5f, 0x69, 0x5f, 0x6f, 0x5f, 0x73, 0x5f, 0x74,
    0x62, 0x30, 0x62, 0x31, 0x62, 0x32, 0x62, 0x61, 0x62, 0x63,
    0x62, 0x65, 0x62, 0x69, 0x62, 0x6f, 0x62, 0x73, 0x62, 0x74};
#define g_table2_17_inner g_table2_10_inner
#define g_table2_17_outer g_table2_10_outer
static const uint8_t g_table2_18_emit[40] = {
    0x64, 0x30, 0x64, 0x31, 0x64, 0x32, 0x64, 0x61, 0x64, 0x63,
    0x64, 0x65, 0x64, 0x69, 0x64, 0x6f, 0x64, 0x73, 0x64, 0x74,
    0x66, 0x30, 0x66, 0x31, 0x66, 0x32, 0x66, 0x61, 0x66, 0x63,
    0x66, 0x65, 0x66, 0x69, 0x66, 0x6f, 0x66, 0x73, 0x66, 0x74};
#define g_table2_18_inner g_table2_10_inner
#define g_table2_18_outer g_table2_10_outer
static const uint8_t g_table2_19_emit[40] = {
    0x67, 0x30, 0x67, 0x31, 0x67, 0x32, 0x67, 0x61, 0x67, 0x63,
    0x67, 0x65, 0x67, 0x69, 0x67, 0x6f, 0x67, 0x73, 0x67, 0x74,
    0x68, 0x30, 0x68, 0x31, 0x68, 0x32, 0x68, 0x61, 0x68, 0x63,
    0x68, 0x65, 0x68, 0x69, 0x68, 0x6f, 0x68, 0x73, 0x68, 0x74};
#define g_table2_19_inner g_table2_10_inner
#define g_table2_19_outer g_table2_10_outer
static const uint8_t g_table2_20_emit[40] = {
    0x6c, 0x30, 0x6c, 0x31, 0x6c, 0x32, 0x6c, 0x61, 0x6c, 0x63,
    0x6c, 0x65, 0x6c, 0x69, 0x6c, 0x6f, 0x6c, 0x73, 0x6c, 0x74,
    0x6d, 0x30, 0x6d, 0x31, 0x6d, 0x32, 0x6d, 0x61, 0x6d, 0x63,
    0x6d, 0x65, 0x6d, 0x69, 0x6d, 0x6f, 0x6d, 0x73, 0x6d, 0x74};
#define g_table2_20_inner g_table2_10_inner
#define g_table2_20_outer g_table2_10_outer
static const uint8_t g_table2_21_emit[40] = {
    0x6e, 0x30, 0x6e, 0x31, 0x6e, 0x32, 0x6e, 0x61, 0x6e, 0x63,
    0x6e, 0x65, 0x6e, 0x69, 0x6e, 0x6f, 0x6e, 0x73, 0x6e, 0x74,
    0x70, 0x30, 0x70, 0x31, 0x70, 0x32, 0x70, 0x61, 0x70, 0x63,
    0x70, 0x65, 0x70, 0x69, 0x70, 0x6f, 0x70, 0x73, 0x70, 0x74};
#define g_table2_21_inner g_table2_10_inner
#define g_table2_21_outer g_table2_10_outer
static const uint8_t g_table2_22_emit[40] = {
    0x72, 0x30, 0x72, 0x31, 0x72, 0x32, 0x72, 0x61, 0x72, 0x63,
    0x72, 0x65, 0x72, 0x69, 0x72, 0x6f, 0x72, 0x73, 0x72, 0x74,
    0x75, 0x30, 0x75, 0x31, 0x75, 0x32, 0x75, 0x61, 0x75, 0x63,
    0x75, 0x65, 0x75, 0x69, 0x75, 0x6f, 0x75, 0x73, 0x75, 0x74};
#define g_table2_22_inner g_table2_10_inner
#define g_table2_22_outer g_table2_10_outer
static const uint8_t g_table2_23_emit[4] = {0x3a, 0x42, 0x43, 0x44};
static const uint16_t g_table2_23_inner[4] = {0x0017, 0x0097, 0x0117, 0x0197};
#define g_table2_23_outer g_table1_5_outer
static const uint8_t g_table2_24_emit[4] = {0x45, 0x46, 0x47, 0x48};
#define g_table2_24_inner g_table2_23_inner
#define g_table2_24_outer g_table1_5_outer
static const uint8_t g_table2_25_emit[4] = {0x49, 0x4a, 0x4b, 0x4c};
#define g_table2_25_inner g_table2_23_inner
#define g_table2_25_outer g_table1_5_outer
static const uint8_t g_table2_26_emit[4] = {0x4d, 0x4e, 0x4f, 0x50};
#define g_table2_26_inner g_table2_23_inner
#define g_table2_26_outer g_table1_5_outer
static const uint8_t g_table2_27_emit[4] = {0x51, 0x52, 0x53, 0x54};
#define g_table2_27_inner g_table2_23_inner
#define g_table2_27_outer g_table1_5_outer
static const uint8_t g_table2_28_emit[4] = {0x55, 0x56, 0x57, 0x59};
#define g_table2_28_inner g_table2_23_inner
#define g_table2_28_outer g_table1_5_outer
static const uint8_t g_table2_29_emit[4] = {0x6a, 0x6b, 0x71, 0x76};
#define g_table2_29_inner g_table2_23_inner
#define g_table2_29_outer g_table1_5_outer
static const uint8_t g_table2_30_emit[4] = {0x77, 0x78, 0x79, 0x7a};
#define g_table2_30_inner g_table2_23_inner
#define g_table2_30_outer g_table1_5_outer
static const uint8_t g_table2_31_emit[14] = {0x26, 0x2a, 0x2c, 0x3b, 0x58,
                                             0x5a, 0x21, 0x22, 0x28, 0x29,
                                             0x3f, 0x27, 0x2b, 0x7c};
static const uint16_t g_table2_31_inner[17] = {
    0x0018, 0x0098, 0x0118, 0x0198, 0x0218, 0x0298, 0x031a, 0x039a, 0x041a,
    0x049a, 0x051a, 0x059b, 0x061b, 0x069b, 0x002b, 0x003b, 0x004b};
static const uint8_t g_table2_31_outer[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,  1,  1,  1,  2,  2,  2,  2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4,  4,  4,  4,  4,  4,  5,  5, 5, 5,
    5, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15, 16};
static const uint8_t* const g_table2_emit[] = {
    g_table2_0_emit,  g_table2_1_emit,  g_table2_2_emit,  g_table2_3_emit,
    g_table2_4_emit,  g_table2_5_emit,  g_table2_6_emit,  g_table2_7_emit,
    g_table2_8_emit,  g_table2_9_emit,  g_table2_10_emit, g_table2_11_emit,
    g_table2_12_emit, g_table2_13_emit, g_table2_14_emit, g_table2_15_emit,
    g_table2_16_emit, g_table2_17_emit, g_table2_18_emit, g_table2_19_emit,
    g_table2_20_emit, g_table2_21_emit, g_table2_22_emit, g_table2_23_emit,
    g_table2_24_emit, g_table2_25_emit, g_table2_26_emit, g_table2_27_emit,
    g_table2_28_emit, g_table2_29_emit, g_table2_30_emit, g_table2_31_emit,
};
static const uint16_t* const g_table2_inner[] = {
    g_table2_0_inner,  g_table2_1_inner,  g_table2_2_inner,  g_table2_3_inner,
    g_table2_4_inner,  g_table2_5_inner,  g_table2_6_inner,  g_table2_7_inner,
    g_table2_8_inner,  g_table2_9_inner,  g_table2_10_inner, g_table2_11_inner,
    g_table2_12_inner, g_table2_13_inner, g_table2_14_inner, g_table2_15_inner,
    g_table2_16_inner, g_table2_17_inner, g_table2_18_inner, g_table2_19_inner,
    g_table2_20_inner, g_table2_21_inner, g_table2_22_inner, g_table2_23_inner,
    g_table2_24_inner, g_table2_25_inner, g_table2_26_inner, g_table2_27_inner,
    g_table2_28_inner, g_table2_29_inner, g_table2_30_inner, g_table2_31_inner,
};
static const uint8_t* const g_table2_outer[] = {
    g_table2_0_outer,  g_table2_1_outer,  g_table2_2_outer,  g_table2_3_outer,
    g_table2_4_outer,  g_table2_5_outer,  g_table2_6_outer,  g_table2_7_outer,
    g_table2_8_outer,  g_table2_9_outer,  g_table2_10_outer, g_table2_11_outer,
    g_table2_12_outer, g_table2_13_outer, g_table2_14_outer, g_table2_15_outer,
    g_table2_16_outer, g_table2_17_outer, g_table2_18_outer, g_table2_19_outer,
    g_table2_20_outer, g_table2_21_outer, g_table2_22_outer, g_table2_23_outer,
    g_table2_24_outer, g_table2_25_outer, g_table2_26_outer, g_table2_27_outer,
    g_table2_28_outer, g_table2_29_outer, g_table2_30_outer, g_table2_31_outer,
};
inline uint64_t GetOp2(size_t i) {
  return g_table2_inner[i >> 6][g_table2_outer[i >> 6][i & 0x3f]];
}
inline uint64_t GetEmit2(size_t i, size_t emit) {
  return g_table2_emit[i >> 6][emit];
}
static const uint8_t g_table3_0_emit[2] = {0x23, 0x3e};
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
static const uint8_t g_table4_0_emit[4] = {0x00, 0x24, 0x40, 0x5b};
static const uint8_t g_table4_0_ops[4] = {0x02, 0x06, 0x0a, 0x0e};
static const uint8_t* const g_table4_emit[] = {
    g_table4_0_emit,
};
static const uint8_t* const g_table4_ops[] = {
    g_table4_0_ops,
};
inline uint64_t GetOp4(size_t i) { return g_table4_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit4(size_t i, size_t emit) {
  return g_table4_emit[i >> 2][emit];
}
static const uint8_t g_table5_0_emit[1] = {0x5d};
static const uint16_t g_table5_0_inner[1] = {0x0002};
static const uint8_t g_table5_0_outer[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#define g_table5_1_emit g_table5_0_emit
#define g_table5_1_inner g_table5_0_inner
#define g_table5_1_outer g_table5_0_outer
#define g_table5_2_emit g_table5_0_emit
#define g_table5_2_inner g_table5_0_inner
#define g_table5_2_outer g_table5_0_outer
#define g_table5_3_emit g_table5_0_emit
#define g_table5_3_inner g_table5_0_inner
#define g_table5_3_outer g_table5_0_outer
static const uint8_t g_table5_4_emit[1] = {0x7e};
#define g_table5_4_inner g_table5_0_inner
#define g_table5_4_outer g_table5_0_outer
#define g_table5_5_emit g_table5_4_emit
#define g_table5_5_inner g_table5_0_inner
#define g_table5_5_outer g_table5_0_outer
#define g_table5_6_emit g_table5_4_emit
#define g_table5_6_inner g_table5_0_inner
#define g_table5_6_outer g_table5_0_outer
#define g_table5_7_emit g_table5_4_emit
#define g_table5_7_inner g_table5_0_inner
#define g_table5_7_outer g_table5_0_outer
static const uint8_t g_table5_8_emit[1] = {0x5e};
static const uint16_t g_table5_8_inner[1] = {0x0003};
#define g_table5_8_outer g_table5_0_outer
#define g_table5_9_emit g_table5_8_emit
#define g_table5_9_inner g_table5_8_inner
#define g_table5_9_outer g_table5_0_outer
static const uint8_t g_table5_10_emit[1] = {0x7d};
#define g_table5_10_inner g_table5_8_inner
#define g_table5_10_outer g_table5_0_outer
#define g_table5_11_emit g_table5_10_emit
#define g_table5_11_inner g_table5_8_inner
#define g_table5_11_outer g_table5_0_outer
static const uint8_t g_table5_12_emit[1] = {0x3c};
static const uint16_t g_table5_12_inner[1] = {0x0004};
#define g_table5_12_outer g_table5_0_outer
static const uint8_t g_table5_13_emit[1] = {0x60};
#define g_table5_13_inner g_table5_12_inner
#define g_table5_13_outer g_table5_0_outer
static const uint8_t g_table5_14_emit[1] = {0x7b};
#define g_table5_14_inner g_table5_12_inner
#define g_table5_14_outer g_table5_0_outer
static const uint8_t g_table5_15_emit[50] = {
    0x5c, 0xc3, 0xd0, 0x80, 0x82, 0x83, 0xa2, 0xb8, 0xc2, 0xe0,
    0xe2, 0x99, 0xa1, 0xa7, 0xac, 0xb0, 0xb1, 0xb3, 0xd1, 0xd8,
    0xd9, 0xe3, 0xe5, 0xe6, 0x81, 0x84, 0x85, 0x86, 0x88, 0x92,
    0x9a, 0x9c, 0xa0, 0xa3, 0xa4, 0xa9, 0xaa, 0xad, 0xb2, 0xb5,
    0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4, 0xe8, 0xe9};
static const uint16_t g_table5_15_inner[70] = {
    0x0008, 0x0208, 0x0408, 0x0609, 0x0809, 0x0a09, 0x0c09, 0x0e09, 0x1009,
    0x1209, 0x1409, 0x160a, 0x180a, 0x1a0a, 0x1c0a, 0x1e0a, 0x200a, 0x220a,
    0x240a, 0x260a, 0x280a, 0x2a0a, 0x2c0a, 0x2e0a, 0x300b, 0x320b, 0x340b,
    0x360b, 0x380b, 0x3a0b, 0x3c0b, 0x3e0b, 0x400b, 0x420b, 0x440b, 0x460b,
    0x480b, 0x4a0b, 0x4c0b, 0x4e0b, 0x500b, 0x520b, 0x540b, 0x560b, 0x580b,
    0x5a0b, 0x5c0b, 0x5e0b, 0x600b, 0x620b, 0x001b, 0x002b, 0x003b, 0x004b,
    0x005b, 0x006b, 0x007b, 0x008b, 0x009b, 0x00ab, 0x00bb, 0x00cb, 0x00db,
    0x00eb, 0x00fb, 0x010b, 0x011b, 0x012b, 0x013b, 0x014b};
static const uint8_t g_table5_15_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
    2,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,
    6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 11,
    11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20,
    21, 21, 22, 22, 23, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69};
static const uint8_t* const g_table5_emit[] = {
    g_table5_0_emit,  g_table5_1_emit,  g_table5_2_emit,  g_table5_3_emit,
    g_table5_4_emit,  g_table5_5_emit,  g_table5_6_emit,  g_table5_7_emit,
    g_table5_8_emit,  g_table5_9_emit,  g_table5_10_emit, g_table5_11_emit,
    g_table5_12_emit, g_table5_13_emit, g_table5_14_emit, g_table5_15_emit,
};
static const uint16_t* const g_table5_inner[] = {
    g_table5_0_inner,  g_table5_1_inner,  g_table5_2_inner,  g_table5_3_inner,
    g_table5_4_inner,  g_table5_5_inner,  g_table5_6_inner,  g_table5_7_inner,
    g_table5_8_inner,  g_table5_9_inner,  g_table5_10_inner, g_table5_11_inner,
    g_table5_12_inner, g_table5_13_inner, g_table5_14_inner, g_table5_15_inner,
};
static const uint8_t* const g_table5_outer[] = {
    g_table5_0_outer,  g_table5_1_outer,  g_table5_2_outer,  g_table5_3_outer,
    g_table5_4_outer,  g_table5_5_outer,  g_table5_6_outer,  g_table5_7_outer,
    g_table5_8_outer,  g_table5_9_outer,  g_table5_10_outer, g_table5_11_outer,
    g_table5_12_outer, g_table5_13_outer, g_table5_14_outer, g_table5_15_outer,
};
inline uint64_t GetOp5(size_t i) {
  return g_table5_inner[i >> 7][g_table5_outer[i >> 7][i & 0x7f]];
}
inline uint64_t GetEmit5(size_t i, size_t emit) {
  return g_table5_emit[i >> 7][emit];
}
static const uint8_t g_table6_0_emit[2] = {0x01, 0x87};
#define g_table6_0_ops g_table3_0_ops
static const uint8_t* const g_table6_emit[] = {
    g_table6_0_emit,
};
static const uint8_t* const g_table6_ops[] = {
    g_table6_0_ops,
};
inline uint64_t GetOp6(size_t i) { return g_table6_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit6(size_t i, size_t emit) {
  return g_table6_emit[i >> 1][emit];
}
static const uint8_t g_table7_0_emit[2] = {0x89, 0x8a};
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
static const uint8_t g_table8_0_emit[2] = {0x8b, 0x8c};
#define g_table8_0_ops g_table3_0_ops
static const uint8_t* const g_table8_emit[] = {
    g_table8_0_emit,
};
static const uint8_t* const g_table8_ops[] = {
    g_table8_0_ops,
};
inline uint64_t GetOp8(size_t i) { return g_table8_ops[i >> 1][i & 0x1]; }
inline uint64_t GetEmit8(size_t i, size_t emit) {
  return g_table8_emit[i >> 1][emit];
}
static const uint8_t g_table9_0_emit[2] = {0x8d, 0x8f};
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
static const uint8_t g_table10_0_emit[2] = {0x93, 0x95};
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
static const uint8_t g_table11_0_emit[2] = {0x96, 0x97};
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
static const uint8_t g_table12_0_emit[2] = {0x98, 0x9b};
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
static const uint8_t g_table13_0_emit[2] = {0x9d, 0x9e};
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
static const uint8_t g_table14_0_emit[2] = {0xa5, 0xa6};
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
static const uint8_t g_table15_0_emit[2] = {0xa8, 0xae};
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
static const uint8_t g_table16_0_emit[2] = {0xaf, 0xb4};
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
static const uint8_t g_table17_0_emit[2] = {0xb6, 0xb7};
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
static const uint8_t g_table18_0_emit[2] = {0xbc, 0xbf};
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
static const uint8_t g_table19_0_emit[2] = {0xc5, 0xe7};
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
static const uint8_t g_table20_0_emit[4] = {0x90, 0x91, 0x94, 0x9f};
#define g_table20_0_ops g_table4_0_ops
static const uint8_t* const g_table20_emit[] = {
    g_table20_0_emit,
};
static const uint8_t* const g_table20_ops[] = {
    g_table20_0_ops,
};
inline uint64_t GetOp20(size_t i) { return g_table20_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit20(size_t i, size_t emit) {
  return g_table20_emit[i >> 2][emit];
}
static const uint8_t g_table21_0_emit[4] = {0xab, 0xce, 0xd7, 0xe1};
#define g_table21_0_ops g_table4_0_ops
static const uint8_t* const g_table21_emit[] = {
    g_table21_0_emit,
};
static const uint8_t* const g_table21_ops[] = {
    g_table21_0_ops,
};
inline uint64_t GetOp21(size_t i) { return g_table21_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit21(size_t i, size_t emit) {
  return g_table21_emit[i >> 2][emit];
}
static const uint8_t g_table22_0_emit[17] = {0xc0, 0xc1, 0xc8, 0xc9, 0xca, 0xcd,
                                             0xd2, 0xd5, 0xda, 0xdb, 0xee, 0xf0,
                                             0xf2, 0xf3, 0xff, 0xcb, 0xcc};
static const uint8_t g_table22_0_ops[32] = {
    0x04, 0x04, 0x0c, 0x0c, 0x14, 0x14, 0x1c, 0x1c, 0x24, 0x24, 0x2c,
    0x2c, 0x34, 0x34, 0x3c, 0x3c, 0x44, 0x44, 0x4c, 0x4c, 0x54, 0x54,
    0x5c, 0x5c, 0x64, 0x64, 0x6c, 0x6c, 0x74, 0x74, 0x7d, 0x85};
static const uint8_t* const g_table22_emit[] = {
    g_table22_0_emit,
};
static const uint8_t* const g_table22_ops[] = {
    g_table22_0_ops,
};
inline uint64_t GetOp22(size_t i) { return g_table22_ops[i >> 5][i & 0x1f]; }
inline uint64_t GetEmit22(size_t i, size_t emit) {
  return g_table22_emit[i >> 5][emit];
}
static const uint8_t g_table23_0_emit[3] = {0xef, 0x09, 0x8e};
static const uint8_t g_table23_0_ops[4] = {0x01, 0x01, 0x06, 0x0a};
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
static const uint8_t g_table24_0_emit[6] = {0xec, 0xed, 0xc7, 0xcf, 0xea, 0xeb};
static const uint8_t g_table24_0_ops[8] = {0x02, 0x02, 0x06, 0x06,
                                           0x0b, 0x0f, 0x13, 0x17};
static const uint8_t* const g_table24_emit[] = {
    g_table24_0_emit,
};
static const uint8_t* const g_table24_ops[] = {
    g_table24_0_ops,
};
inline uint64_t GetOp24(size_t i) { return g_table24_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit24(size_t i, size_t emit) {
  return g_table24_emit[i >> 3][emit];
}
static const uint8_t g_table25_0_emit[4] = {0xd3, 0xd4, 0xd6, 0xdd};
static const uint16_t g_table25_0_ops[32] = {
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0025, 0x0025, 0x0025, 0x0025, 0x0025, 0x0025, 0x0025, 0x0025,
    0x0045, 0x0045, 0x0045, 0x0045, 0x0045, 0x0045, 0x0045, 0x0045,
    0x0065, 0x0065, 0x0065, 0x0065, 0x0065, 0x0065, 0x0065, 0x0065};
static const uint8_t g_table25_1_emit[4] = {0xde, 0xdf, 0xf1, 0xf4};
#define g_table25_1_ops g_table25_0_ops
static const uint8_t g_table25_2_emit[4] = {0xf5, 0xf6, 0xf7, 0xf8};
#define g_table25_2_ops g_table25_0_ops
static const uint8_t g_table25_3_emit[4] = {0xfa, 0xfb, 0xfc, 0xfd};
#define g_table25_3_ops g_table25_0_ops
static const uint8_t g_table25_4_emit[7] = {0xfe, 0x02, 0x03, 0x04,
                                            0x05, 0x06, 0x07};
static const uint16_t g_table25_4_ops[32] = {
    0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005,
    0x0026, 0x0026, 0x0026, 0x0026, 0x0046, 0x0046, 0x0046, 0x0046,
    0x0066, 0x0066, 0x0066, 0x0066, 0x0086, 0x0086, 0x0086, 0x0086,
    0x00a6, 0x00a6, 0x00a6, 0x00a6, 0x00c6, 0x00c6, 0x00c6, 0x00c6};
static const uint8_t g_table25_5_emit[8] = {0x08, 0x0b, 0x0c, 0x0e,
                                            0x0f, 0x10, 0x11, 0x12};
static const uint16_t g_table25_5_ops[32] = {
    0x0006, 0x0006, 0x0006, 0x0006, 0x0026, 0x0026, 0x0026, 0x0026,
    0x0046, 0x0046, 0x0046, 0x0046, 0x0066, 0x0066, 0x0066, 0x0066,
    0x0086, 0x0086, 0x0086, 0x0086, 0x00a6, 0x00a6, 0x00a6, 0x00a6,
    0x00c6, 0x00c6, 0x00c6, 0x00c6, 0x00e6, 0x00e6, 0x00e6, 0x00e6};
static const uint8_t g_table25_6_emit[8] = {0x13, 0x14, 0x15, 0x17,
                                            0x18, 0x19, 0x1a, 0x1b};
#define g_table25_6_ops g_table25_5_ops
static const uint8_t g_table25_7_emit[10] = {0x1c, 0x1d, 0x1e, 0x1f, 0x7f,
                                             0xdc, 0xf9, 0x0a, 0x0d, 0x16};
static const uint16_t g_table25_7_ops[32] = {
    0x0006, 0x0006, 0x0006, 0x0006, 0x0026, 0x0026, 0x0026, 0x0026,
    0x0046, 0x0046, 0x0046, 0x0046, 0x0066, 0x0066, 0x0066, 0x0066,
    0x0086, 0x0086, 0x0086, 0x0086, 0x00a6, 0x00a6, 0x00a6, 0x00a6,
    0x00c6, 0x00c6, 0x00c6, 0x00c6, 0x00e8, 0x0108, 0x0128, 0x0018};
static const uint8_t* const g_table25_emit[] = {
    g_table25_0_emit, g_table25_1_emit, g_table25_2_emit, g_table25_3_emit,
    g_table25_4_emit, g_table25_5_emit, g_table25_6_emit, g_table25_7_emit,
};
static const uint16_t* const g_table25_ops[] = {
    g_table25_0_ops, g_table25_1_ops, g_table25_2_ops, g_table25_3_ops,
    g_table25_4_ops, g_table25_5_ops, g_table25_6_ops, g_table25_7_ops,
};
inline uint64_t GetOp25(size_t i) { return g_table25_ops[i >> 5][i & 0x1f]; }
inline uint64_t GetEmit25(size_t i, size_t emit) {
  return g_table25_emit[i >> 5][emit];
}
template <typename F>
class HuffDecoder {
 public:
  HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end)
      : sink_(sink), begin_(begin), end_(end) {}
  bool Run() {
    while (ok_) {
      if (!RefillTo11()) {
        Done();
        return ok_;
      }
      const auto index = (buffer_ >> (buffer_len_ - 11)) & 0x7ff;
      const auto op = GetOp2(index);
      buffer_len_ -= op & 15;
      const auto emit_ofs = op >> 7;
      switch ((op >> 4) & 7) {
        case 2: {
          DecodeStep0();
          break;
        }
        case 3: {
          DecodeStep1();
          break;
        }
        case 4: {
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
  bool RefillTo11() {
    switch (buffer_len_) {
      case 10:
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
      case 1:
      case 2: {
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp4(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit4(index, emit_ofs + 0));
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
  void DecodeStep2() {
    if (!RefillTo11()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 11)) & 0x7ff;
    const auto op = GetOp5(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 9;
    switch ((op >> 4) & 31) {
      case 8: {
        DecodeStep10();
        break;
      }
      case 9: {
        DecodeStep11();
        break;
      }
      case 10: {
        DecodeStep12();
        break;
      }
      case 11: {
        DecodeStep13();
        break;
      }
      case 12: {
        DecodeStep14();
        break;
      }
      case 13: {
        DecodeStep15();
        break;
      }
      case 14: {
        DecodeStep16();
        break;
      }
      case 16: {
        DecodeStep17();
        break;
      }
      case 17: {
        DecodeStep18();
        break;
      }
      case 19: {
        DecodeStep19();
        break;
      }
      case 15: {
        DecodeStep20();
        break;
      }
      case 18: {
        DecodeStep21();
        break;
      }
      case 20: {
        DecodeStep22();
        break;
      }
      case 1: {
        DecodeStep3();
        break;
      }
      case 2: {
        DecodeStep4();
        break;
      }
      case 3: {
        DecodeStep5();
        break;
      }
      case 4: {
        DecodeStep6();
        break;
      }
      case 5: {
        DecodeStep7();
        break;
      }
      case 6: {
        DecodeStep8();
        break;
      }
      case 7: {
        DecodeStep9();
        break;
      }
      case 0: {
        sink_(GetEmit5(index, emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep3() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp6(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit6(index, emit_ofs + 0));
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
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 1)) & 0x1;
    const auto op = GetOp8(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit8(index, emit_ofs + 0));
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp20(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit20(index, emit_ofs + 0));
  }
  void DecodeStep18() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp21(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit21(index, emit_ofs + 0));
  }
  void DecodeStep19() {
    if (!RefillTo5()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 5)) & 0x1f;
    const auto op = GetOp22(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit22(index, emit_ofs + 0));
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
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetOp24(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit24(index, emit_ofs + 0));
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
  void DecodeStep22() {
    if (!RefillTo8()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
    const auto op = GetOp25(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 5;
    switch ((op >> 4) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit25(index, emit_ofs + 0));
        break;
      }
    }
  }
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
  void Done() {
    if (buffer_len_ < 10) {
      buffer_ = (buffer_ << (10 - buffer_len_)) |
                ((uint64_t(1) << (10 - buffer_len_)) - 1);
      buffer_len_ = 10;
    }
    const auto index = (buffer_ >> (buffer_len_ - 10)) & 0x3ff;
    const auto op = GetOp1(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 6;
    switch ((op >> 4) & 3) {
      case 1:
      case 2:
      case 3: {
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
