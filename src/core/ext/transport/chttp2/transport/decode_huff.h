#include <stdlib.h>

#include <cstddef>
#include <cstdint>
// max=116 unique=10 flat=80 nested=160
// monotonic increasing
static const uint8_t g_emit_buffer_0[10] = {48,  49,  50,  97,  99,
                                            101, 105, 111, 115, 116};
inline uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=1040 unique=31 flat=512 nested=752
static const uint16_t g_emit_op_0[32] = {
    5,  120, 235, 350, 465, 580, 695, 810, 925, 1040, 5,
    10, 15,  20,  25,  30,  35,  40,  45,  50,  55,   60,
    65, 70,  75,  80,  85,  90,  95,  100, 105, 110};
inline uint16_t GetEmitOp0(size_t i) { return g_emit_op_0[i]; }
// max=37 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_1[2] = {32, 37};
inline uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_1[2] = {6, 7};
inline uint8_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=46 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_2[2] = {45, 46};
inline uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_2[2] = {6, 7};
inline uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=51 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_3[2] = {47, 51};
inline uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_3[2] = {6, 7};
inline uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=53 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_4[2] = {52, 53};
inline uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_4[2] = {6, 7};
inline uint8_t GetEmitOp4(size_t i) { return g_emit_op_4[i]; }
// max=55 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_5[2] = {54, 55};
inline uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_5[2] = {6, 7};
inline uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=57 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_6[2] = {56, 57};
inline uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_6[2] = {6, 7};
inline uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=65 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_7[2] = {61, 65};
inline uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_7[2] = {6, 7};
inline uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=98 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_8[2] = {95, 98};
inline uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_8[2] = {6, 7};
inline uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=102 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_9[2] = {100, 102};
inline uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_9[2] = {6, 7};
inline uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=104 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_10[2] = {103, 104};
inline uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_10[2] = {6, 7};
inline uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=109 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_11[2] = {108, 109};
inline uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_11[2] = {6, 7};
inline uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=112 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_12[2] = {110, 112};
inline uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_12[2] = {6, 7};
inline uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=117 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_13[2] = {114, 117};
inline uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=7 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_13[2] = {6, 7};
inline uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=68 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_14[4] = {58, 66, 67, 68};
inline uint8_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_14[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
// max=72 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_15[4] = {69, 70, 71, 72};
inline uint8_t GetEmitBuffer15(size_t i) { return g_emit_buffer_15[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_15[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp15(size_t i) { return g_emit_op_15[i]; }
// max=76 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_16[4] = {73, 74, 75, 76};
inline uint8_t GetEmitBuffer16(size_t i) { return g_emit_buffer_16[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_16[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp16(size_t i) { return g_emit_op_16[i]; }
// max=80 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_17[4] = {77, 78, 79, 80};
inline uint8_t GetEmitBuffer17(size_t i) { return g_emit_buffer_17[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_17[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp17(size_t i) { return g_emit_op_17[i]; }
// max=84 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_18[4] = {81, 82, 83, 84};
inline uint8_t GetEmitBuffer18(size_t i) { return g_emit_buffer_18[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_18[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp18(size_t i) { return g_emit_op_18[i]; }
// max=89 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_19[4] = {85, 86, 87, 89};
inline uint8_t GetEmitBuffer19(size_t i) { return g_emit_buffer_19[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_19[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp19(size_t i) { return g_emit_op_19[i]; }
// max=118 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_20[4] = {106, 107, 113, 118};
inline uint8_t GetEmitBuffer20(size_t i) { return g_emit_buffer_20[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_20[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp20(size_t i) { return g_emit_op_20[i]; }
// max=122 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_21[4] = {119, 120, 121, 122};
inline uint8_t GetEmitBuffer21(size_t i) { return g_emit_buffer_21[i]; }
// max=13 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_21[4] = {7, 9, 11, 13};
inline uint8_t GetEmitOp21(size_t i) { return g_emit_op_21[i]; }
// max=90 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_22[11] = {38, 42, 44, 59, 88, 90,
                                             33, 34, 40, 41, 63};
inline uint8_t GetEmitBuffer22(size_t i) { return g_emit_buffer_22[i]; }
// max=210 unique=14 flat=256 nested=368
static const uint8_t g_emit_op_22[32] = {
    8,  8,  8,  8,  28,  28,  28,  28,  48,  48,  48,  48,  68,  68, 68, 68,
    88, 88, 88, 88, 108, 108, 108, 108, 130, 150, 170, 190, 210, 5,  10, 15};
inline uint8_t GetEmitOp22(size_t i) { return g_emit_op_22[i]; }
// max=43 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_23[2] = {39, 43};
inline uint8_t GetEmitBuffer23(size_t i) { return g_emit_buffer_23[i]; }
// max=12 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_23[2] = {11, 12};
inline uint8_t GetEmitOp23(size_t i) { return g_emit_op_23[i]; }
// max=126 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_24[11] = {0,  36,  64, 91, 93, 126,
                                             94, 125, 60, 96, 123};
inline uint8_t GetEmitBuffer24(size_t i) { return g_emit_buffer_24[i]; }
// max=115 unique=12 flat=256 nested=352
static const uint8_t g_emit_op_24[32] = {
    13, 13, 13, 13, 23, 23, 23, 23, 33, 33, 33, 33, 43, 43,  43,  43,
    53, 53, 53, 53, 63, 63, 63, 63, 74, 74, 84, 84, 95, 105, 115, 5};
inline uint8_t GetEmitOp24(size_t i) { return g_emit_op_24[i]; }
// max=226 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_25[11] = {92,  195, 208, 128, 130, 131,
                                             162, 184, 194, 224, 226};
inline uint8_t GetEmitBuffer25(size_t i) { return g_emit_buffer_25[i]; }
// max=970 unique=29 flat=512 nested=720
static const uint16_t g_emit_op_25[32] = {
    19, 19, 114, 114, 209, 209, 305, 400, 495, 590, 685, 780, 875, 970, 5,  10,
    15, 20, 25,  30,  35,  40,  45,  50,  55,  60,  65,  70,  75,  80,  85, 90};
inline uint16_t GetEmitOp25(size_t i) { return g_emit_op_25[i]; }
// max=161 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_26[2] = {153, 161};
inline uint8_t GetEmitBuffer26(size_t i) { return g_emit_buffer_26[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_26[2] = {21, 22};
inline uint8_t GetEmitOp26(size_t i) { return g_emit_op_26[i]; }
// max=172 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_27[2] = {167, 172};
inline uint8_t GetEmitBuffer27(size_t i) { return g_emit_buffer_27[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_27[2] = {21, 22};
inline uint8_t GetEmitOp27(size_t i) { return g_emit_op_27[i]; }
// max=177 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_28[2] = {176, 177};
inline uint8_t GetEmitBuffer28(size_t i) { return g_emit_buffer_28[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_28[2] = {21, 22};
inline uint8_t GetEmitOp28(size_t i) { return g_emit_op_28[i]; }
// max=209 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_29[2] = {179, 209};
inline uint8_t GetEmitBuffer29(size_t i) { return g_emit_buffer_29[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_29[2] = {21, 22};
inline uint8_t GetEmitOp29(size_t i) { return g_emit_op_29[i]; }
// max=217 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_30[2] = {216, 217};
inline uint8_t GetEmitBuffer30(size_t i) { return g_emit_buffer_30[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_30[2] = {21, 22};
inline uint8_t GetEmitOp30(size_t i) { return g_emit_op_30[i]; }
// max=229 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_31[2] = {227, 229};
inline uint8_t GetEmitBuffer31(size_t i) { return g_emit_buffer_31[i]; }
// max=22 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_31[2] = {21, 22};
inline uint8_t GetEmitOp31(size_t i) { return g_emit_op_31[i]; }
// max=146 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_32[4] = {133, 134, 136, 146};
inline uint8_t GetEmitBuffer32(size_t i) { return g_emit_buffer_32[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_32[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp32(size_t i) { return g_emit_op_32[i]; }
// max=163 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_33[4] = {154, 156, 160, 163};
inline uint8_t GetEmitBuffer33(size_t i) { return g_emit_buffer_33[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_33[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp33(size_t i) { return g_emit_op_33[i]; }
// max=173 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_34[4] = {164, 169, 170, 173};
inline uint8_t GetEmitBuffer34(size_t i) { return g_emit_buffer_34[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_34[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp34(size_t i) { return g_emit_op_34[i]; }
// max=186 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_35[4] = {178, 181, 185, 186};
inline uint8_t GetEmitBuffer35(size_t i) { return g_emit_buffer_35[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_35[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp35(size_t i) { return g_emit_op_35[i]; }
// max=196 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_36[4] = {187, 189, 190, 196};
inline uint8_t GetEmitBuffer36(size_t i) { return g_emit_buffer_36[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_36[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp36(size_t i) { return g_emit_op_36[i]; }
// max=233 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_37[4] = {198, 228, 232, 233};
inline uint8_t GetEmitBuffer37(size_t i) { return g_emit_buffer_37[i]; }
// max=28 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_37[4] = {22, 24, 26, 28};
inline uint8_t GetEmitOp37(size_t i) { return g_emit_op_37[i]; }
// max=143 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_38[8] = {1,   135, 137, 138,
                                            139, 140, 141, 143};
inline uint8_t GetEmitBuffer38(size_t i) { return g_emit_buffer_38[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_38[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp38(size_t i) { return g_emit_op_38[i]; }
// max=158 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_39[8] = {147, 149, 150, 151,
                                            152, 155, 157, 158};
inline uint8_t GetEmitBuffer39(size_t i) { return g_emit_buffer_39[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_39[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp39(size_t i) { return g_emit_op_39[i]; }
// max=183 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_40[8] = {165, 166, 168, 174,
                                            175, 180, 182, 183};
inline uint8_t GetEmitBuffer40(size_t i) { return g_emit_buffer_40[i]; }
// max=44 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_40[8] = {23, 26, 29, 32, 35, 38, 41, 44};
inline uint8_t GetEmitOp40(size_t i) { return g_emit_op_40[i]; }
// max=230 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_41[3] = {230, 129, 132};
inline uint8_t GetEmitBuffer41(size_t i) { return g_emit_buffer_41[i]; }
// max=26 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_41[4] = {21, 21, 24, 26};
inline uint8_t GetEmitOp41(size_t i) { return g_emit_op_41[i]; }
// max=239 unique=11 flat=88 nested=176
static const uint8_t g_emit_buffer_42[11] = {188, 191, 197, 231, 239, 9,
                                             142, 144, 145, 148, 159};
inline uint8_t GetEmitBuffer42(size_t i) { return g_emit_buffer_42[i]; }
// max=64 unique=11 flat=128 nested=216
// monotonic increasing
static const uint8_t g_emit_op_42[16] = {23, 23, 27, 27, 31, 31, 35, 35,
                                         39, 39, 44, 48, 52, 56, 60, 64};
inline uint8_t GetEmitOp42(size_t i) { return g_emit_op_42[i]; }
// max=237 unique=10 flat=80 nested=160
static const uint8_t g_emit_buffer_43[10] = {171, 206, 215, 225, 236,
                                             237, 199, 207, 234, 235};
inline uint8_t GetEmitBuffer43(size_t i) { return g_emit_buffer_43[i]; }
// max=790 unique=26 flat=512 nested=672
static const uint16_t g_emit_op_43[32] = {
    24,  24,  109, 109, 194, 194, 279, 279, 364, 364, 449,
    449, 535, 620, 705, 790, 5,   10,  15,  20,  25,  30,
    35,  40,  45,  50,  55,  60,  65,  70,  75,  80};
inline uint16_t GetEmitOp43(size_t i) { return g_emit_op_43[i]; }
// max=193 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_44[2] = {192, 193};
inline uint8_t GetEmitBuffer44(size_t i) { return g_emit_buffer_44[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_44[2] = {26, 27};
inline uint8_t GetEmitOp44(size_t i) { return g_emit_op_44[i]; }
// max=201 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_45[2] = {200, 201};
inline uint8_t GetEmitBuffer45(size_t i) { return g_emit_buffer_45[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_45[2] = {26, 27};
inline uint8_t GetEmitOp45(size_t i) { return g_emit_op_45[i]; }
// max=205 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_46[2] = {202, 205};
inline uint8_t GetEmitBuffer46(size_t i) { return g_emit_buffer_46[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_46[2] = {26, 27};
inline uint8_t GetEmitOp46(size_t i) { return g_emit_op_46[i]; }
// max=213 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_47[2] = {210, 213};
inline uint8_t GetEmitBuffer47(size_t i) { return g_emit_buffer_47[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_47[2] = {26, 27};
inline uint8_t GetEmitOp47(size_t i) { return g_emit_op_47[i]; }
// max=219 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_48[2] = {218, 219};
inline uint8_t GetEmitBuffer48(size_t i) { return g_emit_buffer_48[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_48[2] = {26, 27};
inline uint8_t GetEmitOp48(size_t i) { return g_emit_op_48[i]; }
// max=240 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_49[2] = {238, 240};
inline uint8_t GetEmitBuffer49(size_t i) { return g_emit_buffer_49[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_49[2] = {26, 27};
inline uint8_t GetEmitOp49(size_t i) { return g_emit_op_49[i]; }
// max=243 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_buffer_50[2] = {242, 243};
inline uint8_t GetEmitBuffer50(size_t i) { return g_emit_buffer_50[i]; }
// max=27 unique=2 flat=16 nested=32
// monotonic increasing
static const uint8_t g_emit_op_50[2] = {26, 27};
inline uint8_t GetEmitOp50(size_t i) { return g_emit_op_50[i]; }
// max=221 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_51[4] = {211, 212, 214, 221};
inline uint8_t GetEmitBuffer51(size_t i) { return g_emit_buffer_51[i]; }
// max=33 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_51[4] = {27, 29, 31, 33};
inline uint8_t GetEmitOp51(size_t i) { return g_emit_op_51[i]; }
// max=244 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_52[4] = {222, 223, 241, 244};
inline uint8_t GetEmitBuffer52(size_t i) { return g_emit_buffer_52[i]; }
// max=33 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_52[4] = {27, 29, 31, 33};
inline uint8_t GetEmitOp52(size_t i) { return g_emit_op_52[i]; }
// max=248 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_53[4] = {245, 246, 247, 248};
inline uint8_t GetEmitBuffer53(size_t i) { return g_emit_buffer_53[i]; }
// max=33 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_53[4] = {27, 29, 31, 33};
inline uint8_t GetEmitOp53(size_t i) { return g_emit_op_53[i]; }
// max=253 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_buffer_54[4] = {250, 251, 252, 253};
inline uint8_t GetEmitBuffer54(size_t i) { return g_emit_buffer_54[i]; }
// max=33 unique=4 flat=32 nested=64
// monotonic increasing
static const uint8_t g_emit_op_54[4] = {27, 29, 31, 33};
inline uint8_t GetEmitOp54(size_t i) { return g_emit_op_54[i]; }
// max=18 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_55[8] = {8, 11, 12, 14, 15, 16, 17, 18};
inline uint8_t GetEmitBuffer55(size_t i) { return g_emit_buffer_55[i]; }
// max=49 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_55[8] = {28, 31, 34, 37, 40, 43, 46, 49};
inline uint8_t GetEmitOp55(size_t i) { return g_emit_op_55[i]; }
// max=27 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_buffer_56[8] = {19, 20, 21, 23, 24, 25, 26, 27};
inline uint8_t GetEmitBuffer56(size_t i) { return g_emit_buffer_56[i]; }
// max=49 unique=8 flat=64 nested=128
// monotonic increasing
static const uint8_t g_emit_op_56[8] = {28, 31, 34, 37, 40, 43, 46, 49};
inline uint8_t GetEmitOp56(size_t i) { return g_emit_op_56[i]; }
// max=255 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_57[3] = {255, 203, 204};
inline uint8_t GetEmitBuffer57(size_t i) { return g_emit_buffer_57[i]; }
// max=31 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_57[4] = {26, 26, 29, 31};
inline uint8_t GetEmitOp57(size_t i) { return g_emit_op_57[i]; }
// max=254 unique=7 flat=56 nested=112
static const uint8_t g_emit_buffer_58[7] = {254, 2, 3, 4, 5, 6, 7};
inline uint8_t GetEmitBuffer58(size_t i) { return g_emit_buffer_58[i]; }
// max=46 unique=7 flat=64 nested=120
// monotonic increasing
static const uint8_t g_emit_op_58[8] = {27, 27, 31, 34, 37, 40, 43, 46};
inline uint8_t GetEmitOp58(size_t i) { return g_emit_op_58[i]; }
// max=256 unique=11 flat=176 nested=264
static const uint16_t g_emit_buffer_59[11] = {28,  29, 30, 31, 127, 220,
                                              249, 10, 13, 22, 256};
inline uint16_t GetEmitBuffer59(size_t i) { return g_emit_buffer_59[i]; }
// max=80 unique=11 flat=256 nested=344
// monotonic increasing
static const uint8_t g_emit_op_59[32] = {
    28, 28, 28, 28, 33, 33, 33, 33, 38, 38, 38, 38, 43, 43, 43, 43,
    48, 48, 48, 48, 53, 53, 53, 53, 58, 58, 58, 58, 65, 70, 75, 80};
inline uint8_t GetEmitOp59(size_t i) { return g_emit_op_59[i]; }
// max=124 unique=3 flat=24 nested=48
static const uint8_t g_emit_buffer_60[3] = {124, 35, 62};
inline uint8_t GetEmitBuffer60(size_t i) { return g_emit_buffer_60[i]; }
// max=16 unique=3 flat=32 nested=56
// monotonic increasing
static const uint8_t g_emit_op_60[4] = {11, 11, 14, 16};
inline uint8_t GetEmitOp60(size_t i) { return g_emit_op_60[i]; }
template <typename F>
bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {
  uint64_t buffer = 0;
  uint64_t index;
  size_t emit_ofs;
  int buffer_len = 0;
  uint64_t op;
refill:
  while (buffer_len < 5) {
    if (begin == end) return buffer_len == 0;
    buffer <<= 8;
    buffer |= static_cast<uint64_t>(*begin++);
    buffer_len += 8;
  }
  index = buffer >> (buffer_len - 5);
  op = GetEmitOp0(index);
  buffer_len -= op % 5;
  op /= 5;
  emit_ofs = op / 23;
  switch (op % 23) {
    case 22: {
      // 0:f8/8,1:3ffd8/18,2:7fffe2/23,3:7fffe3/23,4:7fffe4/23,5:7fffe5/23,6:7fffe6/23,7:7fffe7/23,8:7fffe8/23,9:7ffea/19,10:1fffffc/25,11:7fffe9/23,12:7fffea/23,13:1fffffd/25,14:7fffeb/23,15:7fffec/23,16:7fffed/23,17:7fffee/23,18:7fffef/23,19:7ffff0/23,20:7ffff1/23,21:7ffff2/23,22:1fffffe/25,23:7ffff3/23,24:7ffff4/23,25:7ffff5/23,26:7ffff6/23,27:7ffff7/23,28:7ffff8/23,29:7ffff9/23,30:7ffffa/23,31:7ffffb/23,33:18/5,34:19/5,35:7a/7,36:f9/8,38:0/3,39:3a/6,40:1a/5,41:1b/5,42:1/3,43:3b/6,44:2/3,59:3/3,60:3fc/10,62:7b/7,63:1c/5,64:fa/8,88:4/3,90:5/3,91:fb/8,92:3ff0/14,93:fc/8,94:1fc/9,96:3fd/10,123:3fe/10,124:3c/6,125:1fd/9,126:fd/8,127:7ffffc/23,128:7fe6/15,129:1ffd2/17,130:7fe7/15,131:7fe8/15,132:1ffd3/17,133:1ffd4/17,134:1ffd5/17,135:3ffd9/18,136:1ffd6/17,137:3ffda/18,138:3ffdb/18,139:3ffdc/18,140:3ffdd/18,141:3ffde/18,142:7ffeb/19,143:3ffdf/18,144:7ffec/19,145:7ffed/19,146:1ffd7/17,147:3ffe0/18,148:7ffee/19,149:3ffe1/18,150:3ffe2/18,151:3ffe3/18,152:3ffe4/18,153:ffdc/16,154:1ffd8/17,155:3ffe5/18,156:1ffd9/17,157:3ffe6/18,158:3ffe7/18,159:7ffef/19,160:1ffda/17,161:ffdd/16,162:7fe9/15,163:1ffdb/17,164:1ffdc/17,165:3ffe8/18,166:3ffe9/18,167:ffde/16,168:3ffea/18,169:1ffdd/17,170:1ffde/17,171:7fff0/19,172:ffdf/16,173:1ffdf/17,174:3ffeb/18,175:3ffec/18,176:ffe0/16,177:ffe1/16,178:1ffe0/17,179:ffe2/16,180:3ffed/18,181:1ffe1/17,182:3ffee/18,183:3ffef/18,184:7fea/15,185:1ffe2/17,186:1ffe3/17,187:1ffe4/17,188:3fff0/18,189:1ffe5/17,190:1ffe6/17,191:3fff1/18,192:1fffe0/21,193:1fffe1/21,194:7feb/15,195:3ff1/14,196:1ffe7/17,197:3fff2/18,198:1ffe8/17,199:fffec/20,200:1fffe2/21,201:1fffe3/21,202:1fffe4/21,203:3fffde/22,204:3fffdf/22,205:1fffe5/21,206:7fff1/19,207:fffed/20,208:3ff2/14,209:ffe3/16,210:1fffe6/21,211:3fffe0/22,212:3fffe1/22,213:1fffe7/21,214:3fffe2/22,215:7fff2/19,216:ffe4/16,217:ffe5/16,218:1fffe8/21,219:1fffe9/21,220:7ffffd/23,221:3fffe3/22,222:3fffe4/22,223:3fffe5/22,224:7fec/15,225:7fff3/19,226:7fed/15,227:ffe6/16,228:1ffe9/17,229:ffe7/16,230:ffe8/16,231:3fff3/18,232:1ffea/17,233:1ffeb/17,234:fffee/20,235:fffef/20,236:7fff4/19,237:7fff5/19,238:1fffea/21,239:3fff4/18,240:1fffeb/21,241:3fffe6/22,242:1fffec/21,243:1fffed/21,244:3fffe7/22,245:3fffe8/22,246:3fffe9/22,247:3fffea/22,248:3fffeb/22,249:7ffffe/23,250:3fffec/22,251:3fffed/22,252:3fffee/22,253:3fffef/22,254:3ffff0/22,255:1fffee/21,256:1ffffff/25
      while (buffer_len < 5) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 5);
      op = GetEmitOp22(index);
      buffer_len -= op % 5;
      op /= 5;
      emit_ofs = op / 4;
      switch (op % 4) {
        case 3: {
          // 0:0/3,1:1fd8/13,2:3ffe2/18,3:3ffe3/18,4:3ffe4/18,5:3ffe5/18,6:3ffe6/18,7:3ffe7/18,8:3ffe8/18,9:3fea/14,10:ffffc/20,11:3ffe9/18,12:3ffea/18,13:ffffd/20,14:3ffeb/18,15:3ffec/18,16:3ffed/18,17:3ffee/18,18:3ffef/18,19:3fff0/18,20:3fff1/18,21:3fff2/18,22:ffffe/20,23:3fff3/18,24:3fff4/18,25:3fff5/18,26:3fff6/18,27:3fff7/18,28:3fff8/18,29:3fff9/18,30:3fffa/18,31:3fffb/18,36:1/3,60:1c/5,64:2/3,91:3/3,92:1f0/9,93:4/3,94:c/4,96:1d/5,123:1e/5,125:d/4,126:5/3,127:3fffc/18,128:3e6/10,129:fd2/12,130:3e7/10,131:3e8/10,132:fd3/12,133:fd4/12,134:fd5/12,135:1fd9/13,136:fd6/12,137:1fda/13,138:1fdb/13,139:1fdc/13,140:1fdd/13,141:1fde/13,142:3feb/14,143:1fdf/13,144:3fec/14,145:3fed/14,146:fd7/12,147:1fe0/13,148:3fee/14,149:1fe1/13,150:1fe2/13,151:1fe3/13,152:1fe4/13,153:7dc/11,154:fd8/12,155:1fe5/13,156:fd9/12,157:1fe6/13,158:1fe7/13,159:3fef/14,160:fda/12,161:7dd/11,162:3e9/10,163:fdb/12,164:fdc/12,165:1fe8/13,166:1fe9/13,167:7de/11,168:1fea/13,169:fdd/12,170:fde/12,171:3ff0/14,172:7df/11,173:fdf/12,174:1feb/13,175:1fec/13,176:7e0/11,177:7e1/11,178:fe0/12,179:7e2/11,180:1fed/13,181:fe1/12,182:1fee/13,183:1fef/13,184:3ea/10,185:fe2/12,186:fe3/12,187:fe4/12,188:1ff0/13,189:fe5/12,190:fe6/12,191:1ff1/13,192:ffe0/16,193:ffe1/16,194:3eb/10,195:1f1/9,196:fe7/12,197:1ff2/13,198:fe8/12,199:7fec/15,200:ffe2/16,201:ffe3/16,202:ffe4/16,203:1ffde/17,204:1ffdf/17,205:ffe5/16,206:3ff1/14,207:7fed/15,208:1f2/9,209:7e3/11,210:ffe6/16,211:1ffe0/17,212:1ffe1/17,213:ffe7/16,214:1ffe2/17,215:3ff2/14,216:7e4/11,217:7e5/11,218:ffe8/16,219:ffe9/16,220:3fffd/18,221:1ffe3/17,222:1ffe4/17,223:1ffe5/17,224:3ec/10,225:3ff3/14,226:3ed/10,227:7e6/11,228:fe9/12,229:7e7/11,230:7e8/11,231:1ff3/13,232:fea/12,233:feb/12,234:7fee/15,235:7fef/15,236:3ff4/14,237:3ff5/14,238:ffea/16,239:1ff4/13,240:ffeb/16,241:1ffe6/17,242:ffec/16,243:ffed/16,244:1ffe7/17,245:1ffe8/17,246:1ffe9/17,247:1ffea/17,248:1ffeb/17,249:3fffe/18,250:1ffec/17,251:1ffed/17,252:1ffee/17,253:1ffef/17,254:1fff0/17,255:ffee/16,256:fffff/20
          while (buffer_len < 5) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 5);
          op = GetEmitOp24(index);
          buffer_len -= op % 5;
          op /= 5;
          emit_ofs = op / 2;
          switch (op % 2) {
            case 1: {
              // 1:d8/8,2:1fe2/13,3:1fe3/13,4:1fe4/13,5:1fe5/13,6:1fe6/13,7:1fe7/13,8:1fe8/13,9:1ea/9,10:7ffc/15,11:1fe9/13,12:1fea/13,13:7ffd/15,14:1feb/13,15:1fec/13,16:1fed/13,17:1fee/13,18:1fef/13,19:1ff0/13,20:1ff1/13,21:1ff2/13,22:7ffe/15,23:1ff3/13,24:1ff4/13,25:1ff5/13,26:1ff6/13,27:1ff7/13,28:1ff8/13,29:1ff9/13,30:1ffa/13,31:1ffb/13,92:0/4,127:1ffc/13,128:6/5,129:52/7,130:7/5,131:8/5,132:53/7,133:54/7,134:55/7,135:d9/8,136:56/7,137:da/8,138:db/8,139:dc/8,140:dd/8,141:de/8,142:1eb/9,143:df/8,144:1ec/9,145:1ed/9,146:57/7,147:e0/8,148:1ee/9,149:e1/8,150:e2/8,151:e3/8,152:e4/8,153:1c/6,154:58/7,155:e5/8,156:59/7,157:e6/8,158:e7/8,159:1ef/9,160:5a/7,161:1d/6,162:9/5,163:5b/7,164:5c/7,165:e8/8,166:e9/8,167:1e/6,168:ea/8,169:5d/7,170:5e/7,171:1f0/9,172:1f/6,173:5f/7,174:eb/8,175:ec/8,176:20/6,177:21/6,178:60/7,179:22/6,180:ed/8,181:61/7,182:ee/8,183:ef/8,184:a/5,185:62/7,186:63/7,187:64/7,188:f0/8,189:65/7,190:66/7,191:f1/8,192:7e0/11,193:7e1/11,194:b/5,195:1/4,196:67/7,197:f2/8,198:68/7,199:3ec/10,200:7e2/11,201:7e3/11,202:7e4/11,203:fde/12,204:fdf/12,205:7e5/11,206:1f1/9,207:3ed/10,208:2/4,209:23/6,210:7e6/11,211:fe0/12,212:fe1/12,213:7e7/11,214:fe2/12,215:1f2/9,216:24/6,217:25/6,218:7e8/11,219:7e9/11,220:1ffd/13,221:fe3/12,222:fe4/12,223:fe5/12,224:c/5,225:1f3/9,226:d/5,227:26/6,228:69/7,229:27/6,230:28/6,231:f3/8,232:6a/7,233:6b/7,234:3ee/10,235:3ef/10,236:1f4/9,237:1f5/9,238:7ea/11,239:f4/8,240:7eb/11,241:fe6/12,242:7ec/11,243:7ed/11,244:fe7/12,245:fe8/12,246:fe9/12,247:fea/12,248:feb/12,249:1ffe/13,250:fec/12,251:fed/12,252:fee/12,253:fef/12,254:ff0/12,255:7ee/11,256:7fff/15
              while (buffer_len < 5) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 5);
              op = GetEmitOp25(index);
              buffer_len -= op % 5;
              op /= 5;
              emit_ofs = op / 19;
              switch (op % 19) {
                case 7: {
                  // 129:2/2,132:3/2,230:0/1
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp41(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer41(op + 0));
                  goto refill;
                }
                case 8: {
                  // 133:0/2,134:1/2,136:2/2,146:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp32(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer32(op + 0));
                  goto refill;
                }
                case 15: {
                  // 147:0/3,149:1/3,150:2/3,151:3/3,152:4/3,155:5/3,157:6/3,158:7/3
                  while (buffer_len < 3) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 3);
                  op = GetEmitOp39(index);
                  buffer_len -= op % 3;
                  sink(GetEmitBuffer39(op + 0));
                  goto refill;
                }
                case 1: {
                  // 153:0/1,161:1/1
                  while (buffer_len < 1) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 1);
                  op = GetEmitOp26(index);
                  buffer_len -= op % 1;
                  sink(GetEmitBuffer26(op + 0));
                  goto refill;
                }
                case 9: {
                  // 154:0/2,156:1/2,160:2/2,163:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp33(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer33(op + 0));
                  goto refill;
                }
                case 10: {
                  // 164:0/2,169:1/2,170:2/2,173:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp34(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer34(op + 0));
                  goto refill;
                }
                case 16: {
                  // 165:0/3,166:1/3,168:2/3,174:3/3,175:4/3,180:5/3,182:6/3,183:7/3
                  while (buffer_len < 3) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 3);
                  op = GetEmitOp40(index);
                  buffer_len -= op % 3;
                  sink(GetEmitBuffer40(op + 0));
                  goto refill;
                }
                case 2: {
                  // 167:0/1,172:1/1
                  while (buffer_len < 1) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 1);
                  op = GetEmitOp27(index);
                  buffer_len -= op % 1;
                  sink(GetEmitBuffer27(op + 0));
                  goto refill;
                }
                case 3: {
                  // 176:0/1,177:1/1
                  while (buffer_len < 1) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 1);
                  op = GetEmitOp28(index);
                  buffer_len -= op % 1;
                  sink(GetEmitBuffer28(op + 0));
                  goto refill;
                }
                case 11: {
                  // 178:0/2,181:1/2,185:2/2,186:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp35(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer35(op + 0));
                  goto refill;
                }
                case 4: {
                  // 179:0/1,209:1/1
                  while (buffer_len < 1) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 1);
                  op = GetEmitOp29(index);
                  buffer_len -= op % 1;
                  sink(GetEmitBuffer29(op + 0));
                  goto refill;
                }
                case 12: {
                  // 187:0/2,189:1/2,190:2/2,196:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp36(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer36(op + 0));
                  goto refill;
                }
                case 13: {
                  // 198:0/2,228:1/2,232:2/2,233:3/2
                  while (buffer_len < 2) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 2);
                  op = GetEmitOp37(index);
                  buffer_len -= op % 2;
                  sink(GetEmitBuffer37(op + 0));
                  goto refill;
                }
                case 14: {
                  // 1:0/3,135:1/3,137:2/3,138:3/3,139:4/3,140:5/3,141:6/3,143:7/3
                  while (buffer_len < 3) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 3);
                  op = GetEmitOp38(index);
                  buffer_len -= op % 3;
                  sink(GetEmitBuffer38(op + 0));
                  goto refill;
                }
                case 5: {
                  // 216:0/1,217:1/1
                  while (buffer_len < 1) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 1);
                  op = GetEmitOp30(index);
                  buffer_len -= op % 1;
                  sink(GetEmitBuffer30(op + 0));
                  goto refill;
                }
                case 6: {
                  // 227:0/1,229:1/1
                  while (buffer_len < 1) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 1);
                  op = GetEmitOp31(index);
                  buffer_len -= op % 1;
                  sink(GetEmitBuffer31(op + 0));
                  goto refill;
                }
                case 18: {
                  // 2:e2/8,3:e3/8,4:e4/8,5:e5/8,6:e6/8,7:e7/8,8:e8/8,10:3fc/10,11:e9/8,12:ea/8,13:3fd/10,14:eb/8,15:ec/8,16:ed/8,17:ee/8,18:ef/8,19:f0/8,20:f1/8,21:f2/8,22:3fe/10,23:f3/8,24:f4/8,25:f5/8,26:f6/8,27:f7/8,28:f8/8,29:f9/8,30:fa/8,31:fb/8,127:fc/8,171:0/4,192:20/6,193:21/6,199:c/5,200:22/6,201:23/6,202:24/6,203:5e/7,204:5f/7,205:25/6,206:1/4,207:d/5,210:26/6,211:60/7,212:61/7,213:27/6,214:62/7,215:2/4,218:28/6,219:29/6,220:fd/8,221:63/7,222:64/7,223:65/7,225:3/4,234:e/5,235:f/5,236:4/4,237:5/4,238:2a/6,240:2b/6,241:66/7,242:2c/6,243:2d/6,244:67/7,245:68/7,246:69/7,247:6a/7,248:6b/7,249:fe/8,250:6c/7,251:6d/7,252:6e/7,253:6f/7,254:70/7,255:2e/6,256:3ff/10
                  while (buffer_len < 5) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 5);
                  op = GetEmitOp43(index);
                  buffer_len -= op % 5;
                  op /= 5;
                  emit_ofs = op / 17;
                  switch (op % 17) {
                    case 16: {
                      // 10:1c/5,13:1d/5,22:1e/5,28:0/3,29:1/3,30:2/3,31:3/3,127:4/3,220:5/3,249:6/3,256:1f/5
                      while (buffer_len < 5) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 5);
                      op = GetEmitOp59(index);
                      buffer_len -= op % 5;
                      sink(GetEmitBuffer59(op + 0));
                      goto refill;
                    }
                    case 1: {
                      // 192:0/1,193:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp44(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer44(op + 0));
                      goto refill;
                    }
                    case 15: {
                      // 19:0/3,20:1/3,21:2/3,23:3/3,24:4/3,25:5/3,26:6/3,27:7/3
                      while (buffer_len < 3) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 3);
                      op = GetEmitOp56(index);
                      buffer_len -= op % 3;
                      sink(GetEmitBuffer56(op + 0));
                      goto refill;
                    }
                    case 2: {
                      // 200:0/1,201:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp45(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer45(op + 0));
                      goto refill;
                    }
                    case 3: {
                      // 202:0/1,205:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp46(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer46(op + 0));
                      goto refill;
                    }
                    case 8: {
                      // 203:2/2,204:3/2,255:0/1
                      while (buffer_len < 2) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 2);
                      op = GetEmitOp57(index);
                      buffer_len -= op % 2;
                      sink(GetEmitBuffer57(op + 0));
                      goto refill;
                    }
                    case 4: {
                      // 210:0/1,213:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp47(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer47(op + 0));
                      goto refill;
                    }
                    case 9: {
                      // 211:0/2,212:1/2,214:2/2,221:3/2
                      while (buffer_len < 2) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 2);
                      op = GetEmitOp51(index);
                      buffer_len -= op % 2;
                      sink(GetEmitBuffer51(op + 0));
                      goto refill;
                    }
                    case 5: {
                      // 218:0/1,219:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp48(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer48(op + 0));
                      goto refill;
                    }
                    case 10: {
                      // 222:0/2,223:1/2,241:2/2,244:3/2
                      while (buffer_len < 2) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 2);
                      op = GetEmitOp52(index);
                      buffer_len -= op % 2;
                      sink(GetEmitBuffer52(op + 0));
                      goto refill;
                    }
                    case 6: {
                      // 238:0/1,240:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp49(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer49(op + 0));
                      goto refill;
                    }
                    case 7: {
                      // 242:0/1,243:1/1
                      while (buffer_len < 1) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 1);
                      op = GetEmitOp50(index);
                      buffer_len -= op % 1;
                      sink(GetEmitBuffer50(op + 0));
                      goto refill;
                    }
                    case 11: {
                      // 245:0/2,246:1/2,247:2/2,248:3/2
                      while (buffer_len < 2) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 2);
                      op = GetEmitOp53(index);
                      buffer_len -= op % 2;
                      sink(GetEmitBuffer53(op + 0));
                      goto refill;
                    }
                    case 12: {
                      // 250:0/2,251:1/2,252:2/2,253:3/2
                      while (buffer_len < 2) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 2);
                      op = GetEmitOp54(index);
                      buffer_len -= op % 2;
                      sink(GetEmitBuffer54(op + 0));
                      goto refill;
                    }
                    case 13: {
                      // 2:2/3,3:3/3,4:4/3,5:5/3,6:6/3,7:7/3,254:0/2
                      while (buffer_len < 3) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 3);
                      op = GetEmitOp58(index);
                      buffer_len -= op % 3;
                      sink(GetEmitBuffer58(op + 0));
                      goto refill;
                    }
                    case 14: {
                      // 8:0/3,11:1/3,12:2/3,14:3/3,15:4/3,16:5/3,17:6/3,18:7/3
                      while (buffer_len < 3) {
                        if (begin == end) return buffer_len == 0;
                        buffer <<= 8;
                        buffer |= static_cast<uint64_t>(*begin++);
                        buffer_len += 8;
                      }
                      index = buffer >> (buffer_len - 3);
                      op = GetEmitOp55(index);
                      buffer_len -= op % 3;
                      sink(GetEmitBuffer55(op + 0));
                      goto refill;
                    }
                    case 0: {
                      sink(GetEmitBuffer43(emit_ofs + 0));
                      goto refill;
                    }
                  }
                }
                case 17: {
                  // 9:a/4,142:b/4,144:c/4,145:d/4,148:e/4,159:f/4,188:0/3,191:1/3,197:2/3,231:3/3,239:4/3
                  while (buffer_len < 4) {
                    if (begin == end) return buffer_len == 0;
                    buffer <<= 8;
                    buffer |= static_cast<uint64_t>(*begin++);
                    buffer_len += 8;
                  }
                  index = buffer >> (buffer_len - 4);
                  op = GetEmitOp42(index);
                  buffer_len -= op % 4;
                  sink(GetEmitBuffer42(op + 0));
                  goto refill;
                }
                case 0: {
                  sink(GetEmitBuffer25(emit_ofs + 0));
                  goto refill;
                }
              }
            }
            case 0: {
              sink(GetEmitBuffer24(emit_ofs + 0));
              goto refill;
            }
          }
        }
        case 2: {
          // 35:2/2,62:3/2,124:0/1
          while (buffer_len < 2) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 2);
          op = GetEmitOp60(index);
          buffer_len -= op % 2;
          sink(GetEmitBuffer60(op + 0));
          goto refill;
        }
        case 1: {
          // 39:0/1,43:1/1
          while (buffer_len < 1) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 1);
          op = GetEmitOp23(index);
          buffer_len -= op % 1;
          sink(GetEmitBuffer23(op + 0));
          goto refill;
        }
        case 0: {
          sink(GetEmitBuffer22(emit_ofs + 0));
          goto refill;
        }
      }
    }
    case 9: {
      // 100:0/1,102:1/1
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
    case 10: {
      // 103:0/1,104:1/1
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
    case 20: {
      // 106:0/2,107:1/2,113:2/2,118:3/2
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
    case 11: {
      // 108:0/1,109:1/1
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
    case 12: {
      // 110:0/1,112:1/1
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
    case 13: {
      // 114:0/1,117:1/1
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
    case 21: {
      // 119:0/2,120:1/2,121:2/2,122:3/2
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
    case 1: {
      // 32:0/1,37:1/1
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
      // 45:0/1,46:1/1
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
      // 47:0/1,51:1/1
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
    case 4: {
      // 52:0/1,53:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp4(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer4(op + 0));
      goto refill;
    }
    case 5: {
      // 54:0/1,55:1/1
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
    case 6: {
      // 56:0/1,57:1/1
      while (buffer_len < 1) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 1);
      op = GetEmitOp6(index);
      buffer_len -= op % 1;
      sink(GetEmitBuffer6(op + 0));
      goto refill;
    }
    case 14: {
      // 58:0/2,66:1/2,67:2/2,68:3/2
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp14(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer14(op + 0));
      goto refill;
    }
    case 7: {
      // 61:0/1,65:1/1
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
    case 15: {
      // 69:0/2,70:1/2,71:2/2,72:3/2
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp15(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer15(op + 0));
      goto refill;
    }
    case 16: {
      // 73:0/2,74:1/2,75:2/2,76:3/2
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp16(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer16(op + 0));
      goto refill;
    }
    case 17: {
      // 77:0/2,78:1/2,79:2/2,80:3/2
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp17(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer17(op + 0));
      goto refill;
    }
    case 18: {
      // 81:0/2,82:1/2,83:2/2,84:3/2
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp18(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer18(op + 0));
      goto refill;
    }
    case 19: {
      // 85:0/2,86:1/2,87:2/2,89:3/2
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      op = GetEmitOp19(index);
      buffer_len -= op % 2;
      sink(GetEmitBuffer19(op + 0));
      goto refill;
    }
    case 8: {
      // 95:0/1,98:1/1
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
    case 0: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      goto refill;
    }
  }
  abort();
}
