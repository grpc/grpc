#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=117 unique=36 flat=288 nested=576
static const uint8_t g_emit_buffer_0[36] = {
    48, 49, 50,  97,  99,  101, 105, 111, 115, 116, 32,  37,
    45, 46, 47,  51,  52,  53,  54,  55,  56,  57,  61,  65,
    95, 98, 100, 102, 103, 104, 108, 109, 110, 112, 114, 117};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=3996 unique=54 flat=1024 nested=1376
static const uint16_t g_emit_op_0[64] = {
    5,    5,    119,  119,  233,  233,  347,  347,  461,  461,  575,
    575,  689,  689,  803,  803,  917,  917,  1031, 1031, 1146, 1260,
    1374, 1488, 1602, 1716, 1830, 1944, 2058, 2172, 2286, 2400, 2514,
    2628, 2742, 2856, 2970, 3084, 3198, 3312, 3426, 3540, 3654, 3768,
    3882, 3996, 12,   18,   24,   30,   36,   42,   48,   54,   60,
    66,   72,   78,   84,   90,   96,   102,  108,  114};
inline uint16_t GetEmitOp0(size_t i) { return g_emit_op_0[i]; }
// max=122 unique=68 flat=544 nested=1088
static const uint8_t g_emit_buffer_1[68] = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32,  37, 45,  46,
    47,  51,  52,  53,  54,  55,  56,  57,  61,  65,  95,  98, 100, 102,
    103, 104, 108, 109, 110, 112, 114, 117, 58,  66,  67,  68, 69,  70,
    71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82, 83,  84,
    85,  86,  87,  89,  106, 107, 113, 118, 119, 120, 121, 122};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=2352 unique=72 flat=2048 nested=2176
static const uint16_t g_emit_op_1[128] = {
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
    2247, 2282, 2317, 2352, 14,   21,   28,   35};
inline uint16_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=42 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {38, 42};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=9 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {8, 9};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=59 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {44, 59};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=9 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {8, 9};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=90 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {88, 90};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=9 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_4[2] = {8, 9};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=126 unique=18 flat=144 nested=288
static const uint8_t g_emit_buffer_5[18] = {
    33, 34, 40, 41, 63, 39, 43, 124, 35, 62, 0, 36, 64, 91, 93, 126, 94, 125};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=371 unique=20 flat=2048 nested=1344
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
    10,  31,  52,  73,  94,  116, 137, 158, 180, 201,
    223, 244, 265, 286, 307, 328, 350, 371, 14,  21};
inline uint16_t GetEmitOp5(size_t i) {
  return g_emit_op_5_inner[g_emit_op_5_outer[i]];
}
// max=96 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {60, 96};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=16 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {15, 16};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=230 unique=25 flat=200 nested=400
static const uint8_t g_emit_buffer_7[25] = {
    123, 92,  195, 208, 128, 130, 131, 162, 184, 194, 224, 226, 153,
    161, 167, 172, 176, 177, 179, 209, 216, 217, 227, 229, 230};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=4053 unique=48 flat=2048 nested=1792
// monotonic increasing
static const uint8_t g_emit_op_7_outer[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
    4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};
static const uint16_t g_emit_op_7_inner[48] = {
    15,   187,  355,  523,  692,  860,  1028, 1196, 1364, 1532, 1700, 1868,
    2037, 2205, 2373, 2541, 2709, 2877, 3045, 3213, 3381, 3549, 3717, 3885,
    4053, 14,   21,   28,   35,   42,   49,   56,   63,   70,   77,   84,
    91,   98,   105,  112,  119,  126,  133,  140,  147,  154,  161,  168};
inline uint16_t GetEmitOp7(size_t i) {
  return g_emit_op_7_inner[g_emit_op_7_outer[i]];
}
// max=132 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {129, 132};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {22, 23};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=134 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {133, 134};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_9[2] = {22, 23};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=146 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_10[2] = {136, 146};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_10[2] = {22, 23};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=156 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_11[2] = {154, 156};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_11[2] = {22, 23};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=163 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_12[2] = {160, 163};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_12[2] = {22, 23};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=169 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_13[2] = {164, 169};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_13[2] = {22, 23};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=173 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_14[2] = {170, 173};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_14[2] = {22, 23};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=181 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_15[2] = {178, 181};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_15[2] = {22, 23};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=186 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_16[2] = {185, 186};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_16[2] = {22, 23};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=189 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_17[2] = {187, 189};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_17[2] = {22, 23};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=196 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_18[2] = {190, 196};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_18[2] = {22, 23};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=228 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_19[2] = {198, 228};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_19[2] = {22, 23};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=233 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_20[2] = {232, 233};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=23 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_20[2] = {22, 23};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=138 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_21[4] = {1, 135, 137, 138};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_21[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=143 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_22[4] = {139, 140, 141, 143};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_22[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=151 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_23[4] = {147, 149, 150, 151};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_23[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=158 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_24[4] = {152, 155, 157, 158};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_24[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=174 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_25[4] = {165, 166, 168, 174};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_25[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=183 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_26[4] = {175, 180, 182, 183};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_26[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=231 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_27[4] = {188, 191, 197, 231};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=29 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_27[4] = {23, 25, 27, 29};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=237 unique=10 flat=80 nested=160
static const uint8_t g_emit_buffer_28[10] = {171, 206, 215, 225, 236,
                                             237, 199, 207, 234, 235};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=61 unique=10 flat=128 nested=208
