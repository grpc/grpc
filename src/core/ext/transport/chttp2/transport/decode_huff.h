#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=122 unique=68 flat=544 nested=1088
static const uint8_t g_emit_buffer_0[68] = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32,  37, 45,  46,
    47,  51,  52,  53,  54,  55,  56,  57,  61,  65,  95,  98, 100, 102,
    103, 104, 108, 109, 110, 112, 114, 117, 58,  66,  67,  68, 69,  70,
    71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82, 83,  84,
    85,  86,  87,  89,  106, 107, 113, 118, 119, 120, 121, 122};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=2352 unique=72 flat=2048 nested=2176
static const uint16_t g_emit_op_0[128] = {
    5,    5,    5,    5,    40,   40,   40,   40,   75,   75,   75,   75,
    110,  110,  110,  110,  145,  145,  145,  145,  180,  180,  180,  180,
    215,  215,  215,  215,  250,  250,  250,  250,  285,  285,  285,  285,
    320,  320,  320,  320,  356,  356,  391,  391,  426,  426,  461,  461,
    496,  496,  531,  531,  566,  566,  601,  601,  636,  636,  671,  671,
    706,  706,  741,  741,  776,  776,  811,  811,  846,  846,  881,  881,
    916,  916,  951,  951,  986,  986,  1021, 1021, 1056, 1056, 1091, 1091,
    1126, 1126, 1161, 1161, 1196, 1196, 1231, 1231, 1267, 1302, 1337, 1372,
    1407, 1442, 1477, 1512, 1547, 1582, 1617, 1652, 1687, 1722, 1757, 1792,
    1827, 1862, 1897, 1932, 1967, 2002, 2037, 2072, 2107, 2142, 2177, 2212,
    2247, 2282, 2317, 2352, 7,    14,   21,   28};
inline uint16_t GetEmitOp0(size_t i) { return g_emit_op_0[i]; }
// max=42 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_1[2] = {38, 42};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=9 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_1[2] = {8, 9};
inline uint8_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=59 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {44, 59};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=9 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {8, 9};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=90 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {88, 90};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=9 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {8, 9};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=126 unique=18 flat=144 nested=288
static const uint8_t g_emit_buffer_4[18] = {
    33, 34, 40, 41, 63, 39, 43, 124, 35, 62, 0, 36, 64, 91, 93, 126, 94, 125};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=371 unique=20 flat=2048 nested=1344
// monotonic increasing
static const uint8_t g_emit_op_4_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 1, 1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2, 2, 2, 2, 2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3, 3, 3, 3, 3,  3,
    3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4, 4, 4, 4, 4,  4,
    4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6, 6, 6, 6, 6,  6,
    6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  9, 9, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 17, 18, 19};
static const uint16_t g_emit_op_4_inner[20] = {
    10,  31,  52,  73,  94,  116, 137, 158, 180, 201,
    223, 244, 265, 286, 307, 328, 350, 371, 7,   14};
inline uint16_t GetEmitOp4(size_t i) {
  return g_emit_op_4_inner[g_emit_op_4_outer[i]];
}
// max=96 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {60, 96};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=16 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_5[2] = {15, 16};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=230 unique=25 flat=200 nested=400
static const uint8_t g_emit_buffer_6[25] = {
    123, 92,  195, 208, 128, 130, 131, 162, 184, 194, 224, 226, 153,
    161, 167, 172, 176, 177, 179, 209, 216, 217, 227, 229, 230};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=4053 unique=48 flat=2048 nested=1792
// monotonic increasing
static const uint8_t g_emit_op_6_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
    4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};
static const uint16_t g_emit_op_6_inner[48] = {
    15,   187,  355,  523,  692,  860,  1028, 1196, 1364, 1532, 1700, 1868,
    2037, 2205, 2373, 2541, 2709, 2877, 3045, 3213, 3381, 3549, 3717, 3885,
    4053, 7,    14,   21,   28,   35,   42,   49,   56,   63,   70,   77,
    84,   91,   98,   105,  112,  119,  126,  133,  140,  147,  154,  161};
