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
static const uint16_t g_table1_0_ops[128] = {
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
static const uint8_t* const g_table1_emit[] = {
    g_table1_0_emit,
};
static const uint16_t* const g_table1_ops[] = {
    g_table1_0_ops,
};
inline uint64_t GetOp1(size_t i) { return g_table1_ops[i >> 7][i & 0x7f]; }
inline uint64_t GetEmit1(size_t i, size_t emit) {
  return g_table1_emit[i >> 7][emit];
}
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
static const uint8_t* const g_table2_emit[] = {
    g_table2_0_emit,
};
static const uint16_t* const g_table2_inner[] = {
    g_table2_0_inner,
};
static const uint8_t* const g_table2_outer[] = {
    g_table2_0_outer,
};
inline uint64_t GetOp2(size_t i) {
  return g_table2_inner[i >> 8][g_table2_outer[i >> 8][i & 0xff]];
}
inline uint64_t GetEmit2(size_t i, size_t emit) {
  return g_table2_emit[i >> 8][emit];
}
static const uint8_t g_table3_0_emit[4] = {0x21, 0x22, 0x28, 0x29};
static const uint8_t g_table3_0_ops[4] = {0x02, 0x06, 0x0a, 0x0e};
static const uint8_t* const g_table3_emit[] = {
    g_table3_0_emit,
};
static const uint8_t* const g_table3_ops[] = {
    g_table3_0_ops,
};
inline uint64_t GetOp3(size_t i) { return g_table3_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit3(size_t i, size_t emit) {
  return g_table3_emit[i >> 2][emit];
}
static const uint8_t g_table4_0_emit[145] = {
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
static const uint16_t g_table4_0_inner[85] = {
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
static const uint8_t g_table4_0_outer[256] = {
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
static const uint8_t* const g_table4_emit[] = {
    g_table4_0_emit,
};
static const uint16_t* const g_table4_inner[] = {
    g_table4_0_inner,
};
static const uint8_t* const g_table4_outer[] = {
    g_table4_0_outer,
};
inline uint64_t GetOp4(size_t i) {
  return g_table4_inner[i >> 8][g_table4_outer[i >> 8][i & 0xff]];
}
inline uint64_t GetEmit4(size_t i, size_t emit) {
  return g_table4_emit[i >> 8][emit];
}
static const uint8_t g_table5_0_emit[15] = {0x5c, 0xc3, 0xd0, 0x80, 0x82,
                                            0x83, 0xa2, 0xb8, 0xc2, 0xe0,
                                            0xe2, 0x99, 0xa1, 0xa7, 0xac};
static const uint8_t g_table5_0_ops[32] = {
    0x03, 0x03, 0x03, 0x03, 0x0b, 0x0b, 0x0b, 0x0b, 0x13, 0x13, 0x13,
    0x13, 0x1c, 0x1c, 0x24, 0x24, 0x2c, 0x2c, 0x34, 0x34, 0x3c, 0x3c,
    0x44, 0x44, 0x4c, 0x4c, 0x54, 0x54, 0x5d, 0x65, 0x6d, 0x75};
static const uint8_t* const g_table5_emit[] = {
    g_table5_0_emit,
};
static const uint8_t* const g_table5_ops[] = {
    g_table5_0_ops,
};
inline uint64_t GetOp5(size_t i) { return g_table5_ops[i >> 5][i & 0x1f]; }
inline uint64_t GetEmit5(size_t i, size_t emit) {
  return g_table5_emit[i >> 5][emit];
}
static const uint8_t g_table6_0_emit[76] = {
    0xb0, 0xb1, 0xb3, 0xd1, 0xd8, 0xd9, 0xe3, 0xe5, 0xe6, 0x81, 0x84,
    0x85, 0x86, 0x88, 0x92, 0x9a, 0x9c, 0xa0, 0xa3, 0xa4, 0xa9, 0xaa,
    0xad, 0xb2, 0xb5, 0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4,
    0xe8, 0xe9, 0x01, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8f, 0x93,
    0x95, 0x96, 0x97, 0x98, 0x9b, 0x9d, 0x9e, 0xa5, 0xa6, 0xa8, 0xae,
    0xaf, 0xb4, 0xb6, 0xb7, 0xbc, 0xbf, 0xc5, 0xe7, 0xef, 0x09, 0x8e,
    0x90, 0x91, 0x94, 0x9f, 0xab, 0xce, 0xd7, 0xe1, 0xec, 0xed};
static const uint16_t g_table6_0_inner[86] = {
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
static const uint8_t g_table6_0_outer[256] = {
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
  return g_table6_inner[i >> 8][g_table6_outer[i >> 8][i & 0xff]];
}
inline uint64_t GetEmit6(size_t i, size_t emit) {
  return g_table6_emit[i >> 8][emit];
}
static const uint8_t g_table7_0_emit[2] = {0xc7, 0xcf};
static const uint8_t g_table7_0_ops[2] = {0x01, 0x03};
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
static const uint8_t g_table8_0_emit[2] = {0xea, 0xeb};
#define g_table8_0_ops g_table7_0_ops
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
static const uint8_t g_table9_0_emit[4] = {0xc0, 0xc1, 0xc8, 0xc9};
#define g_table9_0_ops g_table3_0_ops
static const uint8_t* const g_table9_emit[] = {
    g_table9_0_emit,
};
static const uint8_t* const g_table9_ops[] = {
    g_table9_0_ops,
};
inline uint64_t GetOp9(size_t i) { return g_table9_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit9(size_t i, size_t emit) {
  return g_table9_emit[i >> 2][emit];
}
static const uint8_t g_table10_0_emit[4] = {0xca, 0xcd, 0xd2, 0xd5};
#define g_table10_0_ops g_table3_0_ops
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
static const uint8_t g_table11_0_emit[4] = {0xda, 0xdb, 0xee, 0xf0};
#define g_table11_0_ops g_table3_0_ops
static const uint8_t* const g_table11_emit[] = {
    g_table11_0_emit,
};
static const uint8_t* const g_table11_ops[] = {
    g_table11_0_ops,
};
inline uint64_t GetOp11(size_t i) { return g_table11_ops[i >> 2][i & 0x3]; }
inline uint64_t GetEmit11(size_t i, size_t emit) {
  return g_table11_emit[i >> 2][emit];
}
static const uint8_t g_table12_0_emit[8] = {0xd3, 0xd4, 0xd6, 0xdd,
                                            0xde, 0xdf, 0xf1, 0xf4};
static const uint8_t g_table12_0_ops[8] = {0x03, 0x07, 0x0b, 0x0f,
                                           0x13, 0x17, 0x1b, 0x1f};
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
static const uint8_t g_table13_0_emit[8] = {0xf5, 0xf6, 0xf7, 0xf8,
                                            0xfa, 0xfb, 0xfc, 0xfd};
#define g_table13_0_ops g_table12_0_ops
static const uint8_t* const g_table13_emit[] = {
    g_table13_0_emit,
};
static const uint8_t* const g_table13_ops[] = {
    g_table13_0_ops,
};
inline uint64_t GetOp13(size_t i) { return g_table13_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit13(size_t i, size_t emit) {
  return g_table13_emit[i >> 3][emit];
}
static const uint8_t g_table14_0_emit[15] = {0xfe, 0x02, 0x03, 0x04, 0x05,
                                             0x06, 0x07, 0x08, 0x0b, 0x0c,
                                             0x0e, 0x0f, 0x10, 0x11, 0x12};
static const uint8_t g_table14_0_ops[16] = {0x03, 0x03, 0x0c, 0x14, 0x1c, 0x24,
                                            0x2c, 0x34, 0x3c, 0x44, 0x4c, 0x54,
                                            0x5c, 0x64, 0x6c, 0x74};
static const uint8_t* const g_table14_emit[] = {
    g_table14_0_emit,
};
static const uint8_t* const g_table14_ops[] = {
    g_table14_0_ops,
};
inline uint64_t GetOp14(size_t i) { return g_table14_ops[i >> 4][i & 0xf]; }
inline uint64_t GetEmit14(size_t i, size_t emit) {
  return g_table14_emit[i >> 4][emit];
}
static const uint8_t g_table15_0_emit[5] = {0xf2, 0xf3, 0xff, 0xcb, 0xcc};
static const uint8_t g_table15_0_ops[8] = {0x02, 0x02, 0x06, 0x06,
                                           0x0a, 0x0a, 0x0f, 0x13};
static const uint8_t* const g_table15_emit[] = {
    g_table15_0_emit,
};
static const uint8_t* const g_table15_ops[] = {
    g_table15_0_ops,
};
inline uint64_t GetOp15(size_t i) { return g_table15_ops[i >> 3][i & 0x7]; }
inline uint64_t GetEmit15(size_t i, size_t emit) {
  return g_table15_emit[i >> 3][emit];
}
static const uint8_t g_table16_0_emit[8] = {0x13, 0x14, 0x15, 0x17,
                                            0x18, 0x19, 0x1a, 0x1b};
static const uint8_t g_table16_0_ops[32] = {
    0x04, 0x04, 0x04, 0x04, 0x14, 0x14, 0x14, 0x14, 0x24, 0x24, 0x24,
    0x24, 0x34, 0x34, 0x34, 0x34, 0x44, 0x44, 0x44, 0x44, 0x54, 0x54,
    0x54, 0x54, 0x64, 0x64, 0x64, 0x64, 0x74, 0x74, 0x74, 0x74};
static const uint8_t g_table16_1_emit[10] = {0x1c, 0x1d, 0x1e, 0x1f, 0x7f,
                                             0xdc, 0xf9, 0x0a, 0x0d, 0x16};
static const uint8_t g_table16_1_ops[32] = {
    0x04, 0x04, 0x04, 0x04, 0x14, 0x14, 0x14, 0x14, 0x24, 0x24, 0x24,
    0x24, 0x34, 0x34, 0x34, 0x34, 0x44, 0x44, 0x44, 0x44, 0x54, 0x54,
    0x54, 0x54, 0x64, 0x64, 0x64, 0x64, 0x76, 0x86, 0x96, 0x0e};
static const uint8_t* const g_table16_emit[] = {
    g_table16_0_emit,
    g_table16_1_emit,
};
static const uint8_t* const g_table16_ops[] = {
    g_table16_0_ops,
    g_table16_1_ops,
};
inline uint64_t GetOp16(size_t i) { return g_table16_ops[i >> 5][i & 0x1f]; }
inline uint64_t GetEmit16(size_t i, size_t emit) {
  return g_table16_emit[i >> 5][emit];
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
      case 2: {
        DecodeStep2();
        break;
      }
      case 3: {
        DecodeStep3();
        break;
      }
      case 1: {
        sink_(GetEmit4(index, emit_ofs + 0));
        break;
      }
      case 0: {
        sink_(GetEmit4(index, emit_ofs + 0));
        sink_(GetEmit4(index, emit_ofs + 1));
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
    if (!RefillTo8()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
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
    const auto op = GetOp8(index);
    buffer_len_ -= op & 1;
    const auto emit_ofs = op >> 1;
    sink_(GetEmit8(index, emit_ofs + 0));
  }
  void DecodeStep6() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp9(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit9(index, emit_ofs + 0));
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
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp11(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit11(index, emit_ofs + 0));
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
    const auto op = GetOp13(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit13(index, emit_ofs + 0));
  }
  void DecodeStep11() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetOp14(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit14(index, emit_ofs + 0));
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
    const auto op = GetOp15(index);
    buffer_len_ -= op & 3;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit15(index, emit_ofs + 0));
  }
  void DecodeStep13() {
    if (!RefillTo6()) {
      ok_ = false;
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 6)) & 0x3f;
    const auto op = GetOp16(index);
    buffer_len_ -= op & 7;
    const auto emit_ofs = op >> 4;
    switch ((op >> 3) & 1) {
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
      case 0: {
        sink_(GetEmit16(index, emit_ofs + 0));
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