// monotonic increasing
static const uint8_t g_emit_op_28[16] = {24, 24, 28, 28, 32, 32, 36, 36,
                                         40, 40, 44, 44, 49, 53, 57, 61};
inline uint8_t GetEmitOp28(size_t i) { return g_emit_op_28[i]; }
// max=239 unique=7 flat=56 nested=112
static const uint8_t g_emit_buffer_29[7] = {239, 9, 142, 144, 145, 148, 159};
inline uint8_t GetEmitBuffer29(size_t i) { return g_emit_buffer_29[i]; }
// max=42 unique=7 flat=64 nested=120
// monotonic increasing
static const uint8_t g_emit_op_29[8] = {23, 23, 27, 30, 33, 36, 39, 42};
inline uint8_t GetEmitOp29(size_t i) { return g_emit_op_29[i]; }
// max=255 unique=63 flat=504 nested=1008
static const uint8_t g_emit_buffer_30[63] = {
    192, 193, 200, 201, 202, 205, 210, 213, 218, 219, 238, 240, 242,
    243, 255, 203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245,
    246, 247, 248, 250, 251, 252, 253, 254, 2,   3,   4,   5,   6,
    7,   8,   11,  12,  14,  15,  16,  17,  18,  19,  20,  21,  23,
    24,  25,  26,  27,  28,  29,  30,  31,  127, 220, 249};
inline uint8_t GetEmitBuffer30(size_t i) { return g_emit_buffer_30[i]; }
// max=896 unique=64 flat=2048 nested=2048
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
    26,  40,  54,  68,  82,  96,  110, 124, 138, 152, 166, 180, 194,
    208, 222, 237, 251, 265, 279, 293, 307, 321, 335, 349, 363, 377,
    391, 405, 419, 433, 447, 461, 475, 489, 504, 518, 532, 546, 560,
    574, 588, 602, 616, 630, 644, 658, 672, 686, 700, 714, 728, 742,
    756, 770, 784, 798, 812, 826, 840, 854, 868, 882, 896, 14};