inline uint16_t GetEmitOp6(size_t i) {
  return g_emit_op_6_inner[g_emit_op_6_outer[i]];
}
// max=132 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {129, 132};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_7[2] = {22, 23};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=134 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {133, 134};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {22, 23};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=146 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {136, 146};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_9[2] = {22, 23};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=156 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_10[2] = {154, 156};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_10[2] = {22, 23};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=163 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_11[2] = {160, 163};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_11[2] = {22, 23};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=169 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_12[2] = {164, 169};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_12[2] = {22, 23};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=173 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_13[2] = {170, 173};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_13[2] = {22, 23};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=181 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {178, 181};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_14[2] = {22, 23};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=186 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {185, 186};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_15[2] = {22, 23};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=189 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_16[2] = {187, 189};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_16[2] = {22, 23};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=196 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {190, 196};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_17[2] = {22, 23};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=228 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {198, 228};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_18[2] = {22, 23};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=233 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_19[2] = {232, 233};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_19[2] = {22, 23};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=138 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_20[4] = {1, 135, 137, 138};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_20[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=143 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_21[4] = {139, 140, 141, 143};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_21[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=151 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_22[4] = {147, 149, 150, 151};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_22[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=158 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_23[4] = {152, 155, 157, 158};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_23[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=174 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_24[4] = {165, 166, 168, 174};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_24[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=183 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_25[4] = {175, 180, 182, 183};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_25[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=231 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_26[4] = {188, 191, 197, 231};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_26[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=237 unique=10 flat=80 nested=160
static const uint8_t g_emit_buffer_27[10] = {171, 206, 215, 225, 236,
                                             237, 199, 207, 234, 235};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=61 unique=10 flat=128 nested=208
// monotonic increasing
static const uint8_t g_emit_op_27[16] = {24, 24, 28, 28, 32, 32, 36, 36,
                                         40, 40, 44, 44, 49, 53, 57, 61};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=239 unique=7 flat=56 nested=112
static const uint8_t g_emit_buffer_28[7] = {239, 9, 142, 144, 145, 148, 159};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=42 unique=7 flat=64 nested=120
// monotonic increasing
static const uint8_t g_emit_op_28[8] = {23, 23, 27, 30, 33, 36, 39, 42};
inline uint8_t GetEmitOp28(size_t i) { return g_emit_op_28[i]; }
// max=255 unique=63 flat=504 nested=1008
static const uint8_t g_emit_buffer_29[63] = {
    192, 193, 200, 201, 202, 205, 210, 213, 218, 219, 238, 240, 242,
    243, 255, 203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245,
    246, 247, 248, 250, 251, 252, 253, 254, 2,   3,   4,   5,   6,
    7,   8,   11,  12,  14,  15,  16,  17,  18,  19,  20,  21,  23,
    24,  25,  26,  27,  28,  29,  30,  31,  127, 220, 249};
inline uint8_t GetEmitBuffer29(size_t i) { return g_emit_buffer_29[i]; }
// max=896 unique=64 flat=2048 nested=2048
// monotonic increasing
static const uint8_t g_emit_op_29_outer[128] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,
    4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,
    9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14,
    14, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
    23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32,
    32, 33, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
static const uint16_t g_emit_op_29_inner[64] = {
    26,  40,  54,  68,  82,  96,  110, 124, 138, 152, 166, 180, 194,
    208, 222, 237, 251, 265, 279, 293, 307, 321, 335, 349, 363, 377,
    391, 405, 419, 433, 447, 461, 475, 489, 504, 518, 532, 546, 560,
    574, 588, 602, 616, 630, 644, 658, 672, 686, 700, 714, 728, 742,
    756, 770, 784, 798, 812, 826, 840, 854, 868, 882, 896, 7};
