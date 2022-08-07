#include <stdlib.h>

#include <cstddef>
#include <cstdint>
static const uint8_t g_table1_0_emit[36] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75};
static const uint16_t g_table1_0_inner[54] = {
    0x0005, 0x0105, 0x0205, 0x0305, 0x0405, 0x0505, 0x0605, 0x0705, 0x0805,
    0x0905, 0x0a06, 0x0b06, 0x0c06, 0x0d06, 0x0e06, 0x0f06, 0x1006, 0x1106,
    0x1206, 0x1306, 0x1406, 0x1506, 0x1606, 0x1706, 0x1806, 0x1906, 0x1a06,
    0x1b06, 0x1c06, 0x1d06, 0x1e06, 0x1f06, 0x2006, 0x2106, 0x2206, 0x2306,
    0x000e, 0x0016, 0x001e, 0x0026, 0x002e, 0x0036, 0x003e, 0x0046, 0x004e,
    0x0056, 0x005e, 0x0066, 0x006e, 0x0076, 0x007e, 0x0086, 0x008e, 0x0096};
static const uint8_t g_table1_0_outer[64] = {
    0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53};
inline uint64_t GetOp1(size_t i) {
  return g_table1_0_inner[g_table1_0_outer[i]];
}
inline uint64_t GetEmit1(size_t, size_t emit) { return g_table1_0_emit[emit]; }
static const uint8_t g_table2_0_emit[68] = {
    0x30, 0x31, 0x32, 0x61, 0x63, 0x65, 0x69, 0x6f, 0x73, 0x74, 0x20, 0x25,
    0x2d, 0x2e, 0x2f, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3d, 0x41,
    0x5f, 0x62, 0x64, 0x66, 0x67, 0x68, 0x6c, 0x6d, 0x6e, 0x70, 0x72, 0x75,
    0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59,
    0x6a, 0x6b, 0x71, 0x76, 0x77, 0x78, 0x79, 0x7a};
static const uint16_t g_table2_0_inner[72] = {
    0x0005, 0x0045, 0x0085, 0x00c5, 0x0105, 0x0145, 0x0185, 0x01c5, 0x0205,
    0x0245, 0x0286, 0x02c6, 0x0306, 0x0346, 0x0386, 0x03c6, 0x0406, 0x0446,
    0x0486, 0x04c6, 0x0506, 0x0546, 0x0586, 0x05c6, 0x0606, 0x0646, 0x0686,
    0x06c6, 0x0706, 0x0746, 0x0786, 0x07c6, 0x0806, 0x0846, 0x0886, 0x08c6,
    0x0907, 0x0947, 0x0987, 0x09c7, 0x0a07, 0x0a47, 0x0a87, 0x0ac7, 0x0b07,
    0x0b47, 0x0b87, 0x0bc7, 0x0c07, 0x0c47, 0x0c87, 0x0cc7, 0x0d07, 0x0d47,
    0x0d87, 0x0dc7, 0x0e07, 0x0e47, 0x0e87, 0x0ec7, 0x0f07, 0x0f47, 0x0f87,
    0x0fc7, 0x1007, 0x1047, 0x1087, 0x10c7, 0x000f, 0x0017, 0x001f, 0x0027};