inline uint16_t GetEmitOp30(size_t i) {
  return g_emit_op_30_inner[g_emit_op_30_outer[i]];
}
// max=22 unique=3 flat=24 nested=48
// monotonic increasing
static const uint8_t g_emit_buffer_31[3] = {10, 13, 22};
inline uint8_t GetEmitBuffer31(size_t i) { return g_emit_buffer_31[i]; }
// max=38 unique=4 flat=32 nested=64
static const uint8_t g_emit_op_31[4] = {30, 34, 38, 32};
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
      const auto index = buffer_ >> (buffer_len_ - 7);
      auto op = GetEmitOp1(index);
      buffer_len_ -= op % 7;
      op /= 7;
      const auto emit_ofs = op / 5;
      switch (op % 5) {
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
    while (buffer_len_ < 7) {
      if (begin_ == end_) return false;
      buffer_ <<= 8;
      buffer_ |= static_cast<uint64_t>(*begin_++);
      buffer_len_ += 8;
    }
    return true;
  }
  void DecodeStep0() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp2(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer2(op + 0));
  }
  bool RefillTo1() {
    while (buffer_len_ < 1) {
      if (begin_ == end_) return false;
      buffer_ <<= 8;
      buffer_ |= static_cast<uint64_t>(*begin_++);
      buffer_len_ += 8;
    }
    return true;
  }
  void DecodeStep1() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp3(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer3(op + 0));
  }
  void DecodeStep2() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp4(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer4(op + 0));
  }
  void DecodeStep3() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 7);
    auto op = GetEmitOp5(index);
    buffer_len_ -= op % 7;
    op /= 7;
    const auto emit_ofs = op / 3;
    switch (op % 3) {
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
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp6(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer6(op + 0));
  }
  void DecodeStep5() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 7);
    auto op = GetEmitOp7(index);
    buffer_len_ -= op % 7;
    op /= 7;
    const auto emit_ofs = op / 24;
    switch (op % 24) {
      case 5: {
        DecodeStep10();
        break;
      }
      case 6: {
        DecodeStep11();
        break;
      }
      case 7: {
        DecodeStep12();
        break;
      }
      case 8: {
        DecodeStep13();
        break;
      }
      case 9: {
        DecodeStep14();
        break;
      }
      case 10: {
        DecodeStep15();
        break;
      }
      case 11: {
        DecodeStep16();
        break;
      }
      case 12: {
        DecodeStep17();
        break;
      }
      case 13: {
        DecodeStep18();
        break;
      }
      case 14: {
        DecodeStep19();
        break;
      }
      case 15: {
        DecodeStep20();
        break;
      }
      case 16: {
        DecodeStep21();
        break;
      }
      case 17: {
        DecodeStep22();
        break;
      }
      case 18: {
        DecodeStep23();
        break;
      }
      case 19: {
        DecodeStep24();
        break;
      }
      case 20: {
        DecodeStep25();
        break;
      }
      case 22: {
        DecodeStep26();
        break;
      }
      case 21: {
        DecodeStep27();
        break;
      }
      case 23: {
        DecodeStep28();
        break;
      }
      case 1: {
        DecodeStep6();
        break;
      }
      case 2: {
        DecodeStep7();
        break;
      }
      case 3: {
        DecodeStep8();
        break;
      }
      case 4: {
        DecodeStep9();
        break;
      }
      case 0: {
        sink_(GetEmitBuffer7(emit_ofs + 0));
        break;
      }
    }
  }
  void DecodeStep6() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp8(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer8(op + 0));
  }
  void DecodeStep7() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp9(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer9(op + 0));
  }
  void DecodeStep8() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp10(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer10(op + 0));
  }
  void DecodeStep9() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp11(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer11(op + 0));
  }
  void DecodeStep10() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp12(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer12(op + 0));
  }
  void DecodeStep11() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp13(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer13(op + 0));
  }
  void DecodeStep12() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp14(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer14(op + 0));
  }
  void DecodeStep13() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp15(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer15(op + 0));
  }
  void DecodeStep14() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp16(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer16(op + 0));
  }
  void DecodeStep15() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp17(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer17(op + 0));
  }
  void DecodeStep16() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp18(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer18(op + 0));
  }
  void DecodeStep17() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp19(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer19(op + 0));
  }
  void DecodeStep18() {
    if (!RefillTo1()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 1);
    auto op = GetEmitOp20(index);
    buffer_len_ -= op % 1;
    sink_(GetEmitBuffer20(op + 0));
  }
  void DecodeStep19() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp21(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer21(op + 0));
  }
  bool RefillTo2() {
    while (buffer_len_ < 2) {
      if (begin_ == end_) return false;
      buffer_ <<= 8;
      buffer_ |= static_cast<uint64_t>(*begin_++);
      buffer_len_ += 8;
    }
    return true;
  }
  void DecodeStep20() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp22(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer22(op + 0));
  }
  void DecodeStep21() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp23(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer23(op + 0));
  }
  void DecodeStep22() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp24(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer24(op + 0));
  }
  void DecodeStep23() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp25(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer25(op + 0));
  }
  void DecodeStep24() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp26(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer26(op + 0));
  }
  void DecodeStep25() {
    if (!RefillTo2()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp27(index);
    buffer_len_ -= op % 2;
    sink_(GetEmitBuffer27(op + 0));
  }
  void DecodeStep26() {
    if (!RefillTo4()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 4);
    auto op = GetEmitOp28(index);
    buffer_len_ -= op % 4;
    sink_(GetEmitBuffer28(op + 0));
  }
  bool RefillTo4() {
    while (buffer_len_ < 4) {
      if (begin_ == end_) return false;
      buffer_ <<= 8;
      buffer_ |= static_cast<uint64_t>(*begin_++);
      buffer_len_ += 8;
    }
    return true;
  }
  void DecodeStep27() {
    if (!RefillTo3()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 3);
    auto op = GetEmitOp29(index);
    buffer_len_ -= op % 3;
    sink_(GetEmitBuffer29(op + 0));
  }
  bool RefillTo3() {
    while (buffer_len_ < 3) {
      if (begin_ == end_) return false;
      buffer_ <<= 8;
      buffer_ |= static_cast<uint64_t>(*begin_++);
      buffer_len_ += 8;
    }
    return true;
  }
  void DecodeStep28() {
    if (!RefillTo7()) {
      ok_ = false;
      return;
    }
    const auto index = buffer_ >> (buffer_len_ - 7);
    auto op = GetEmitOp30(index);
    buffer_len_ -= op % 7;
    op /= 7;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
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
    const auto index = buffer_ >> (buffer_len_ - 2);
    auto op = GetEmitOp31(index);
    buffer_len_ -= op % 2;
    op /= 2;
    const auto emit_ofs = op / 2;
    switch (op % 2) {
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
    const auto index = buffer_ >> (buffer_len_ - 6);
    auto op = GetEmitOp0(index);
    buffer_len_ -= op % 6;
    op /= 6;
    const auto emit_ofs = op / 19;
    switch (op % 19) {
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
        ok_ = false;
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
