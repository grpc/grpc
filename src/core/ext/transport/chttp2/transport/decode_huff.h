#include <stdlib.h>

#include <cstddef>
#include <cstdint>
static const uint8_t g_table1_0_emit[68] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75,
    0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59,
    0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78, 0x79, 0x7a};
static const uint16_t g_table1_0_inner[72] = {
    0x0005, 0x0045, 0x0085, 0x00c5, 0x0105, 0x0145, 0x0185, 0x01c5, 0x0205,
    0x0245, 0x0286, 0x02c6, 0x0306, 0x0346, 0x0386, 0x03c6, 0x0406, 0x0446,
    0x0486, 0x04c6, 0x0506, 0x0546, 0x0586, 0x05c6, 0x0606, 0x0646, 0x0686,
    0x06c6, 0x0706, 0x0746, 0x0786, 0x07c6, 0x0806, 0x0846, 0x0886, 0x08c6,
    0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87, 0x0ac7, 0x0b07,
    0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7, 0x0d07, 0x0d47,
    0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07, 0x0f47, 0x0f87,
    0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x000f, 0x0017, 0x001f, 0x0027};
static const uint8_t g_table1_0_outer[128] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,
    4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,
    9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18,
    18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27,
    28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 35, 35, 36, 37, 38,
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71};
inline uint64_t GetOp1(size_t i) {
  return g_table1_0_inner[g_table1_0_outer[i]];
}
inline uint64_t GetEmit1(size_t, size_t emit) { return g_table1_0_emit[emit]; }
static const uint8_t g_table2_0_emit[74] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20,
    0x25, 0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3d, 0x41, 0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e,
    0x70, 0x72, 0x75, 0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x59, 0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x26, 0x2a, 0x2c, 0x3b, 0x58, 0x5a};
static const uint16_t g_table2_0_inner[76] = {
    0x0005, 0x0045, 0x0085, 0x00c5, 0x0105, 0x0145, 0x0185, 0x01c5, 0x0205,
    0x0245, 0x0286, 0x02c6, 0x0306, 0x0346, 0x0386, 0x03c6, 0x0406, 0x0446,
    0x0486, 0x04c6, 0x0506, 0x0546, 0x0586, 0x05c6, 0x0606, 0x0646, 0x0686,
    0x06c6, 0x0706, 0x0746, 0x0786, 0x07c6, 0x0806, 0x0846, 0x0886, 0x08c6,
    0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87, 0x0ac7, 0x0b07,
    0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7, 0x0d07, 0x0d47,
    0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07, 0x0f47, 0x0f87,
    0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x1108, 0x1148, 0x1188, 0x11c8,
    0x1208, 0x1248, 0x0018, 0x0028};