inline uint16_t GetEmitOp29(size_t i) {
  return g_emit_op_29_inner[g_emit_op_29_outer[i]];
}
// max=256 unique=4 flat=64 nested=96
// monotonic increasing
static const uint16_t g_emit_buffer_30[4] = {10, 13, 22, 256};
inline uint16_t GetEmitBuffer30(size_t i) { return g_emit_buffer_30[i]; }
// max=36 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_30[4] = {30, 32, 34, 36};
inline uint8_t GetEmitOp30(size_t i) { return g_emit_op_30[i]; }
template <typename F>
bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {
  uint64_t buffer = 0;
  uint64_t index;
  size_t emit_ofs;
  int buffer_len = 0;
  uint64_t op;
refill:
  while (buffer_len < 7) {
    if (begin == end) return buffer_len == 0;
    buffer <<= 8;
    buffer |= static_cast<uint64_t>(*begin++);
    buffer_len += 8;
  }
  index = buffer >> (buffer_len - 7);
  op = GetEmitOp0(index);
  buffer_len -= op % 7;
  op /= 7;
  emit_ofs = op / 5;
  switch (op % 5) {
    case 4: {
      // 0:38/6,1:ffd8/16,2:1fffe2/21,3:1fffe3/21,4:1fffe4/21,5:1fffe5/21,6:1fffe6/21,7:1fffe7/21,8:1fffe8/21,9:1ffea/17,10:7ffffc/23,11:1fffe9/21,12:1fffea/21,13:7ffffd/23,14:1fffeb/21,15:1fffec/21,16:1fffed/21,17:1fffee/21,18:1fffef/21,19:1ffff0/21,20:1ffff1/21,21:1ffff2/21,22:7ffffe/23,23:1ffff3/21,24:1ffff4/21,25:1ffff5/21,26:1ffff6/21,27:1ffff7/21,28:1ffff8/21,29:1ffff9/21,30:1ffffa/21,31:1ffffb/21,33:0/3,34:1/3,35:1a/5,36:39/6,39:a/4,40:2/3,41:3/3,43:b/4,60:fc/8,62:1b/5,63:4/3,64:3a/6,91:3b/6,92:ff0/12,93:3c/6,94:7c/7,96:fd/8,123:fe/8,124:c/4,125:7d/7,126:3d/6,127:1ffffc/21,128:1fe6/13,129:7fd2/15,130:1fe7/13,131:1fe8/13,132:7fd3/15,133:7fd4/15,134:7fd5/15,135:ffd9/16,136:7fd6/15,137:ffda/16,138:ffdb/16,139:ffdc/16,140:ffdd/16,141:ffde/16,142:1ffeb/17,143:ffdf/16,144:1ffec/17,145:1ffed/17,146:7fd7/15,147:ffe0/16,148:1ffee/17,149:ffe1/16,150:ffe2/16,151:ffe3/16,152:ffe4/16,153:3fdc/14,154:7fd8/15,155:ffe5/16,156:7fd9/15,157:ffe6/16,158:ffe7/16,159:1ffef/17,160:7fda/15,161:3fdd/14,162:1fe9/13,163:7fdb/15,164:7fdc/15,165:ffe8/16,166:ffe9/16,167:3fde/14,168:ffea/16,169:7fdd/15,170:7fde/15,171:1fff0/17,172:3fdf/14,173:7fdf/15,174:ffeb/16,175:ffec/16,176:3fe0/14,177:3fe1/14,178:7fe0/15,179:3fe2/14,180:ffed/16,181:7fe1/15,182:ffee/16,183:ffef/16,184:1fea/13,185:7fe2/15,186:7fe3/15,187:7fe4/15,188:fff0/16,189:7fe5/15,190:7fe6/15,191:fff1/16,192:7ffe0/19,193:7ffe1/19,194:1feb/13,195:ff1/12,196:7fe7/15,197:fff2/16,198:7fe8/15,199:3ffec/18,200:7ffe2/19,201:7ffe3/19,202:7ffe4/19,203:fffde/20,204:fffdf/20,205:7ffe5/19,206:1fff1/17,207:3ffed/18,208:ff2/12,209:3fe3/14,210:7ffe6/19,211:fffe0/20,212:fffe1/20,213:7ffe7/19,214:fffe2/20,215:1fff2/17,216:3fe4/14,217:3fe5/14,218:7ffe8/19,219:7ffe9/19,220:1ffffd/21,221:fffe3/20,222:fffe4/20,223:fffe5/20,224:1fec/13,225:1fff3/17,226:1fed/13,227:3fe6/14,228:7fe9/15,229:3fe7/14,230:3fe8/14,231:fff3/16,232:7fea/15,233:7feb/15,234:3ffee/18,235:3ffef/18,236:1fff4/17,237:1fff5/17,238:7ffea/19,239:fff4/16,240:7ffeb/19,241:fffe6/20,242:7ffec/19,243:7ffed/19,244:fffe7/20,245:fffe8/20,246:fffe9/20,247:fffea/20,248:fffeb/20,249:1ffffe/21,250:fffec/20,251:fffed/20,252:fffee/20,253:fffef/20,254:ffff0/20,255:7ffee/19,256:7fffff/23
      while (buffer_len < 7) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 7);
      op = GetEmitOp4(index);
      buffer_len -= op % 7;
      op /= 7;
      emit_ofs = op / 3;
      switch (op % 3) {
        case 2: {
          // 1:1d8/9,2:3fe2/14,3:3fe3/14,4:3fe4/14,5:3fe5/14,6:3fe6/14,7:3fe7/14,8:3fe8/14,9:3ea/10,10:fffc/16,11:3fe9/14,12:3fea/14,13:fffd/16,14:3feb/14,15:3fec/14,16:3fed/14,17:3fee/14,18:3fef/14,19:3ff0/14,20:3ff1/14,21:3ff2/14,22:fffe/16,23:3ff3/14,24:3ff4/14,25:3ff5/14,26:3ff6/14,27:3ff7/14,28:3ff8/14,29:3ff9/14,30:3ffa/14,31:3ffb/14,92:10/5,123:0/1,127:3ffc/14,128:26/6,129:d2/8,130:27/6,131:28/6,132:d3/8,133:d4/8,134:d5/8,135:1d9/9,136:d6/8,137:1da/9,138:1db/9,139:1dc/9,140:1dd/9,141:1de/9,142:3eb/10,143:1df/9,144:3ec/10,145:3ed/10,146:d7/8,147:1e0/9,148:3ee/10,149:1e1/9,150:1e2/9,151:1e3/9,152:1e4/9,153:5c/7,154:d8/8,155:1e5/9,156:d9/8,157:1e6/9,158:1e7/9,159:3ef/10,160:da/8,161:5d/7,162:29/6,163:db/8,164:dc/8,165:1e8/9,166:1e9/9,167:5e/7,168:1ea/9,169:dd/8,170:de/8,171:3f0/10,172:5f/7,173:df/8,174:1eb/9,175:1ec/9,176:60/7,177:61/7,178:e0/8,179:62/7,180:1ed/9,181:e1/8,182:1ee/9,183:1ef/9,184:2a/6,185:e2/8,186:e3/8,187:e4/8,188:1f0/9,189:e5/8,190:e6/8,191:1f1/9,192:fe0/12,193:fe1/12,194:2b/6,195:11/5,196:e7/8,197:1f2/9,198:e8/8,199:7ec/11,200:fe2/12,201:fe3/12,202:fe4/12,203:1fde/13,204:1fdf/13,205:fe5/12,206:3f1/10,207:7ed/11,208:12/5,209:63/7,210:fe6/12,211:1fe0/13,212:1fe1/13,213:fe7/12,214:1fe2/13,215:3f2/10,216:64/7,217:65/7,218:fe8/12,219:fe9/12,220:3ffd/14,221:1fe3/13,222:1fe4/13,223:1fe5/13,224:2c/6,225:3f3/10,226:2d/6,227:66/7,228:e9/8,229:67/7,230:68/7,231:1f3/9,232:ea/8,233:eb/8,234:7ee/11,235:7ef/11,236:3f4/10,237:3f5/10,238:fea/12,239:1f4/9,240:feb/12,241:1fe6/13,242:fec/12,243:fed/12,244:1fe7/13,245:1fe8/13,246:1fe9/13,247:1fea/13,248:1feb/13,249:3ffe/14,250:1fec/13,251:1fed/13,252:1fee/13,253:1fef/13,254:1ff0/13,255:fee/12,256:ffff/16
          while (buffer_len < 7) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 7);
          op = GetEmitOp6(index);
          buffer_len -= op % 7;
          op /= 7;
          emit_ofs = op / 24;
          switch (op % 24) {
            case 1: {
              // 129:0/1,132:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp7(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer7(op + 0));
              goto refill;
            }
            case 2: {
              // 133:0/1,134:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp8(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer8(op + 0));
              goto refill;
            }
            case 3: {
              // 136:0/1,146:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp9(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer9(op + 0));
              goto refill;
            }
            case 15: {
              // 139:0/2,140:1/2,141:2/2,143:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp21(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer21(op + 0));
              goto refill;
            }
            case 16: {
              // 147:0/2,149:1/2,150:2/2,151:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp22(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer22(op + 0));
              goto refill;
            }
            case 17: {
              // 152:0/2,155:1/2,157:2/2,158:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp23(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer23(op + 0));
              goto refill;
            }
            case 4: {
              // 154:0/1,156:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp10(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer10(op + 0));
              goto refill;
            }
            case 5: {
              // 160:0/1,163:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp11(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer11(op + 0));
              goto refill;
            }
            case 6: {
              // 164:0/1,169:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp12(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer12(op + 0));
              goto refill;
            }
            case 18: {
              // 165:0/2,166:1/2,168:2/2,174:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp24(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer24(op + 0));
              goto refill;
            }
            case 7: {
              // 170:0/1,173:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp13(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer13(op + 0));
              goto refill;
            }
            case 22: {
              // 171:0/3,199:c/4,206:1/3,207:d/4,215:2/3,225:3/3,234:e/4,235:f/4,236:4/3,237:5/3
              while (buffer_len < 4) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 4);
              op = GetEmitOp27(index);
              buffer_len -= op % 4;
              sink(GetEmitBuffer27(op + 0));
              goto refill;
            }
            case 19: {
              // 175:0/2,180:1/2,182:2/2,183:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp25(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer25(op + 0));
              goto refill;
            }
            case 8: {
              // 178:0/1,181:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp14(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer14(op + 0));
              goto refill;
            }
            case 9: {
              // 185:0/1,186:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp15(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer15(op + 0));
              goto refill;
            }
            case 10: {
              // 187:0/1,189:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp16(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer16(op + 0));
              goto refill;
            }
            case 20: {
              // 188:0/2,191:1/2,197:2/2,231:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp26(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer26(op + 0));
              goto refill;
            }
            case 11: {
              // 190:0/1,196:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp17(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer17(op + 0));
              goto refill;
            }
            case 12: {
              // 198:0/1,228:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp18(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer18(op + 0));
              goto refill;
            }
            case 14: {
              // 1:0/2,135:1/2,137:2/2,138:3/2
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              op = GetEmitOp20(index);
              buffer_len -= op % 2;
              sink(GetEmitBuffer20(op + 0));
              goto refill;
            }
            case 13: {
              // 232:0/1,233:1/1
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              op = GetEmitOp19(index);
              buffer_len -= op % 1;
              sink(GetEmitBuffer19(op + 0));
              goto refill;
            }
            case 23: {
              // 2:62/7,3:63/7,4:64/7,5:65/7,6:66/7,7:67/7,8:68/7,10:1fc/9,11:69/7,12:6a/7,13:1fd/9,14:6b/7,15:6c/7,16:6d/7,17:6e/7,18:6f/7,19:70/7,20:71/7,21:72/7,22:1fe/9,23:73/7,24:74/7,25:75/7,26:76/7,27:77/7,28:78/7,29:79/7,30:7a/7,31:7b/7,127:7c/7,192:0/5,193:1/5,200:2/5,201:3/5,202:4/5,203:1e/6,204:1f/6,205:5/5,210:6/5,211:20/6,212:21/6,213:7/5,214:22/6,218:8/5,219:9/5,220:7d/7,221:23/6,222:24/6,223:25/6,238:a/5,240:b/5,241:26/6,242:c/5,243:d/5,244:27/6,245:28/6,246:29/6,247:2a/6,248:2b/6,249:7e/7,250:2c/6,251:2d/6,252:2e/6,253:2f/6,254:30/6,255:e/5,256:1ff/9
              while (buffer_len < 7) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 7);
              op = GetEmitOp29(index);
              buffer_len -= op % 7;
              op /= 7;
              emit_ofs = op / 2;
              switch (op % 2) {
                case 1: {
                  // 10:0/2,13:1/2,22:2/2,256:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp30(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer30(op + 0));
                  goto refill;
                }
                case 0: {
                  sink(GetEmitBuffer29(emit_ofs + 0));
                  goto refill;
                }
              }
            }
            case 21: {
              // 9:2/3,142:3/3,144:4/3,145:5/3,148:6/3,159:7/3,239:0/2
              while (buffer_len < 3) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 3);
              op = GetEmitOp28(index);
              buffer_len -= op % 3;
              sink(GetEmitBuffer28(op + 0));
              goto refill;
            }
            case 0: {
              sink(GetEmitBuffer6(emit_ofs + 0));
              goto refill;
            }
          }
        }
        case 1: {
          // 60:0/1,96:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp5(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer5(op + 0));
          goto refill;
        }
        case 0: {
          sink(GetEmitBuffer4(emit_ofs + 0));
          goto refill;
        }
      }
    }
    case 1: {
      // 38:0/1,42:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp1(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer1(op + 0));
      goto refill;
    }
    case 2: {
      // 44:0/1,59:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp2(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer2(op + 0));
      goto refill;
    }
    case 3: {
      // 88:0/1,90:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp3(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer3(op + 0));
      goto refill;
    }
    case 0: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      goto refill;
    }
  }
  abort();
}