static const uint8_t g_table2_0_outer[128] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,
    4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,
    9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18,
    18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27,
    28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 35, 35, 36, 37, 38,
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71};
inline uint64_t GetOp2(size_t i) {
  return g_table2_0_inner[g_table2_0_outer[i]];
}
inline uint64_t GetEmit2(size_t, size_t emit) { return g_table2_0_emit[emit]; }
static const uint8_t g_table3_0_emit[2] = {0x26, 0x2a};
static const uint8_t g_table3_0_inner[2] = {0x01, 0x03};
static const uint8_t g_table3_0_outer[2] = {0, 1};
inline uint64_t GetOp3(size_t i) {
  return g_table3_0_inner[g_table3_0_outer[i]];
}
inline uint64_t GetEmit3(size_t, size_t emit) { return g_table3_0_emit[emit]; }
static const uint8_t g_table4_0_emit[2] = {0x2c, 0x3b};
#define g_table4_0_inner g_table3_0_inner
#define g_table4_0_outer g_table3_0_outer
inline uint64_t GetOp4(size_t i) {
  return g_table4_0_inner[g_table4_0_outer[i]];
}
inline uint64_t GetEmit4(size_t, size_t emit) { return g_table4_0_emit[emit]; }
static const uint8_t g_table5_0_emit[2] = {0x58, 0x5a};
#define g_table5_0_inner g_table3_0_inner
#define g_table5_0_outer g_table3_0_outer
inline uint64_t GetOp5(size_t i) {
  return g_table5_0_inner[g_table5_0_outer[i]];
}
inline uint64_t GetEmit5(size_t, size_t emit) { return g_table5_0_emit[emit]; }
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
inline uint64_t GetOp6(size_t i) {
  return g_table6_0_inner[g_table6_0_outer[i]];
}
inline uint64_t GetEmit6(size_t, size_t emit) { return g_table6_0_emit[emit]; }
static const uint8_t g_table7_0_emit[2] = {0x3c, 0x60};
#define g_table7_0_inner g_table3_0_inner
#define g_table7_0_outer g_table3_0_outer
inline uint64_t GetOp7(size_t i) {
  return g_table7_0_inner[g_table7_0_outer[i]];
}
inline uint64_t GetEmit7(size_t, size_t emit) { return g_table7_0_emit[emit]; }
static const uint8_t g_table8_0_emit[1] = {0x7b};
static const uint16_t g_table8_0_inner[1] = {0x0001};
static const uint8_t g_table8_0_outer[256] = {
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
#define g_table8_1_emit g_table8_0_emit
#define g_table8_1_inner g_table8_0_inner
#define g_table8_1_outer g_table8_0_outer
#define g_table8_2_emit g_table8_0_emit
#define g_table8_2_inner g_table8_0_inner
#define g_table8_2_outer g_table8_0_outer
#define g_table8_3_emit g_table8_0_emit
#define g_table8_3_inner g_table8_0_inner
#define g_table8_3_outer g_table8_0_outer
#define g_table8_4_emit g_table8_0_emit
#define g_table8_4_inner g_table8_0_inner
#define g_table8_4_outer g_table8_0_outer
#define g_table8_5_emit g_table8_0_emit
#define g_table8_5_inner g_table8_0_inner
#define g_table8_5_outer g_table8_0_outer
#define g_table8_6_emit g_table8_0_emit
#define g_table8_6_inner g_table8_0_inner
#define g_table8_6_outer g_table8_0_outer
#define g_table8_7_emit g_table8_0_emit
#define g_table8_7_inner g_table8_0_inner
#define g_table8_7_outer g_table8_0_outer
#define g_table8_8_emit g_table8_0_emit
#define g_table8_8_inner g_table8_0_inner
#define g_table8_8_outer g_table8_0_outer
#define g_table8_9_emit g_table8_0_emit
#define g_table8_9_inner g_table8_0_inner
#define g_table8_9_outer g_table8_0_outer
#define g_table8_10_emit g_table8_0_emit
#define g_table8_10_inner g_table8_0_inner
#define g_table8_10_outer g_table8_0_outer
#define g_table8_11_emit g_table8_0_emit
#define g_table8_11_inner g_table8_0_inner
#define g_table8_11_outer g_table8_0_outer
#define g_table8_12_emit g_table8_0_emit
#define g_table8_12_inner g_table8_0_inner
#define g_table8_12_outer g_table8_0_outer
#define g_table8_13_emit g_table8_0_emit
#define g_table8_13_inner g_table8_0_inner
#define g_table8_13_outer g_table8_0_outer
#define g_table8_14_emit g_table8_0_emit
#define g_table8_14_inner g_table8_0_inner
#define g_table8_14_outer g_table8_0_outer
#define g_table8_15_emit g_table8_0_emit
#define g_table8_15_inner g_table8_0_inner
#define g_table8_15_outer g_table8_0_outer
#define g_table8_16_emit g_table8_0_emit
#define g_table8_16_inner g_table8_0_inner
#define g_table8_16_outer g_table8_0_outer
#define g_table8_17_emit g_table8_0_emit
#define g_table8_17_inner g_table8_0_inner
#define g_table8_17_outer g_table8_0_outer
#define g_table8_18_emit g_table8_0_emit
#define g_table8_18_inner g_table8_0_inner
#define g_table8_18_outer g_table8_0_outer
#define g_table8_19_emit g_table8_0_emit
#define g_table8_19_inner g_table8_0_inner
#define g_table8_19_outer g_table8_0_outer
#define g_table8_20_emit g_table8_0_emit
#define g_table8_20_inner g_table8_0_inner
#define g_table8_20_outer g_table8_0_outer
#define g_table8_21_emit g_table8_0_emit
#define g_table8_21_inner g_table8_0_inner
#define g_table8_21_outer g_table8_0_outer
#define g_table8_22_emit g_table8_0_emit
#define g_table8_22_inner g_table8_0_inner
#define g_table8_22_outer g_table8_0_outer
#define g_table8_23_emit g_table8_0_emit
#define g_table8_23_inner g_table8_0_inner
#define g_table8_23_outer g_table8_0_outer
#define g_table8_24_emit g_table8_0_emit
#define g_table8_24_inner g_table8_0_inner
#define g_table8_24_outer g_table8_0_outer
#define g_table8_25_emit g_table8_0_emit
#define g_table8_25_inner g_table8_0_inner
#define g_table8_25_outer g_table8_0_outer
#define g_table8_26_emit g_table8_0_emit
#define g_table8_26_inner g_table8_0_inner
#define g_table8_26_outer g_table8_0_outer
#define g_table8_27_emit g_table8_0_emit
#define g_table8_27_inner g_table8_0_inner
#define g_table8_27_outer g_table8_0_outer
#define g_table8_28_emit g_table8_0_emit
#define g_table8_28_inner g_table8_0_inner
#define g_table8_28_outer g_table8_0_outer
#define g_table8_29_emit g_table8_0_emit
#define g_table8_29_inner g_table8_0_inner
#define g_table8_29_outer g_table8_0_outer
#define g_table8_30_emit g_table8_0_emit
#define g_table8_30_inner g_table8_0_inner
#define g_table8_30_outer g_table8_0_outer
#define g_table8_31_emit g_table8_0_emit
#define g_table8_31_inner g_table8_0_inner
#define g_table8_31_outer g_table8_0_outer
#define g_table8_32_emit g_table8_0_emit
#define g_table8_32_inner g_table8_0_inner
#define g_table8_32_outer g_table8_0_outer
#define g_table8_33_emit g_table8_0_emit
#define g_table8_33_inner g_table8_0_inner
#define g_table8_33_outer g_table8_0_outer
#define g_table8_34_emit g_table8_0_emit
#define g_table8_34_inner g_table8_0_inner
#define g_table8_34_outer g_table8_0_outer
#define g_table8_35_emit g_table8_0_emit
#define g_table8_35_inner g_table8_0_inner
#define g_table8_35_outer g_table8_0_outer
#define g_table8_36_emit g_table8_0_emit
#define g_table8_36_inner g_table8_0_inner
#define g_table8_36_outer g_table8_0_outer
#define g_table8_37_emit g_table8_0_emit
#define g_table8_37_inner g_table8_0_inner
#define g_table8_37_outer g_table8_0_outer
#define g_table8_38_emit g_table8_0_emit
#define g_table8_38_inner g_table8_0_inner
#define g_table8_38_outer g_table8_0_outer
#define g_table8_39_emit g_table8_0_emit
#define g_table8_39_inner g_table8_0_inner
#define g_table8_39_outer g_table8_0_outer
#define g_table8_40_emit g_table8_0_emit
#define g_table8_40_inner g_table8_0_inner
#define g_table8_40_outer g_table8_0_outer
#define g_table8_41_emit g_table8_0_emit
#define g_table8_41_inner g_table8_0_inner
#define g_table8_41_outer g_table8_0_outer
#define g_table8_42_emit g_table8_0_emit
#define g_table8_42_inner g_table8_0_inner
#define g_table8_42_outer g_table8_0_outer
#define g_table8_43_emit g_table8_0_emit
#define g_table8_43_inner g_table8_0_inner
#define g_table8_43_outer g_table8_0_outer
#define g_table8_44_emit g_table8_0_emit
#define g_table8_44_inner g_table8_0_inner
#define g_table8_44_outer g_table8_0_outer
#define g_table8_45_emit g_table8_0_emit
#define g_table8_45_inner g_table8_0_inner
#define g_table8_45_outer g_table8_0_outer
#define g_table8_46_emit g_table8_0_emit
#define g_table8_46_inner g_table8_0_inner
#define g_table8_46_outer g_table8_0_outer
#define g_table8_47_emit g_table8_0_emit
#define g_table8_47_inner g_table8_0_inner
#define g_table8_47_outer g_table8_0_outer
#define g_table8_48_emit g_table8_0_emit
#define g_table8_48_inner g_table8_0_inner
#define g_table8_48_outer g_table8_0_outer
#define g_table8_49_emit g_table8_0_emit
#define g_table8_49_inner g_table8_0_inner
#define g_table8_49_outer g_table8_0_outer
#define g_table8_50_emit g_table8_0_emit
#define g_table8_50_inner g_table8_0_inner
#define g_table8_50_outer g_table8_0_outer
#define g_table8_51_emit g_table8_0_emit
#define g_table8_51_inner g_table8_0_inner
#define g_table8_51_outer g_table8_0_outer
#define g_table8_52_emit g_table8_0_emit
#define g_table8_52_inner g_table8_0_inner
#define g_table8_52_outer g_table8_0_outer
#define g_table8_53_emit g_table8_0_emit
#define g_table8_53_inner g_table8_0_inner
#define g_table8_53_outer g_table8_0_outer
#define g_table8_54_emit g_table8_0_emit
#define g_table8_54_inner g_table8_0_inner
#define g_table8_54_outer g_table8_0_outer
#define g_table8_55_emit g_table8_0_emit
#define g_table8_55_inner g_table8_0_inner
#define g_table8_55_outer g_table8_0_outer
#define g_table8_56_emit g_table8_0_emit
#define g_table8_56_inner g_table8_0_inner
#define g_table8_56_outer g_table8_0_outer
#define g_table8_57_emit g_table8_0_emit
#define g_table8_57_inner g_table8_0_inner
#define g_table8_57_outer g_table8_0_outer
#define g_table8_58_emit g_table8_0_emit
#define g_table8_58_inner g_table8_0_inner
#define g_table8_58_outer g_table8_0_outer
#define g_table8_59_emit g_table8_0_emit
#define g_table8_59_inner g_table8_0_inner
#define g_table8_59_outer g_table8_0_outer
#define g_table8_60_emit g_table8_0_emit
#define g_table8_60_inner g_table8_0_inner
#define g_table8_60_outer g_table8_0_outer
#define g_table8_61_emit g_table8_0_emit
#define g_table8_61_inner g_table8_0_inner
#define g_table8_61_outer g_table8_0_outer
#define g_table8_62_emit g_table8_0_emit
#define g_table8_62_inner g_table8_0_inner
#define g_table8_62_outer g_table8_0_outer
#define g_table8_63_emit g_table8_0_emit
#define g_table8_63_inner g_table8_0_inner
#define g_table8_63_outer g_table8_0_outer
#define g_table8_64_emit g_table8_0_emit
#define g_table8_64_inner g_table8_0_inner
#define g_table8_64_outer g_table8_0_outer
#define g_table8_65_emit g_table8_0_emit
#define g_table8_65_inner g_table8_0_inner
#define g_table8_65_outer g_table8_0_outer
#define g_table8_66_emit g_table8_0_emit
#define g_table8_66_inner g_table8_0_inner
#define g_table8_66_outer g_table8_0_outer
#define g_table8_67_emit g_table8_0_emit
#define g_table8_67_inner g_table8_0_inner
#define g_table8_67_outer g_table8_0_outer
#define g_table8_68_emit g_table8_0_emit
#define g_table8_68_inner g_table8_0_inner
#define g_table8_68_outer g_table8_0_outer
#define g_table8_69_emit g_table8_0_emit
#define g_table8_69_inner g_table8_0_inner
#define g_table8_69_outer g_table8_0_outer
#define g_table8_70_emit g_table8_0_emit
#define g_table8_70_inner g_table8_0_inner
#define g_table8_70_outer g_table8_0_outer
#define g_table8_71_emit g_table8_0_emit
#define g_table8_71_inner g_table8_0_inner
#define g_table8_71_outer g_table8_0_outer
#define g_table8_72_emit g_table8_0_emit
#define g_table8_72_inner g_table8_0_inner
#define g_table8_72_outer g_table8_0_outer
#define g_table8_73_emit g_table8_0_emit
#define g_table8_73_inner g_table8_0_inner
#define g_table8_73_outer g_table8_0_outer
#define g_table8_74_emit g_table8_0_emit
#define g_table8_74_inner g_table8_0_inner
#define g_table8_74_outer g_table8_0_outer
#define g_table8_75_emit g_table8_0_emit
#define g_table8_75_inner g_table8_0_inner
#define g_table8_75_outer g_table8_0_outer
#define g_table8_76_emit g_table8_0_emit
#define g_table8_76_inner g_table8_0_inner
#define g_table8_76_outer g_table8_0_outer
#define g_table8_77_emit g_table8_0_emit
#define g_table8_77_inner g_table8_0_inner
#define g_table8_77_outer g_table8_0_outer
#define g_table8_78_emit g_table8_0_emit
#define g_table8_78_inner g_table8_0_inner
#define g_table8_78_outer g_table8_0_outer
#define g_table8_79_emit g_table8_0_emit
#define g_table8_79_inner g_table8_0_inner
#define g_table8_79_outer g_table8_0_outer
#define g_table8_80_emit g_table8_0_emit
#define g_table8_80_inner g_table8_0_inner
#define g_table8_80_outer g_table8_0_outer
#define g_table8_81_emit g_table8_0_emit
#define g_table8_81_inner g_table8_0_inner
#define g_table8_81_outer g_table8_0_outer
#define g_table8_82_emit g_table8_0_emit
#define g_table8_82_inner g_table8_0_inner
#define g_table8_82_outer g_table8_0_outer
#define g_table8_83_emit g_table8_0_emit
#define g_table8_83_inner g_table8_0_inner
#define g_table8_83_outer g_table8_0_outer
#define g_table8_84_emit g_table8_0_emit
#define g_table8_84_inner g_table8_0_inner
#define g_table8_84_outer g_table8_0_outer
#define g_table8_85_emit g_table8_0_emit
#define g_table8_85_inner g_table8_0_inner
#define g_table8_85_outer g_table8_0_outer
#define g_table8_86_emit g_table8_0_emit
#define g_table8_86_inner g_table8_0_inner
#define g_table8_86_outer g_table8_0_outer
#define g_table8_87_emit g_table8_0_emit
#define g_table8_87_inner g_table8_0_inner
#define g_table8_87_outer g_table8_0_outer
#define g_table8_88_emit g_table8_0_emit
#define g_table8_88_inner g_table8_0_inner
#define g_table8_88_outer g_table8_0_outer
#define g_table8_89_emit g_table8_0_emit
#define g_table8_89_inner g_table8_0_inner
#define g_table8_89_outer g_table8_0_outer
#define g_table8_90_emit g_table8_0_emit
#define g_table8_90_inner g_table8_0_inner
#define g_table8_90_outer g_table8_0_outer
#define g_table8_91_emit g_table8_0_emit
#define g_table8_91_inner g_table8_0_inner
#define g_table8_91_outer g_table8_0_outer
#define g_table8_92_emit g_table8_0_emit
#define g_table8_92_inner g_table8_0_inner
#define g_table8_92_outer g_table8_0_outer
#define g_table8_93_emit g_table8_0_emit
#define g_table8_93_inner g_table8_0_inner
#define g_table8_93_outer g_table8_0_outer
#define g_table8_94_emit g_table8_0_emit
#define g_table8_94_inner g_table8_0_inner
#define g_table8_94_outer g_table8_0_outer
#define g_table8_95_emit g_table8_0_emit
#define g_table8_95_inner g_table8_0_inner
#define g_table8_95_outer g_table8_0_outer
#define g_table8_96_emit g_table8_0_emit
#define g_table8_96_inner g_table8_0_inner
#define g_table8_96_outer g_table8_0_outer
#define g_table8_97_emit g_table8_0_emit
#define g_table8_97_inner g_table8_0_inner
#define g_table8_97_outer g_table8_0_outer
#define g_table8_98_emit g_table8_0_emit
#define g_table8_98_inner g_table8_0_inner
#define g_table8_98_outer g_table8_0_outer
#define g_table8_99_emit g_table8_0_emit
#define g_table8_99_inner g_table8_0_inner
#define g_table8_99_outer g_table8_0_outer
#define g_table8_100_emit g_table8_0_emit
#define g_table8_100_inner g_table8_0_inner
#define g_table8_100_outer g_table8_0_outer
#define g_table8_101_emit g_table8_0_emit
#define g_table8_101_inner g_table8_0_inner
#define g_table8_101_outer g_table8_0_outer
#define g_table8_102_emit g_table8_0_emit
#define g_table8_102_inner g_table8_0_inner
#define g_table8_102_outer g_table8_0_outer
#define g_table8_103_emit g_table8_0_emit
#define g_table8_103_inner g_table8_0_inner
#define g_table8_103_outer g_table8_0_outer
#define g_table8_104_emit g_table8_0_emit
#define g_table8_104_inner g_table8_0_inner
#define g_table8_104_outer g_table8_0_outer
#define g_table8_105_emit g_table8_0_emit
#define g_table8_105_inner g_table8_0_inner
#define g_table8_105_outer g_table8_0_outer
#define g_table8_106_emit g_table8_0_emit
#define g_table8_106_inner g_table8_0_inner
#define g_table8_106_outer g_table8_0_outer
#define g_table8_107_emit g_table8_0_emit
#define g_table8_107_inner g_table8_0_inner
#define g_table8_107_outer g_table8_0_outer
#define g_table8_108_emit g_table8_0_emit
#define g_table8_108_inner g_table8_0_inner
#define g_table8_108_outer g_table8_0_outer
#define g_table8_109_emit g_table8_0_emit
#define g_table8_109_inner g_table8_0_inner
#define g_table8_109_outer g_table8_0_outer
#define g_table8_110_emit g_table8_0_emit
#define g_table8_110_inner g_table8_0_inner
#define g_table8_110_outer g_table8_0_outer
#define g_table8_111_emit g_table8_0_emit
#define g_table8_111_inner g_table8_0_inner
#define g_table8_111_outer g_table8_0_outer
#define g_table8_112_emit g_table8_0_emit
#define g_table8_112_inner g_table8_0_inner
#define g_table8_112_outer g_table8_0_outer
#define g_table8_113_emit g_table8_0_emit
#define g_table8_113_inner g_table8_0_inner
#define g_table8_113_outer g_table8_0_outer
#define g_table8_114_emit g_table8_0_emit
#define g_table8_114_inner g_table8_0_inner
#define g_table8_114_outer g_table8_0_outer
#define g_table8_115_emit g_table8_0_emit
#define g_table8_115_inner g_table8_0_inner
#define g_table8_115_outer g_table8_0_outer
#define g_table8_116_emit g_table8_0_emit
#define g_table8_116_inner g_table8_0_inner
#define g_table8_116_outer g_table8_0_outer
#define g_table8_117_emit g_table8_0_emit
#define g_table8_117_inner g_table8_0_inner
#define g_table8_117_outer g_table8_0_outer
#define g_table8_118_emit g_table8_0_emit
#define g_table8_118_inner g_table8_0_inner
#define g_table8_118_outer g_table8_0_outer
#define g_table8_119_emit g_table8_0_emit
#define g_table8_119_inner g_table8_0_inner
#define g_table8_119_outer g_table8_0_outer
#define g_table8_120_emit g_table8_0_emit
#define g_table8_120_inner g_table8_0_inner
#define g_table8_120_outer g_table8_0_outer
#define g_table8_121_emit g_table8_0_emit
#define g_table8_121_inner g_table8_0_inner
#define g_table8_121_outer g_table8_0_outer
#define g_table8_122_emit g_table8_0_emit
#define g_table8_122_inner g_table8_0_inner
#define g_table8_122_outer g_table8_0_outer
#define g_table8_123_emit g_table8_0_emit
#define g_table8_123_inner g_table8_0_inner
#define g_table8_123_outer g_table8_0_outer
#define g_table8_124_emit g_table8_0_emit
#define g_table8_124_inner g_table8_0_inner
#define g_table8_124_outer g_table8_0_outer
#define g_table8_125_emit g_table8_0_emit
#define g_table8_125_inner g_table8_0_inner
#define g_table8_125_outer g_table8_0_outer
#define g_table8_126_emit g_table8_0_emit
#define g_table8_126_inner g_table8_0_inner
#define g_table8_126_outer g_table8_0_outer
#define g_table8_127_emit g_table8_0_emit
#define g_table8_127_inner g_table8_0_inner
#define g_table8_127_outer g_table8_0_outer
static const uint8_t g_table8_128_emit[1] = {0x5c};
static const uint16_t g_table8_128_inner[1] = {0x0005};
#define g_table8_128_outer g_table8_0_outer
#define g_table8_129_emit g_table8_128_emit
#define g_table8_129_inner g_table8_128_inner
#define g_table8_129_outer g_table8_0_outer
#define g_table8_130_emit g_table8_128_emit
#define g_table8_130_inner g_table8_128_inner
#define g_table8_130_outer g_table8_0_outer
#define g_table8_131_emit g_table8_128_emit
#define g_table8_131_inner g_table8_128_inner
#define g_table8_131_outer g_table8_0_outer
#define g_table8_132_emit g_table8_128_emit
#define g_table8_132_inner g_table8_128_inner
#define g_table8_132_outer g_table8_0_outer
#define g_table8_133_emit g_table8_128_emit
#define g_table8_133_inner g_table8_128_inner
#define g_table8_133_outer g_table8_0_outer
#define g_table8_134_emit g_table8_128_emit
#define g_table8_134_inner g_table8_128_inner
#define g_table8_134_outer g_table8_0_outer
#define g_table8_135_emit g_table8_128_emit
#define g_table8_135_inner g_table8_128_inner
#define g_table8_135_outer g_table8_0_outer
static const uint8_t g_table8_136_emit[1] = {0xc3};
#define g_table8_136_inner g_table8_128_inner
#define g_table8_136_outer g_table8_0_outer
#define g_table8_137_emit g_table8_136_emit
#define g_table8_137_inner g_table8_128_inner
#define g_table8_137_outer g_table8_0_outer
#define g_table8_138_emit g_table8_136_emit
#define g_table8_138_inner g_table8_128_inner
#define g_table8_138_outer g_table8_0_outer
#define g_table8_139_emit g_table8_136_emit
#define g_table8_139_inner g_table8_128_inner
#define g_table8_139_outer g_table8_0_outer
#define g_table8_140_emit g_table8_136_emit
#define g_table8_140_inner g_table8_128_inner
#define g_table8_140_outer g_table8_0_outer
#define g_table8_141_emit g_table8_136_emit
#define g_table8_141_inner g_table8_128_inner
#define g_table8_141_outer g_table8_0_outer
#define g_table8_142_emit g_table8_136_emit
#define g_table8_142_inner g_table8_128_inner
#define g_table8_142_outer g_table8_0_outer
#define g_table8_143_emit g_table8_136_emit
#define g_table8_143_inner g_table8_128_inner
#define g_table8_143_outer g_table8_0_outer
static const uint8_t g_table8_144_emit[1] = {0xd0};
#define g_table8_144_inner g_table8_128_inner
#define g_table8_144_outer g_table8_0_outer
#define g_table8_145_emit g_table8_144_emit
#define g_table8_145_inner g_table8_128_inner
#define g_table8_145_outer g_table8_0_outer
#define g_table8_146_emit g_table8_144_emit
#define g_table8_146_inner g_table8_128_inner
#define g_table8_146_outer g_table8_0_outer
#define g_table8_147_emit g_table8_144_emit
#define g_table8_147_inner g_table8_128_inner
#define g_table8_147_outer g_table8_0_outer
#define g_table8_148_emit g_table8_144_emit
#define g_table8_148_inner g_table8_128_inner
#define g_table8_148_outer g_table8_0_outer
#define g_table8_149_emit g_table8_144_emit
#define g_table8_149_inner g_table8_128_inner
#define g_table8_149_outer g_table8_0_outer
#define g_table8_150_emit g_table8_144_emit
#define g_table8_150_inner g_table8_128_inner
#define g_table8_150_outer g_table8_0_outer
#define g_table8_151_emit g_table8_144_emit
#define g_table8_151_inner g_table8_128_inner
#define g_table8_151_outer g_table8_0_outer
static const uint8_t g_table8_152_emit[1] = {0x80};
static const uint16_t g_table8_152_inner[1] = {0x0006};
#define g_table8_152_outer g_table8_0_outer
#define g_table8_153_emit g_table8_152_emit
#define g_table8_153_inner g_table8_152_inner
#define g_table8_153_outer g_table8_0_outer
#define g_table8_154_emit g_table8_152_emit
#define g_table8_154_inner g_table8_152_inner
#define g_table8_154_outer g_table8_0_outer
#define g_table8_155_emit g_table8_152_emit
#define g_table8_155_inner g_table8_152_inner
#define g_table8_155_outer g_table8_0_outer
static const uint8_t g_table8_156_emit[1] = {0x82};
#define g_table8_156_inner g_table8_152_inner
#define g_table8_156_outer g_table8_0_outer
#define g_table8_157_emit g_table8_156_emit
#define g_table8_157_inner g_table8_152_inner
#define g_table8_157_outer g_table8_0_outer
#define g_table8_158_emit g_table8_156_emit
#define g_table8_158_inner g_table8_152_inner
#define g_table8_158_outer g_table8_0_outer
#define g_table8_159_emit g_table8_156_emit
#define g_table8_159_inner g_table8_152_inner
#define g_table8_159_outer g_table8_0_outer
static const uint8_t g_table8_160_emit[1] = {0x83};
#define g_table8_160_inner g_table8_152_inner
#define g_table8_160_outer g_table8_0_outer
#define g_table8_161_emit g_table8_160_emit
#define g_table8_161_inner g_table8_152_inner
#define g_table8_161_outer g_table8_0_outer
#define g_table8_162_emit g_table8_160_emit
#define g_table8_162_inner g_table8_152_inner
#define g_table8_162_outer g_table8_0_outer
#define g_table8_163_emit g_table8_160_emit
#define g_table8_163_inner g_table8_152_inner
#define g_table8_163_outer g_table8_0_outer
static const uint8_t g_table8_164_emit[1] = {0xa2};
#define g_table8_164_inner g_table8_152_inner
#define g_table8_164_outer g_table8_0_outer
#define g_table8_165_emit g_table8_164_emit
#define g_table8_165_inner g_table8_152_inner
#define g_table8_165_outer g_table8_0_outer
#define g_table8_166_emit g_table8_164_emit
#define g_table8_166_inner g_table8_152_inner
#define g_table8_166_outer g_table8_0_outer
#define g_table8_167_emit g_table8_164_emit
#define g_table8_167_inner g_table8_152_inner
#define g_table8_167_outer g_table8_0_outer
static const uint8_t g_table8_168_emit[1] = {0xb8};
#define g_table8_168_inner g_table8_152_inner
#define g_table8_168_outer g_table8_0_outer
#define g_table8_169_emit g_table8_168_emit
#define g_table8_169_inner g_table8_152_inner
#define g_table8_169_outer g_table8_0_outer
#define g_table8_170_emit g_table8_168_emit
#define g_table8_170_inner g_table8_152_inner
#define g_table8_170_outer g_table8_0_outer
#define g_table8_171_emit g_table8_168_emit
#define g_table8_171_inner g_table8_152_inner
#define g_table8_171_outer g_table8_0_outer
static const uint8_t g_table8_172_emit[1] = {0xc2};
#define g_table8_172_inner g_table8_152_inner
#define g_table8_172_outer g_table8_0_outer
#define g_table8_173_emit g_table8_172_emit
#define g_table8_173_inner g_table8_152_inner
#define g_table8_173_outer g_table8_0_outer
#define g_table8_174_emit g_table8_172_emit
#define g_table8_174_inner g_table8_152_inner
#define g_table8_174_outer g_table8_0_outer
#define g_table8_175_emit g_table8_172_emit
#define g_table8_175_inner g_table8_152_inner
#define g_table8_175_outer g_table8_0_outer
static const uint8_t g_table8_176_emit[1] = {0xe0};
#define g_table8_176_inner g_table8_152_inner
#define g_table8_176_outer g_table8_0_outer
#define g_table8_177_emit g_table8_176_emit
#define g_table8_177_inner g_table8_152_inner
#define g_table8_177_outer g_table8_0_outer
#define g_table8_178_emit g_table8_176_emit
#define g_table8_178_inner g_table8_152_inner
#define g_table8_178_outer g_table8_0_outer
#define g_table8_179_emit g_table8_176_emit
#define g_table8_179_inner g_table8_152_inner
#define g_table8_179_outer g_table8_0_outer
static const uint8_t g_table8_180_emit[1] = {0xe2};
#define g_table8_180_inner g_table8_152_inner
#define g_table8_180_outer g_table8_0_outer
#define g_table8_181_emit g_table8_180_emit
#define g_table8_181_inner g_table8_152_inner
#define g_table8_181_outer g_table8_0_outer
#define g_table8_182_emit g_table8_180_emit
#define g_table8_182_inner g_table8_152_inner
#define g_table8_182_outer g_table8_0_outer
#define g_table8_183_emit g_table8_180_emit
#define g_table8_183_inner g_table8_152_inner
#define g_table8_183_outer g_table8_0_outer
static const uint8_t g_table8_184_emit[1] = {0x99};
static const uint16_t g_table8_184_inner[1] = {0x0007};
#define g_table8_184_outer g_table8_0_outer
#define g_table8_185_emit g_table8_184_emit
#define g_table8_185_inner g_table8_184_inner
#define g_table8_185_outer g_table8_0_outer
static const uint8_t g_table8_186_emit[1] = {0xa1};
#define g_table8_186_inner g_table8_184_inner
#define g_table8_186_outer g_table8_0_outer
#define g_table8_187_emit g_table8_186_emit
#define g_table8_187_inner g_table8_184_inner
#define g_table8_187_outer g_table8_0_outer
static const uint8_t g_table8_188_emit[1] = {0xa7};
#define g_table8_188_inner g_table8_184_inner
#define g_table8_188_outer g_table8_0_outer
#define g_table8_189_emit g_table8_188_emit
#define g_table8_189_inner g_table8_184_inner
#define g_table8_189_outer g_table8_0_outer
static const uint8_t g_table8_190_emit[1] = {0xac};
#define g_table8_190_inner g_table8_184_inner
#define g_table8_190_outer g_table8_0_outer
#define g_table8_191_emit g_table8_190_emit
#define g_table8_191_inner g_table8_184_inner
#define g_table8_191_outer g_table8_0_outer
static const uint8_t g_table8_192_emit[1] = {0xb0};
#define g_table8_192_inner g_table8_184_inner
#define g_table8_192_outer g_table8_0_outer
#define g_table8_193_emit g_table8_192_emit
#define g_table8_193_inner g_table8_184_inner
#define g_table8_193_outer g_table8_0_outer
static const uint8_t g_table8_194_emit[1] = {0xb1};
#define g_table8_194_inner g_table8_184_inner
#define g_table8_194_outer g_table8_0_outer
#define g_table8_195_emit g_table8_194_emit
#define g_table8_195_inner g_table8_184_inner
#define g_table8_195_outer g_table8_0_outer
static const uint8_t g_table8_196_emit[1] = {0xb3};
#define g_table8_196_inner g_table8_184_inner
#define g_table8_196_outer g_table8_0_outer
#define g_table8_197_emit g_table8_196_emit
#define g_table8_197_inner g_table8_184_inner
#define g_table8_197_outer g_table8_0_outer
static const uint8_t g_table8_198_emit[1] = {0xd1};
#define g_table8_198_inner g_table8_184_inner
#define g_table8_198_outer g_table8_0_outer
#define g_table8_199_emit g_table8_198_emit
#define g_table8_199_inner g_table8_184_inner
#define g_table8_199_outer g_table8_0_outer
static const uint8_t g_table8_200_emit[1] = {0xd8};
#define g_table8_200_inner g_table8_184_inner
#define g_table8_200_outer g_table8_0_outer
#define g_table8_201_emit g_table8_200_emit
#define g_table8_201_inner g_table8_184_inner
#define g_table8_201_outer g_table8_0_outer
static const uint8_t g_table8_202_emit[1] = {0xd9};
#define g_table8_202_inner g_table8_184_inner
#define g_table8_202_outer g_table8_0_outer
#define g_table8_203_emit g_table8_202_emit
#define g_table8_203_inner g_table8_184_inner
#define g_table8_203_outer g_table8_0_outer
static const uint8_t g_table8_204_emit[1] = {0xe3};
#define g_table8_204_inner g_table8_184_inner
#define g_table8_204_outer g_table8_0_outer
#define g_table8_205_emit g_table8_204_emit
#define g_table8_205_inner g_table8_184_inner
#define g_table8_205_outer g_table8_0_outer
static const uint8_t g_table8_206_emit[1] = {0xe5};
#define g_table8_206_inner g_table8_184_inner
#define g_table8_206_outer g_table8_0_outer
#define g_table8_207_emit g_table8_206_emit
#define g_table8_207_inner g_table8_184_inner
#define g_table8_207_outer g_table8_0_outer
static const uint8_t g_table8_208_emit[1] = {0xe6};
#define g_table8_208_inner g_table8_184_inner
#define g_table8_208_outer g_table8_0_outer
#define g_table8_209_emit g_table8_208_emit
#define g_table8_209_inner g_table8_184_inner
#define g_table8_209_outer g_table8_0_outer
static const uint8_t g_table8_210_emit[1] = {0x81};
static const uint16_t g_table8_210_inner[1] = {0x0008};
#define g_table8_210_outer g_table8_0_outer
static const uint8_t g_table8_211_emit[1] = {0x84};
#define g_table8_211_inner g_table8_210_inner
#define g_table8_211_outer g_table8_0_outer
static const uint8_t g_table8_212_emit[1] = {0x85};
#define g_table8_212_inner g_table8_210_inner
#define g_table8_212_outer g_table8_0_outer
static const uint8_t g_table8_213_emit[1] = {0x86};
#define g_table8_213_inner g_table8_210_inner
#define g_table8_213_outer g_table8_0_outer
static const uint8_t g_table8_214_emit[1] = {0x88};
#define g_table8_214_inner g_table8_210_inner
#define g_table8_214_outer g_table8_0_outer
static const uint8_t g_table8_215_emit[1] = {0x92};
#define g_table8_215_inner g_table8_210_inner
#define g_table8_215_outer g_table8_0_outer
static const uint8_t g_table8_216_emit[1] = {0x9a};
#define g_table8_216_inner g_table8_210_inner
#define g_table8_216_outer g_table8_0_outer
static const uint8_t g_table8_217_emit[1] = {0x9c};
#define g_table8_217_inner g_table8_210_inner
#define g_table8_217_outer g_table8_0_outer
static const uint8_t g_table8_218_emit[1] = {0xa0};
#define g_table8_218_inner g_table8_210_inner
#define g_table8_218_outer g_table8_0_outer
static const uint8_t g_table8_219_emit[1] = {0xa3};
#define g_table8_219_inner g_table8_210_inner
#define g_table8_219_outer g_table8_0_outer
static const uint8_t g_table8_220_emit[1] = {0xa4};
#define g_table8_220_inner g_table8_210_inner
#define g_table8_220_outer g_table8_0_outer
static const uint8_t g_table8_221_emit[1] = {0xa9};
#define g_table8_221_inner g_table8_210_inner
#define g_table8_221_outer g_table8_0_outer
static const uint8_t g_table8_222_emit[1] = {0xaa};
#define g_table8_222_inner g_table8_210_inner
#define g_table8_222_outer g_table8_0_outer
static const uint8_t g_table8_223_emit[1] = {0xad};
#define g_table8_223_inner g_table8_210_inner
#define g_table8_223_outer g_table8_0_outer
static const uint8_t g_table8_224_emit[1] = {0xb2};
#define g_table8_224_inner g_table8_210_inner
#define g_table8_224_outer g_table8_0_outer
static const uint8_t g_table8_225_emit[1] = {0xb5};
#define g_table8_225_inner g_table8_210_inner
#define g_table8_225_outer g_table8_0_outer
static const uint8_t g_table8_226_emit[1] = {0xb9};
#define g_table8_226_inner g_table8_210_inner
#define g_table8_226_outer g_table8_0_outer
static const uint8_t g_table8_227_emit[1] = {0xba};
#define g_table8_227_inner g_table8_210_inner
#define g_table8_227_outer g_table8_0_outer
static const uint8_t g_table8_228_emit[1] = {0xbb};
#define g_table8_228_inner g_table8_210_inner
#define g_table8_228_outer g_table8_0_outer
static const uint8_t g_table8_229_emit[1] = {0xbd};
#define g_table8_229_inner g_table8_210_inner
#define g_table8_229_outer g_table8_0_outer
static const uint8_t g_table8_230_emit[1] = {0xbe};
#define g_table8_230_inner g_table8_210_inner
#define g_table8_230_outer g_table8_0_outer
static const uint8_t g_table8_231_emit[1] = {0xc4};
#define g_table8_231_inner g_table8_210_inner
#define g_table8_231_outer g_table8_0_outer
static const uint8_t g_table8_232_emit[1] = {0xc6};
#define g_table8_232_inner g_table8_210_inner
#define g_table8_232_outer g_table8_0_outer
static const uint8_t g_table8_233_emit[1] = {0xe4};
#define g_table8_233_inner g_table8_210_inner
#define g_table8_233_outer g_table8_0_outer
static const uint8_t g_table8_234_emit[1] = {0xe8};
#define g_table8_234_inner g_table8_210_inner
#define g_table8_234_outer g_table8_0_outer
static const uint8_t g_table8_235_emit[1] = {0xe9};
#define g_table8_235_inner g_table8_210_inner
#define g_table8_235_outer g_table8_0_outer
static const uint8_t g_table8_236_emit[2] = {0x01, 0x87};
static const uint16_t g_table8_236_inner[2] = {0x0009, 0x0049};
static const uint8_t g_table8_236_outer[256] = {
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
static const uint8_t g_table8_237_emit[2] = {0x89, 0x8a};
#define g_table8_237_inner g_table8_236_inner
#define g_table8_237_outer g_table8_236_outer
static const uint8_t g_table8_238_emit[2] = {0x8b, 0x8c};
#define g_table8_238_inner g_table8_236_inner
#define g_table8_238_outer g_table8_236_outer
static const uint8_t g_table8_239_emit[2] = {0x8d, 0x8f};
#define g_table8_239_inner g_table8_236_inner
#define g_table8_239_outer g_table8_236_outer
static const uint8_t g_table8_240_emit[2] = {0x93, 0x95};
#define g_table8_240_inner g_table8_236_inner
#define g_table8_240_outer g_table8_236_outer
static const uint8_t g_table8_241_emit[2] = {0x96, 0x97};
#define g_table8_241_inner g_table8_236_inner
#define g_table8_241_outer g_table8_236_outer
static const uint8_t g_table8_242_emit[2] = {0x98, 0x9b};
#define g_table8_242_inner g_table8_236_inner
#define g_table8_242_outer g_table8_236_outer
static const uint8_t g_table8_243_emit[2] = {0x9d, 0x9e};
#define g_table8_243_inner g_table8_236_inner
#define g_table8_243_outer g_table8_236_outer
static const uint8_t g_table8_244_emit[2] = {0xa5, 0xa6};
#define g_table8_244_inner g_table8_236_inner
#define g_table8_244_outer g_table8_236_outer
static const uint8_t g_table8_245_emit[2] = {0xa8, 0xae};
#define g_table8_245_inner g_table8_236_inner
#define g_table8_245_outer g_table8_236_outer
static const uint8_t g_table8_246_emit[2] = {0xaf, 0xb4};
#define g_table8_246_inner g_table8_236_inner
#define g_table8_246_outer g_table8_236_outer
static const uint8_t g_table8_247_emit[2] = {0xb6, 0xb7};
#define g_table8_247_inner g_table8_236_inner
#define g_table8_247_outer g_table8_236_outer
static const uint8_t g_table8_248_emit[2] = {0xbc, 0xbf};
#define g_table8_248_inner g_table8_236_inner
#define g_table8_248_outer g_table8_236_outer
static const uint8_t g_table8_249_emit[2] = {0xc5, 0xe7};
#define g_table8_249_inner g_table8_236_inner
#define g_table8_249_outer g_table8_236_outer
static const uint8_t g_table8_250_emit[3] = {0xef, 0x09, 0x8e};
static const uint16_t g_table8_250_inner[3] = {0x0009, 0x004a, 0x008a};
static const uint8_t g_table8_250_outer[256] = {
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
static const uint8_t g_table8_251_emit[4] = {0x90, 0x91, 0x94, 0x9f};
static const uint16_t g_table8_251_inner[4] = {0x000a, 0x004a, 0x008a, 0x00ca};
static const uint8_t g_table8_251_outer[256] = {
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
static const uint8_t g_table8_252_emit[4] = {0xab, 0xce, 0xd7, 0xe1};
#define g_table8_252_inner g_table8_251_inner
#define g_table8_252_outer g_table8_251_outer
static const uint8_t g_table8_253_emit[6] = {0xec, 0xed, 0xc7,
                                             0xcf, 0xea, 0xeb};
static const uint16_t g_table8_253_inner[6] = {0x000a, 0x004a, 0x008b,
                                               0x00cb, 0x010b, 0x014b};
static const uint8_t g_table8_253_outer[256] = {
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
static const uint8_t g_table8_254_emit[17] = {
    0xc0, 0xc1, 0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda,
    0xdb, 0xee, 0xf0, 0xf2, 0xf3, 0xff, 0xcb, 0xcc};
static const uint16_t g_table8_254_inner[17] = {
    0x000c, 0x004c, 0x008c, 0x00cc, 0x010c, 0x014c, 0x018c, 0x01cc, 0x020c,
    0x024c, 0x028c, 0x02cc, 0x030c, 0x034c, 0x038c, 0x03cd, 0x040d};
static const uint8_t g_table8_254_outer[256] = {
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
static const uint8_t g_table8_255_emit[49] = {
    0xd3, 0xd4, 0xd6, 0xdd, 0xde, 0xdf, 0xf1, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x0b, 0x0c, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
    0x1d, 0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16};
static const uint16_t g_table8_255_inner[50] = {
    0x000d, 0x004d, 0x008d, 0x00cd, 0x010d, 0x014d, 0x018d, 0x01cd, 0x020d,
    0x024d, 0x028d, 0x02cd, 0x030d, 0x034d, 0x038d, 0x03cd, 0x040d, 0x044e,
    0x048e, 0x04ce, 0x050e, 0x054e, 0x058e, 0x05ce, 0x060e, 0x064e, 0x068e,
    0x06ce, 0x070e, 0x074e, 0x078e, 0x07ce, 0x080e, 0x084e, 0x088e, 0x08ce,
    0x090e, 0x094e, 0x098e, 0x09ce, 0x0a0e, 0x0a4e, 0x0a8e, 0x0ace, 0x0b0e,
    0x0b4e, 0x0b90, 0x0bd0, 0x0c10, 0x0030};
static const uint8_t g_table8_255_outer[256] = {
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
static const uint8_t* const g_table8_emit[] = {
    g_table8_0_emit,   g_table8_1_emit,   g_table8_2_emit,   g_table8_3_emit,
    g_table8_4_emit,   g_table8_5_emit,   g_table8_6_emit,   g_table8_7_emit,
    g_table8_8_emit,   g_table8_9_emit,   g_table8_10_emit,  g_table8_11_emit,
    g_table8_12_emit,  g_table8_13_emit,  g_table8_14_emit,  g_table8_15_emit,
    g_table8_16_emit,  g_table8_17_emit,  g_table8_18_emit,  g_table8_19_emit,
    g_table8_20_emit,  g_table8_21_emit,  g_table8_22_emit,  g_table8_23_emit,
    g_table8_24_emit,  g_table8_25_emit,  g_table8_26_emit,  g_table8_27_emit,
    g_table8_28_emit,  g_table8_29_emit,  g_table8_30_emit,  g_table8_31_emit,
    g_table8_32_emit,  g_table8_33_emit,  g_table8_34_emit,  g_table8_35_emit,
    g_table8_36_emit,  g_table8_37_emit,  g_table8_38_emit,  g_table8_39_emit,
    g_table8_40_emit,  g_table8_41_emit,  g_table8_42_emit,  g_table8_43_emit,
    g_table8_44_emit,  g_table8_45_emit,  g_table8_46_emit,  g_table8_47_emit,
    g_table8_48_emit,  g_table8_49_emit,  g_table8_50_emit,  g_table8_51_emit,
    g_table8_52_emit,  g_table8_53_emit,  g_table8_54_emit,  g_table8_55_emit,
    g_table8_56_emit,  g_table8_57_emit,  g_table8_58_emit,  g_table8_59_emit,
    g_table8_60_emit,  g_table8_61_emit,  g_table8_62_emit,  g_table8_63_emit,
    g_table8_64_emit,  g_table8_65_emit,  g_table8_66_emit,  g_table8_67_emit,
    g_table8_68_emit,  g_table8_69_emit,  g_table8_70_emit,  g_table8_71_emit,
    g_table8_72_emit,  g_table8_73_emit,  g_table8_74_emit,  g_table8_75_emit,
    g_table8_76_emit,  g_table8_77_emit,  g_table8_78_emit,  g_table8_79_emit,
    g_table8_80_emit,  g_table8_81_emit,  g_table8_82_emit,  g_table8_83_emit,
    g_table8_84_emit,  g_table8_85_emit,  g_table8_86_emit,  g_table8_87_emit,
    g_table8_88_emit,  g_table8_89_emit,  g_table8_90_emit,  g_table8_91_emit,
    g_table8_92_emit,  g_table8_93_emit,  g_table8_94_emit,  g_table8_95_emit,
    g_table8_96_emit,  g_table8_97_emit,  g_table8_98_emit,  g_table8_99_emit,
    g_table8_100_emit, g_table8_101_emit, g_table8_102_emit, g_table8_103_emit,
    g_table8_104_emit, g_table8_105_emit, g_table8_106_emit, g_table8_107_emit,
    g_table8_108_emit, g_table8_109_emit, g_table8_110_emit, g_table8_111_emit,
    g_table8_112_emit, g_table8_113_emit, g_table8_114_emit, g_table8_115_emit,
    g_table8_116_emit, g_table8_117_emit, g_table8_118_emit, g_table8_119_emit,
    g_table8_120_emit, g_table8_121_emit, g_table8_122_emit, g_table8_123_emit,
    g_table8_124_emit, g_table8_125_emit, g_table8_126_emit, g_table8_127_emit,
    g_table8_128_emit, g_table8_129_emit, g_table8_130_emit, g_table8_131_emit,
    g_table8_132_emit, g_table8_133_emit, g_table8_134_emit, g_table8_135_emit,
    g_table8_136_emit, g_table8_137_emit, g_table8_138_emit, g_table8_139_emit,
    g_table8_140_emit, g_table8_141_emit, g_table8_142_emit, g_table8_143_emit,
    g_table8_144_emit, g_table8_145_emit, g_table8_146_emit, g_table8_147_emit,
    g_table8_148_emit, g_table8_149_emit, g_table8_150_emit, g_table8_151_emit,
    g_table8_152_emit, g_table8_153_emit, g_table8_154_emit, g_table8_155_emit,
    g_table8_156_emit, g_table8_157_emit, g_table8_158_emit, g_table8_159_emit,
    g_table8_160_emit, g_table8_161_emit, g_table8_162_emit, g_table8_163_emit,
    g_table8_164_emit, g_table8_165_emit, g_table8_166_emit, g_table8_167_emit,
    g_table8_168_emit, g_table8_169_emit, g_table8_170_emit, g_table8_171_emit,
    g_table8_172_emit, g_table8_173_emit, g_table8_174_emit, g_table8_175_emit,
    g_table8_176_emit, g_table8_177_emit, g_table8_178_emit, g_table8_179_emit,
    g_table8_180_emit, g_table8_181_emit, g_table8_182_emit, g_table8_183_emit,
    g_table8_184_emit, g_table8_185_emit, g_table8_186_emit, g_table8_187_emit,
    g_table8_188_emit, g_table8_189_emit, g_table8_190_emit, g_table8_191_emit,
    g_table8_192_emit, g_table8_193_emit, g_table8_194_emit, g_table8_195_emit,
    g_table8_196_emit, g_table8_197_emit, g_table8_198_emit, g_table8_199_emit,
    g_table8_200_emit, g_table8_201_emit, g_table8_202_emit, g_table8_203_emit,
    g_table8_204_emit, g_table8_205_emit, g_table8_206_emit, g_table8_207_emit,
    g_table8_208_emit, g_table8_209_emit, g_table8_210_emit, g_table8_211_emit,
    g_table8_212_emit, g_table8_213_emit, g_table8_214_emit, g_table8_215_emit,
    g_table8_216_emit, g_table8_217_emit, g_table8_218_emit, g_table8_219_emit,
    g_table8_220_emit, g_table8_221_emit, g_table8_222_emit, g_table8_223_emit,
    g_table8_224_emit, g_table8_225_emit, g_table8_226_emit, g_table8_227_emit,
    g_table8_228_emit, g_table8_229_emit, g_table8_230_emit, g_table8_231_emit,
    g_table8_232_emit, g_table8_233_emit, g_table8_234_emit, g_table8_235_emit,
    g_table8_236_emit, g_table8_237_emit, g_table8_238_emit, g_table8_239_emit,
    g_table8_240_emit, g_table8_241_emit, g_table8_242_emit, g_table8_243_emit,
    g_table8_244_emit, g_table8_245_emit, g_table8_246_emit, g_table8_247_emit,
    g_table8_248_emit, g_table8_249_emit, g_table8_250_emit, g_table8_251_emit,
    g_table8_252_emit, g_table8_253_emit, g_table8_254_emit, g_table8_255_emit,
};
static const uint16_t* const g_table8_inner[] = {
    g_table8_0_inner,   g_table8_1_inner,   g_table8_2_inner,
    g_table8_3_inner,   g_table8_4_inner,   g_table8_5_inner,
    g_table8_6_inner,   g_table8_7_inner,   g_table8_8_inner,
    g_table8_9_inner,   g_table8_10_inner,  g_table8_11_inner,
    g_table8_12_inner,  g_table8_13_inner,  g_table8_14_inner,
    g_table8_15_inner,  g_table8_16_inner,  g_table8_17_inner,
    g_table8_18_inner,  g_table8_19_inner,  g_table8_20_inner,
    g_table8_21_inner,  g_table8_22_inner,  g_table8_23_inner,
    g_table8_24_inner,  g_table8_25_inner,  g_table8_26_inner,
    g_table8_27_inner,  g_table8_28_inner,  g_table8_29_inner,
    g_table8_30_inner,  g_table8_31_inner,  g_table8_32_inner,
    g_table8_33_inner,  g_table8_34_inner,  g_table8_35_inner,
    g_table8_36_inner,  g_table8_37_inner,  g_table8_38_inner,
    g_table8_39_inner,  g_table8_40_inner,  g_table8_41_inner,
    g_table8_42_inner,  g_table8_43_inner,  g_table8_44_inner,
    g_table8_45_inner,  g_table8_46_inner,  g_table8_47_inner,
    g_table8_48_inner,  g_table8_49_inner,  g_table8_50_inner,
    g_table8_51_inner,  g_table8_52_inner,  g_table8_53_inner,
    g_table8_54_inner,  g_table8_55_inner,  g_table8_56_inner,
    g_table8_57_inner,  g_table8_58_inner,  g_table8_59_inner,
    g_table8_60_inner,  g_table8_61_inner,  g_table8_62_inner,
    g_table8_63_inner,  g_table8_64_inner,  g_table8_65_inner,
    g_table8_66_inner,  g_table8_67_inner,  g_table8_68_inner,
    g_table8_69_inner,  g_table8_70_inner,  g_table8_71_inner,
    g_table8_72_inner,  g_table8_73_inner,  g_table8_74_inner,
    g_table8_75_inner,  g_table8_76_inner,  g_table8_77_inner,
    g_table8_78_inner,  g_table8_79_inner,  g_table8_80_inner,
    g_table8_81_inner,  g_table8_82_inner,  g_table8_83_inner,
    g_table8_84_inner,  g_table8_85_inner,  g_table8_86_inner,
    g_table8_87_inner,  g_table8_88_inner,  g_table8_89_inner,
    g_table8_90_inner,  g_table8_91_inner,  g_table8_92_inner,
    g_table8_93_inner,  g_table8_94_inner,  g_table8_95_inner,
    g_table8_96_inner,  g_table8_97_inner,  g_table8_98_inner,
    g_table8_99_inner,  g_table8_100_inner, g_table8_101_inner,
    g_table8_102_inner, g_table8_103_inner, g_table8_104_inner,
    g_table8_105_inner, g_table8_106_inner, g_table8_107_inner,
    g_table8_108_inner, g_table8_109_inner, g_table8_110_inner,
    g_table8_111_inner, g_table8_112_inner, g_table8_113_inner,
    g_table8_114_inner, g_table8_115_inner, g_table8_116_inner,
    g_table8_117_inner, g_table8_118_inner, g_table8_119_inner,
    g_table8_120_inner, g_table8_121_inner, g_table8_122_inner,
    g_table8_123_inner, g_table8_124_inner, g_table8_125_inner,
    g_table8_126_inner, g_table8_127_inner, g_table8_128_inner,
    g_table8_129_inner, g_table8_130_inner, g_table8_131_inner,
    g_table8_132_inner, g_table8_133_inner, g_table8_134_inner,
    g_table8_135_inner, g_table8_136_inner, g_table8_137_inner,
    g_table8_138_inner, g_table8_139_inner, g_table8_140_inner,
    g_table8_141_inner, g_table8_142_inner, g_table8_143_inner,
    g_table8_144_inner, g_table8_145_inner, g_table8_146_inner,
    g_table8_147_inner, g_table8_148_inner, g_table8_149_inner,
    g_table8_150_inner, g_table8_151_inner, g_table8_152_inner,
    g_table8_153_inner, g_table8_154_inner, g_table8_155_inner,
    g_table8_156_inner, g_table8_157_inner, g_table8_158_inner,
    g_table8_159_inner, g_table8_160_inner, g_table8_161_inner,
    g_table8_162_inner, g_table8_163_inner, g_table8_164_inner,
    g_table8_165_inner, g_table8_166_inner, g_table8_167_inner,
    g_table8_168_inner, g_table8_169_inner, g_table8_170_inner,
    g_table8_171_inner, g_table8_172_inner, g_table8_173_inner,
    g_table8_174_inner, g_table8_175_inner, g_table8_176_inner,
    g_table8_177_inner, g_table8_178_inner, g_table8_179_inner,
    g_table8_180_inner, g_table8_181_inner, g_table8_182_inner,
    g_table8_183_inner, g_table8_184_inner, g_table8_185_inner,
    g_table8_186_inner, g_table8_187_inner, g_table8_188_inner,
    g_table8_189_inner, g_table8_190_inner, g_table8_191_inner,
    g_table8_192_inner, g_table8_193_inner, g_table8_194_inner,
    g_table8_195_inner, g_table8_196_inner, g_table8_197_inner,
    g_table8_198_inner, g_table8_199_inner, g_table8_200_inner,
    g_table8_201_inner, g_table8_202_inner, g_table8_203_inner,
    g_table8_204_inner, g_table8_205_inner, g_table8_206_inner,
    g_table8_207_inner, g_table8_208_inner, g_table8_209_inner,
    g_table8_210_inner, g_table8_211_inner, g_table8_212_inner,
    g_table8_213_inner, g_table8_214_inner, g_table8_215_inner,
    g_table8_216_inner, g_table8_217_inner, g_table8_218_inner,
    g_table8_219_inner, g_table8_220_inner, g_table8_221_inner,
    g_table8_222_inner, g_table8_223_inner, g_table8_224_inner,
    g_table8_225_inner, g_table8_226_inner, g_table8_227_inner,
    g_table8_228_inner, g_table8_229_inner, g_table8_230_inner,
    g_table8_231_inner, g_table8_232_inner, g_table8_233_inner,
    g_table8_234_inner, g_table8_235_inner, g_table8_236_inner,
    g_table8_237_inner, g_table8_238_inner, g_table8_239_inner,
    g_table8_240_inner, g_table8_241_inner, g_table8_242_inner,
    g_table8_243_inner, g_table8_244_inner, g_table8_245_inner,
    g_table8_246_inner, g_table8_247_inner, g_table8_248_inner,
    g_table8_249_inner, g_table8_250_inner, g_table8_251_inner,
    g_table8_252_inner, g_table8_253_inner, g_table8_254_inner,
    g_table8_255_inner,
};
static const uint8_t* const g_table8_outer[] = {
    g_table8_0_outer,   g_table8_1_outer,   g_table8_2_outer,
    g_table8_3_outer,   g_table8_4_outer,   g_table8_5_outer,
    g_table8_6_outer,   g_table8_7_outer,   g_table8_8_outer,
    g_table8_9_outer,   g_table8_10_outer,  g_table8_11_outer,
    g_table8_12_outer,  g_table8_13_outer,  g_table8_14_outer,
    g_table8_15_outer,  g_table8_16_outer,  g_table8_17_outer,
    g_table8_18_outer,  g_table8_19_outer,  g_table8_20_outer,
    g_table8_21_outer,  g_table8_22_outer,  g_table8_23_outer,
    g_table8_24_outer,  g_table8_25_outer,  g_table8_26_outer,
    g_table8_27_outer,  g_table8_28_outer,  g_table8_29_outer,
    g_table8_30_outer,  g_table8_31_outer,  g_table8_32_outer,
    g_table8_33_outer,  g_table8_34_outer,  g_table8_35_outer,
    g_table8_36_outer,  g_table8_37_outer,  g_table8_38_outer,
    g_table8_39_outer,  g_table8_40_outer,  g_table8_41_outer,
    g_table8_42_outer,  g_table8_43_outer,  g_table8_44_outer,
    g_table8_45_outer,  g_table8_46_outer,  g_table8_47_outer,
    g_table8_48_outer,  g_table8_49_outer,  g_table8_50_outer,
    g_table8_51_outer,  g_table8_52_outer,  g_table8_53_outer,
    g_table8_54_outer,  g_table8_55_outer,  g_table8_56_outer,
    g_table8_57_outer,  g_table8_58_outer,  g_table8_59_outer,
    g_table8_60_outer,  g_table8_61_outer,  g_table8_62_outer,
    g_table8_63_outer,  g_table8_64_outer,  g_table8_65_outer,
    g_table8_66_outer,  g_table8_67_outer,  g_table8_68_outer,
    g_table8_69_outer,  g_table8_70_outer,  g_table8_71_outer,
    g_table8_72_outer,  g_table8_73_outer,  g_table8_74_outer,
    g_table8_75_outer,  g_table8_76_outer,  g_table8_77_outer,
    g_table8_78_outer,  g_table8_79_outer,  g_table8_80_outer,
    g_table8_81_outer,  g_table8_82_outer,  g_table8_83_outer,
    g_table8_84_outer,  g_table8_85_outer,  g_table8_86_outer,
    g_table8_87_outer,  g_table8_88_outer,  g_table8_89_outer,
    g_table8_90_outer,  g_table8_91_outer,  g_table8_92_outer,
    g_table8_93_outer,  g_table8_94_outer,  g_table8_95_outer,
    g_table8_96_outer,  g_table8_97_outer,  g_table8_98_outer,
    g_table8_99_outer,  g_table8_100_outer, g_table8_101_outer,
    g_table8_102_outer, g_table8_103_outer, g_table8_104_outer,
    g_table8_105_outer, g_table8_106_outer, g_table8_107_outer,
    g_table8_108_outer, g_table8_109_outer, g_table8_110_outer,
    g_table8_111_outer, g_table8_112_outer, g_table8_113_outer,
    g_table8_114_outer, g_table8_115_outer, g_table8_116_outer,
    g_table8_117_outer, g_table8_118_outer, g_table8_119_outer,
    g_table8_120_outer, g_table8_121_outer, g_table8_122_outer,
    g_table8_123_outer, g_table8_124_outer, g_table8_125_outer,
    g_table8_126_outer, g_table8_127_outer, g_table8_128_outer,
    g_table8_129_outer, g_table8_130_outer, g_table8_131_outer,
    g_table8_132_outer, g_table8_133_outer, g_table8_134_outer,
    g_table8_135_outer, g_table8_136_outer, g_table8_137_outer,
    g_table8_138_outer, g_table8_139_outer, g_table8_140_outer,
    g_table8_141_outer, g_table8_142_outer, g_table8_143_outer,
    g_table8_144_outer, g_table8_145_outer, g_table8_146_outer,
    g_table8_147_outer, g_table8_148_outer, g_table8_149_outer,
    g_table8_150_outer, g_table8_151_outer, g_table8_152_outer,
    g_table8_153_outer, g_table8_154_outer, g_table8_155_outer,
    g_table8_156_outer, g_table8_157_outer, g_table8_158_outer,
    g_table8_159_outer, g_table8_160_outer, g_table8_161_outer,
    g_table8_162_outer, g_table8_163_outer, g_table8_164_outer,
    g_table8_165_outer, g_table8_166_outer, g_table8_167_outer,
    g_table8_168_outer, g_table8_169_outer, g_table8_170_outer,
    g_table8_171_outer, g_table8_172_outer, g_table8_173_outer,
    g_table8_174_outer, g_table8_175_outer, g_table8_176_outer,
    g_table8_177_outer, g_table8_178_outer, g_table8_179_outer,
    g_table8_180_outer, g_table8_181_outer, g_table8_182_outer,
    g_table8_183_outer, g_table8_184_outer, g_table8_185_outer,
    g_table8_186_outer, g_table8_187_outer, g_table8_188_outer,
    g_table8_189_outer, g_table8_190_outer, g_table8_191_outer,
    g_table8_192_outer, g_table8_193_outer, g_table8_194_outer,
    g_table8_195_outer, g_table8_196_outer, g_table8_197_outer,
    g_table8_198_outer, g_table8_199_outer, g_table8_200_outer,
    g_table8_201_outer, g_table8_202_outer, g_table8_203_outer,
    g_table8_204_outer, g_table8_205_outer, g_table8_206_outer,
    g_table8_207_outer, g_table8_208_outer, g_table8_209_outer,
    g_table8_210_outer, g_table8_211_outer, g_table8_212_outer,
    g_table8_213_outer, g_table8_214_outer, g_table8_215_outer,
    g_table8_216_outer, g_table8_217_outer, g_table8_218_outer,
    g_table8_219_outer, g_table8_220_outer, g_table8_221_outer,
    g_table8_222_outer, g_table8_223_outer, g_table8_224_outer,
    g_table8_225_outer, g_table8_226_outer, g_table8_227_outer,
    g_table8_228_outer, g_table8_229_outer, g_table8_230_outer,
    g_table8_231_outer, g_table8_232_outer, g_table8_233_outer,
    g_table8_234_outer, g_table8_235_outer, g_table8_236_outer,
    g_table8_237_outer, g_table8_238_outer, g_table8_239_outer,
    g_table8_240_outer, g_table8_241_outer, g_table8_242_outer,
    g_table8_243_outer, g_table8_244_outer, g_table8_245_outer,
    g_table8_246_outer, g_table8_247_outer, g_table8_248_outer,
    g_table8_249_outer, g_table8_250_outer, g_table8_251_outer,
    g_table8_252_outer, g_table8_253_outer, g_table8_254_outer,
    g_table8_255_outer,
};
inline uint64_t GetOp8(size_t i) {
  return g_table8_inner[i >> 8][g_table8_outer[i >> 8][i & 0xff]];
}
inline uint64_t GetEmit8(size_t i, size_t emit) {
  return g_table8_emit[i >> 8][emit];
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
    if (!RefillTo16()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 16)) & 0xffff;
    const auto op = GetOp8(index);
    buffer_len_ -= op & 31;
    const auto emit_ofs = op >> 6;
    switch ((op >> 5) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit8(index, emit_ofs + 0));
        break;
      }
    }
  }
  bool RefillTo16() {
    switch (buffer_len_) {
      case 10:
      case 11:
      case 12:
      case 13:
      case 14:
      case 15:
      case 8:
      case 9: {
        return Read1();
      }
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7: {
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