static const uint8_t g_table2_0_outer[256] = {
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
inline uint64_t GetOp2(size_t i) {
  return g_table2_0_inner[g_table2_0_outer[i]];
}
inline uint64_t GetEmit2(size_t, size_t emit) { return g_table2_0_emit[emit]; }
static const uint8_t g_table3_0_emit[4] = {0x21, 0x22, 0x28, 0x29};
static const uint8_t g_table3_0_inner[4] = {0x02, 0x06, 0x0a, 0x0e};
static const uint8_t g_table3_0_outer[4] = {0, 1, 2, 3};
inline uint64_t GetOp3(size_t i) {
  return g_table3_0_inner[g_table3_0_outer[i]];
}
inline uint64_t GetEmit3(size_t, size_t emit) { return g_table3_0_emit[emit]; }
static const uint8_t g_table4_0_emit[17] = {0x3f, 0x27, 0x2b, 0x7c, 0x23, 0x3e,
                                            0x00, 0x24, 0x40, 0x5b, 0x5d, 0x7e,
                                            0x5e, 0x7d, 0x3c, 0x60, 0x7b};
static const uint16_t g_table4_0_inner[19] = {
    0x0002, 0x0043, 0x0083, 0x00c3, 0x0104, 0x0144, 0x0185,
    0x01c5, 0x0205, 0x0245, 0x0285, 0x02c5, 0x0306, 0x0346,
    0x0387, 0x03c7, 0x0407, 0x0018, 0x0028};
static const uint8_t g_table4_0_outer[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  8,
    8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10,
    10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13,
    13, 14, 14, 15, 15, 16, 16, 17, 18};
inline uint64_t GetOp4(size_t i) {
  return g_table4_0_inner[g_table4_0_outer[i]];
}
inline uint64_t GetEmit4(size_t, size_t emit) { return g_table4_0_emit[emit]; }
static const uint8_t g_table5_0_emit[15] = {0x5c, 0xc3, 0xd0, 0x80, 0x82,
                                            0x83, 0xa2, 0xb8, 0xc2, 0xe0,
                                            0xe2, 0x99, 0xa1, 0xa7, 0xac};
static const uint8_t g_table5_0_inner[15] = {0x03, 0x0b, 0x13, 0x1c, 0x24,
                                             0x2c, 0x34, 0x3c, 0x44, 0x4c,
                                             0x54, 0x5d, 0x65, 0x6d, 0x75};
static const uint8_t g_table5_0_outer[32] = {
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,  2,  3,  3,  4,  4,
    5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14};
inline uint64_t GetOp5(size_t i) {
  return g_table5_0_inner[g_table5_0_outer[i]];
}
inline uint64_t GetEmit5(size_t, size_t emit) { return g_table5_0_emit[emit]; }
static const uint8_t g_table6_0_emit[1] = {0xb0};
static const uint16_t g_table6_0_inner[1] = {0x0005};
static const uint8_t g_table6_0_outer[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#define g_table6_1_emit g_table6_0_emit
#define g_table6_1_inner g_table6_0_inner
#define g_table6_1_outer g_table6_0_outer
static const uint8_t g_table6_2_emit[1] = {0xb1};
#define g_table6_2_inner g_table6_0_inner
#define g_table6_2_outer g_table6_0_outer
#define g_table6_3_emit g_table6_2_emit
#define g_table6_3_inner g_table6_0_inner
#define g_table6_3_outer g_table6_0_outer
static const uint8_t g_table6_4_emit[1] = {0xb3};
#define g_table6_4_inner g_table6_0_inner
#define g_table6_4_outer g_table6_0_outer
#define g_table6_5_emit g_table6_4_emit
#define g_table6_5_inner g_table6_0_inner
#define g_table6_5_outer g_table6_0_outer
static const uint8_t g_table6_6_emit[1] = {0xd1};
#define g_table6_6_inner g_table6_0_inner
#define g_table6_6_outer g_table6_0_outer
#define g_table6_7_emit g_table6_6_emit
#define g_table6_7_inner g_table6_0_inner
#define g_table6_7_outer g_table6_0_outer
static const uint8_t g_table6_8_emit[1] = {0xd8};
#define g_table6_8_inner g_table6_0_inner
#define g_table6_8_outer g_table6_0_outer
#define g_table6_9_emit g_table6_8_emit
#define g_table6_9_inner g_table6_0_inner
#define g_table6_9_outer g_table6_0_outer
static const uint8_t g_table6_10_emit[1] = {0xd9};
#define g_table6_10_inner g_table6_0_inner
#define g_table6_10_outer g_table6_0_outer
#define g_table6_11_emit g_table6_10_emit
#define g_table6_11_inner g_table6_0_inner
#define g_table6_11_outer g_table6_0_outer
static const uint8_t g_table6_12_emit[1] = {0xe3};
#define g_table6_12_inner g_table6_0_inner
#define g_table6_12_outer g_table6_0_outer
#define g_table6_13_emit g_table6_12_emit
#define g_table6_13_inner g_table6_0_inner
#define g_table6_13_outer g_table6_0_outer
static const uint8_t g_table6_14_emit[1] = {0xe5};
#define g_table6_14_inner g_table6_0_inner
#define g_table6_14_outer g_table6_0_outer
#define g_table6_15_emit g_table6_14_emit
#define g_table6_15_inner g_table6_0_inner
#define g_table6_15_outer g_table6_0_outer
static const uint8_t g_table6_16_emit[1] = {0xe6};
#define g_table6_16_inner g_table6_0_inner
#define g_table6_16_outer g_table6_0_outer
#define g_table6_17_emit g_table6_16_emit
#define g_table6_17_inner g_table6_0_inner
#define g_table6_17_outer g_table6_0_outer
static const uint8_t g_table6_18_emit[1] = {0x81};
static const uint16_t g_table6_18_inner[1] = {0x0006};
#define g_table6_18_outer g_table6_0_outer
static const uint8_t g_table6_19_emit[1] = {0x84};
#define g_table6_19_inner g_table6_18_inner
#define g_table6_19_outer g_table6_0_outer
static const uint8_t g_table6_20_emit[1] = {0x85};
#define g_table6_20_inner g_table6_18_inner
#define g_table6_20_outer g_table6_0_outer
static const uint8_t g_table6_21_emit[1] = {0x86};
#define g_table6_21_inner g_table6_18_inner
#define g_table6_21_outer g_table6_0_outer
static const uint8_t g_table6_22_emit[1] = {0x88};
#define g_table6_22_inner g_table6_18_inner
#define g_table6_22_outer g_table6_0_outer
static const uint8_t g_table6_23_emit[1] = {0x92};
#define g_table6_23_inner g_table6_18_inner
#define g_table6_23_outer g_table6_0_outer
static const uint8_t g_table6_24_emit[1] = {0x9a};
#define g_table6_24_inner g_table6_18_inner
#define g_table6_24_outer g_table6_0_outer
static const uint8_t g_table6_25_emit[1] = {0x9c};
#define g_table6_25_inner g_table6_18_inner
#define g_table6_25_outer g_table6_0_outer
static const uint8_t g_table6_26_emit[1] = {0xa0};
#define g_table6_26_inner g_table6_18_inner
#define g_table6_26_outer g_table6_0_outer
static const uint8_t g_table6_27_emit[1] = {0xa3};
#define g_table6_27_inner g_table6_18_inner
#define g_table6_27_outer g_table6_0_outer
static const uint8_t g_table6_28_emit[1] = {0xa4};
#define g_table6_28_inner g_table6_18_inner
#define g_table6_28_outer g_table6_0_outer
static const uint8_t g_table6_29_emit[1] = {0xa9};
#define g_table6_29_inner g_table6_18_inner
#define g_table6_29_outer g_table6_0_outer
static const uint8_t g_table6_30_emit[1] = {0xaa};
#define g_table6_30_inner g_table6_18_inner
#define g_table6_30_outer g_table6_0_outer
static const uint8_t g_table6_31_emit[1] = {0xad};
#define g_table6_31_inner g_table6_18_inner
#define g_table6_31_outer g_table6_0_outer
static const uint8_t g_table6_32_emit[1] = {0xb2};
#define g_table6_32_inner g_table6_18_inner
#define g_table6_32_outer g_table6_0_outer
static const uint8_t g_table6_33_emit[1] = {0xb5};
#define g_table6_33_inner g_table6_18_inner
#define g_table6_33_outer g_table6_0_outer
static const uint8_t g_table6_34_emit[1] = {0xb9};
#define g_table6_34_inner g_table6_18_inner
#define g_table6_34_outer g_table6_0_outer
static const uint8_t g_table6_35_emit[1] = {0xba};
#define g_table6_35_inner g_table6_18_inner
#define g_table6_35_outer g_table6_0_outer
static const uint8_t g_table6_36_emit[1] = {0xbb};
#define g_table6_36_inner g_table6_18_inner
#define g_table6_36_outer g_table6_0_outer
static const uint8_t g_table6_37_emit[1] = {0xbd};
#define g_table6_37_inner g_table6_18_inner
#define g_table6_37_outer g_table6_0_outer
static const uint8_t g_table6_38_emit[1] = {0xbe};
#define g_table6_38_inner g_table6_18_inner
#define g_table6_38_outer g_table6_0_outer
static const uint8_t g_table6_39_emit[1] = {0xc4};
#define g_table6_39_inner g_table6_18_inner
#define g_table6_39_outer g_table6_0_outer
static const uint8_t g_table6_40_emit[1] = {0xc6};
#define g_table6_40_inner g_table6_18_inner
#define g_table6_40_outer g_table6_0_outer
static const uint8_t g_table6_41_emit[1] = {0xe4};
#define g_table6_41_inner g_table6_18_inner
#define g_table6_41_outer g_table6_0_outer
static const uint8_t g_table6_42_emit[1] = {0xe8};
#define g_table6_42_inner g_table6_18_inner
#define g_table6_42_outer g_table6_0_outer
static const uint8_t g_table6_43_emit[1] = {0xe9};
#define g_table6_43_inner g_table6_18_inner
#define g_table6_43_outer g_table6_0_outer
static const uint8_t g_table6_44_emit[2] = {0x01, 0x87};
static const uint16_t g_table6_44_inner[2] = {0x0007, 0x0027};
static const uint8_t g_table6_44_outer[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t g_table6_45_emit[2] = {0x89, 0x8a};
#define g_table6_45_inner g_table6_44_inner
#define g_table6_45_outer g_table6_44_outer
static const uint8_t g_table6_46_emit[2] = {0x8b, 0x8c};
#define g_table6_46_inner g_table6_44_inner
#define g_table6_46_outer g_table6_44_outer
static const uint8_t g_table6_47_emit[2] = {0x8d, 0x8f};
#define g_table6_47_inner g_table6_44_inner
#define g_table6_47_outer g_table6_44_outer
static const uint8_t g_table6_48_emit[2] = {0x93, 0x95};
#define g_table6_48_inner g_table6_44_inner
#define g_table6_48_outer g_table6_44_outer
static const uint8_t g_table6_49_emit[2] = {0x96, 0x97};
#define g_table6_49_inner g_table6_44_inner
#define g_table6_49_outer g_table6_44_outer
static const uint8_t g_table6_50_emit[2] = {0x98, 0x9b};
#define g_table6_50_inner g_table6_44_inner
#define g_table6_50_outer g_table6_44_outer
static const uint8_t g_table6_51_emit[2] = {0x9d, 0x9e};
#define g_table6_51_inner g_table6_44_inner
#define g_table6_51_outer g_table6_44_outer
static const uint8_t g_table6_52_emit[2] = {0xa5, 0xa6};
#define g_table6_52_inner g_table6_44_inner
#define g_table6_52_outer g_table6_44_outer
static const uint8_t g_table6_53_emit[2] = {0xa8, 0xae};
#define g_table6_53_inner g_table6_44_inner
#define g_table6_53_outer g_table6_44_outer
static const uint8_t g_table6_54_emit[2] = {0xaf, 0xb4};
#define g_table6_54_inner g_table6_44_inner
#define g_table6_54_outer g_table6_44_outer
static const uint8_t g_table6_55_emit[2] = {0xb6, 0xb7};
#define g_table6_55_inner g_table6_44_inner
#define g_table6_55_outer g_table6_44_outer
static const uint8_t g_table6_56_emit[2] = {0xbc, 0xbf};
#define g_table6_56_inner g_table6_44_inner
#define g_table6_56_outer g_table6_44_outer
static const uint8_t g_table6_57_emit[2] = {0xc5, 0xe7};
#define g_table6_57_inner g_table6_44_inner
#define g_table6_57_outer g_table6_44_outer
static const uint8_t g_table6_58_emit[3] = {0xef, 0x09, 0x8e};
static const uint16_t g_table6_58_inner[3] = {0x0007, 0x0028, 0x0048};
static const uint8_t g_table6_58_outer[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
static const uint8_t g_table6_59_emit[4] = {0x90, 0x91, 0x94, 0x9f};
static const uint16_t g_table6_59_inner[4] = {0x0008, 0x0028, 0x0048, 0x0068};
static const uint8_t g_table6_59_outer[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
static const uint8_t g_table6_60_emit[4] = {0xab, 0xce, 0xd7, 0xe1};
#define g_table6_60_inner g_table6_59_inner
#define g_table6_60_outer g_table6_59_outer
static const uint8_t g_table6_61_emit[6] = {0xec, 0xed, 0xc7, 0xcf, 0xea, 0xeb};
static const uint16_t g_table6_61_inner[6] = {0x0008, 0x0028, 0x0049,
                                              0x0069, 0x0089, 0x00a9};
static const uint8_t g_table6_61_outer[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
static const uint8_t g_table6_62_emit[17] = {0xc0, 0xc1, 0xc8, 0xc9, 0xca, 0xcd,
                                             0xd2, 0xd5, 0xda, 0xdb, 0xee, 0xf0,
                                             0xf2, 0xf3, 0xff, 0xcb, 0xcc};
static const uint16_t g_table6_62_inner[17] = {
    0x000a, 0x002a, 0x004a, 0x006a, 0x008a, 0x00aa, 0x00ca, 0x00ea, 0x010a,
    0x012a, 0x014a, 0x016a, 0x018a, 0x01aa, 0x01ca, 0x01eb, 0x020b};
static const uint8_t g_table6_62_outer[256] = {
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
    15, 16, 16, 16, 16, 16, 16, 16, 16};
static const uint8_t g_table6_63_emit[49] = {
    0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf, 0xf1, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
    0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16};
static const uint16_t g_table6_63_inner[50] = {
    0x000b, 0x002b, 0x004b, 0x006b, 0x008b, 0x00ab, 0x00cb, 0x00eb, 0x010b,
    0x012b, 0x014b, 0x016b, 0x018b, 0x01ab, 0x01cb, 0x01eb, 0x020b, 0x022c,
    0x024c, 0x026c, 0x028c, 0x02ac, 0x02cc, 0x02ec, 0x030c, 0x032c, 0x034c,
    0x036c, 0x038c, 0x03ac, 0x03cc, 0x03ec, 0x040c, 0x042c, 0x044c, 0x046c,
    0x048c, 0x04ac, 0x04cc, 0x04ec, 0x050c, 0x052c, 0x054c, 0x056c, 0x058c,
    0x05ac, 0x05ce, 0x05ee, 0x060e, 0x001e};
static const uint8_t g_table6_63_outer[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
    2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,
    4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  7,
    7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,
    9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11,
    11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14,
    14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16,
    16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20,
    21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25,
    25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30,
    30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 34, 34, 34, 34, 35,
    35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39,
    40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 44, 44, 44,
    44, 45, 45, 45, 45, 46, 47, 48, 49};
static const uint8_t* const g_table6_emit[] = {
    g_table6_0_emit,  g_table6_1_emit,  g_table6_2_emit,  g_table6_3_emit,
    g_table6_4_emit,  g_table6_5_emit,  g_table6_6_emit,  g_table6_7_emit,
    g_table6_8_emit,  g_table6_9_emit,  g_table6_10_emit, g_table6_11_emit,
    g_table6_12_emit, g_table6_13_emit, g_table6_14_emit, g_table6_15_emit,
    g_table6_16_emit, g_table6_17_emit, g_table6_18_emit, g_table6_19_emit,
    g_table6_20_emit, g_table6_21_emit, g_table6_22_emit, g_table6_23_emit,
    g_table6_24_emit, g_table6_25_emit, g_table6_26_emit, g_table6_27_emit,
    g_table6_28_emit, g_table6_29_emit, g_table6_30_emit, g_table6_31_emit,
    g_table6_32_emit, g_table6_33_emit, g_table6_34_emit, g_table6_35_emit,
    g_table6_36_emit, g_table6_37_emit, g_table6_38_emit, g_table6_39_emit,
    g_table6_40_emit, g_table6_41_emit, g_table6_42_emit, g_table6_43_emit,
    g_table6_44_emit, g_table6_45_emit, g_table6_46_emit, g_table6_47_emit,
    g_table6_48_emit, g_table6_49_emit, g_table6_50_emit, g_table6_51_emit,
    g_table6_52_emit, g_table6_53_emit, g_table6_54_emit, g_table6_55_emit,
    g_table6_56_emit, g_table6_57_emit, g_table6_58_emit, g_table6_59_emit,
    g_table6_60_emit, g_table6_61_emit, g_table6_62_emit, g_table6_63_emit,
};
static const uint16_t* const g_table6_inner[] = {
    g_table6_0_inner,  g_table6_1_inner,  g_table6_2_inner,  g_table6_3_inner,
    g_table6_4_inner,  g_table6_5_inner,  g_table6_6_inner,  g_table6_7_inner,
    g_table6_8_inner,  g_table6_9_inner,  g_table6_10_inner, g_table6_11_inner,
    g_table6_12_inner, g_table6_13_inner, g_table6_14_inner, g_table6_15_inner,
    g_table6_16_inner, g_table6_17_inner, g_table6_18_inner, g_table6_19_inner,
    g_table6_20_inner, g_table6_21_inner, g_table6_22_inner, g_table6_23_inner,
    g_table6_24_inner, g_table6_25_inner, g_table6_26_inner, g_table6_27_inner,
    g_table6_28_inner, g_table6_29_inner, g_table6_30_inner, g_table6_31_inner,
    g_table6_32_inner, g_table6_33_inner, g_table6_34_inner, g_table6_35_inner,
    g_table6_36_inner, g_table6_37_inner, g_table6_38_inner, g_table6_39_inner,
    g_table6_40_inner, g_table6_41_inner, g_table6_42_inner, g_table6_43_inner,
    g_table6_44_inner, g_table6_45_inner, g_table6_46_inner, g_table6_47_inner,
    g_table6_48_inner, g_table6_49_inner, g_table6_50_inner, g_table6_51_inner,
    g_table6_52_inner, g_table6_53_inner, g_table6_54_inner, g_table6_55_inner,
    g_table6_56_inner, g_table6_57_inner, g_table6_58_inner, g_table6_59_inner,
    g_table6_60_inner, g_table6_61_inner, g_table6_62_inner, g_table6_63_inner,
};
static const uint8_t* const g_table6_outer[] = {
    g_table6_0_outer,  g_table6_1_outer,  g_table6_2_outer,  g_table6_3_outer,
    g_table6_4_outer,  g_table6_5_outer,  g_table6_6_outer,  g_table6_7_outer,
    g_table6_8_outer,  g_table6_9_outer,  g_table6_10_outer, g_table6_11_outer,
    g_table6_12_outer, g_table6_13_outer, g_table6_14_outer, g_table6_15_outer,
    g_table6_16_outer, g_table6_17_outer, g_table6_18_outer, g_table6_19_outer,
    g_table6_20_outer, g_table6_21_outer, g_table6_22_outer, g_table6_23_outer,
    g_table6_24_outer, g_table6_25_outer, g_table6_26_outer, g_table6_27_outer,
    g_table6_28_outer, g_table6_29_outer, g_table6_30_outer, g_table6_31_outer,
    g_table6_32_outer, g_table6_33_outer, g_table6_34_outer, g_table6_35_outer,
    g_table6_36_outer, g_table6_37_outer, g_table6_38_outer, g_table6_39_outer,
    g_table6_40_outer, g_table6_41_outer, g_table6_42_outer, g_table6_43_outer,
    g_table6_44_outer, g_table6_45_outer, g_table6_46_outer, g_table6_47_outer,
    g_table6_48_outer, g_table6_49_outer, g_table6_50_outer, g_table6_51_outer,
    g_table6_52_outer, g_table6_53_outer, g_table6_54_outer, g_table6_55_outer,
    g_table6_56_outer, g_table6_57_outer, g_table6_58_outer, g_table6_59_outer,
    g_table6_60_outer, g_table6_61_outer, g_table6_62_outer, g_table6_63_outer,
};
inline uint64_t GetOp6(size_t i) {
  return g_table6_inner[i >> 8][g_table6_outer[i >> 8][i & 0xff]];
}
inline uint64_t GetEmit6(size_t i, size_t emit) {
  return g_table6_emit[i >> 8][emit];
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
      const auto op = GetOp2(index);
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
          sink_(GetEmit2(index, emit_ofs + 0));
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
    const auto op = GetOp3(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit3(index, emit_ofs + 0));
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
    const auto op = GetOp4(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 6;
    switch ((op >> 4) & 3) {
      case 1: {
        DecodeStep2();
        break;
      }
      case 2: {
        DecodeStep3();
        break;
      }
      case 0: {
        sink_(GetEmit4(index, emit_ofs + 0));
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
    const auto op = GetOp5(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit5(index, emit_ofs + 0));
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
    if (!RefillTo14()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 14)) & 0x3fff;
    const auto op = GetOp6(index);
    buffer_len_ -= op & 15;
    const auto emit_ofs = op >> 5;
    switch ((op >> 4) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit6(index, emit_ofs + 0));
        break;
      }
    }
  }
  bool RefillTo14() {
    switch (buffer_len_) {
      case 10:
      case 11:
      case 12:
      case 13:
      case 6:
      case 7:
      case 8:
      case 9: {
        return Read1();
      }
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5: {
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
  void Done() {
    if (buffer_len_ < 7) {
      buffer_ = (buffer_ << (7 - buffer_len_)) |
                ((uint64_t(1) << (7 - buffer_len_)) - 1);
      buffer_len_ = 7;
    }
    const auto index = (buffer_ >> (buffer_len_ - 7)) & 0x7f;
    const auto op = GetOp1(index);
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
