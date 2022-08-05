#include <cstddef>
#include <cstdint>
// max=122 unique=74 flat=592 nested=1184
static const uint8_t g_emit_buffer_0[74] = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32, 37,  45,  46,  47,
    51,  52,  53,  54,  55,  56,  57,  61,  65,  95,  98, 100, 102, 103, 104,
    108, 109, 110, 112, 114, 117, 58,  66,  67,  68,  69, 70,  71,  72,  73,
    74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84, 85,  86,  87,  89,
    106, 107, 113, 118, 119, 120, 121, 122, 38,  42,  44, 59,  88,  90};
uint8_t GetEmitBuffer0(size_t i) { return g_emit_buffer_0[i]; }
// max=441 unique=76 flat=4096 nested=3264
static const uint8_t g_emit_op_0_outer[256] = {
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
static const uint16_t g_emit_op_0_inner[76] = {
    0,   6,   12,  18,  24,  30,  36,  42,  48,  54,  61,  67,  73,
    79,  85,  91,  97,  103, 109, 115, 121, 127, 133, 139, 145, 151,
    157, 163, 169, 175, 181, 187, 193, 199, 205, 211, 218, 224, 230,
    236, 242, 248, 254, 260, 266, 272, 278, 284, 290, 296, 302, 308,
    314, 320, 326, 332, 338, 344, 350, 356, 362, 368, 374, 380, 386,
    392, 398, 404, 411, 417, 423, 429, 435, 441, 4,   5};
uint16_t GetEmitOp0(size_t i) {
  return g_emit_op_0_inner[g_emit_op_0_outer[i]];
}
// max=41 unique=4 flat=32 nested=64
static const uint8_t g_emit_buffer_1[4] = {33, 34, 40, 41};
uint8_t GetEmitBuffer1(size_t i) { return g_emit_buffer_1[i]; }
// max=3 unique=4 flat=32 nested=64
static const uint8_t g_emit_op_1[4] = {0, 1, 2, 3};
uint8_t GetEmitOp1(size_t i) { return g_emit_op_1[i]; }
// max=126 unique=17 flat=136 nested=272
static const uint8_t g_emit_buffer_2[17] = {
    63, 39, 43, 124, 35, 62, 0, 36, 64, 91, 93, 126, 94, 125, 60, 96, 123};
uint8_t GetEmitBuffer2(size_t i) { return g_emit_buffer_2[i]; }
// max=133 unique=19 flat=2048 nested=2200
static const uint8_t g_emit_op_2[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,
    9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   9,
    9,   9,   9,   9,   9,   9,   17,  17,  17,  17,  17,  17,  17,  17,  17,
    17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,
    17,  17,  17,  17,  17,  17,  17,  17,  25,  25,  25,  25,  25,  25,  25,
    25,  25,  25,  25,  25,  25,  25,  25,  25,  25,  25,  25,  25,  25,  25,
    25,  25,  25,  25,  25,  25,  25,  25,  25,  25,  34,  34,  34,  34,  34,
    34,  34,  34,  34,  34,  34,  34,  34,  34,  34,  34,  42,  42,  42,  42,
    42,  42,  42,  42,  42,  42,  42,  42,  42,  42,  42,  42,  51,  51,  51,
    51,  51,  51,  51,  51,  59,  59,  59,  59,  59,  59,  59,  59,  67,  67,
    67,  67,  67,  67,  67,  67,  75,  75,  75,  75,  75,  75,  75,  75,  83,
    83,  83,  83,  83,  83,  83,  83,  91,  91,  91,  91,  91,  91,  91,  91,
    100, 100, 100, 100, 108, 108, 108, 108, 117, 117, 125, 125, 133, 133, 6,
    7};
uint8_t GetEmitOp2(size_t i) { return g_emit_op_2[i]; }
// max=226 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_3[15] = {
    92, 195, 208, 128, 130, 131, 162, 184, 194, 224, 226, 153, 161, 167, 172};
uint8_t GetEmitBuffer3(size_t i) { return g_emit_buffer_3[i]; }
// max=44 unique=15 flat=256 nested=376
static const uint8_t g_emit_op_3[32] = {
    0,  0,  0,  0,  3,  3,  3,  3,  6,  6,  6,  6,  10, 10, 13, 13,
    16, 16, 19, 19, 22, 22, 25, 25, 28, 28, 31, 31, 35, 38, 41, 44};
uint8_t GetEmitOp3(size_t i) { return g_emit_op_3[i]; }
// max=239 unique=76 flat=608 nested=1216
static const uint8_t g_emit_buffer_4[76] = {
    176, 177, 179, 209, 216, 217, 227, 229, 230, 129, 132, 133, 134,
    136, 146, 154, 156, 160, 163, 164, 169, 170, 173, 178, 181, 185,
    186, 187, 189, 190, 196, 198, 228, 232, 233, 1,   135, 137, 138,
    139, 140, 141, 143, 147, 149, 150, 151, 152, 155, 157, 158, 165,
    166, 168, 174, 175, 180, 182, 183, 188, 191, 197, 231, 239, 9,
    142, 144, 145, 148, 159, 171, 206, 215, 225, 236, 237};
uint8_t GetEmitBuffer4(size_t i) { return g_emit_buffer_4[i]; }
// max=1053 unique=86 flat=4096 nested=3424
static const uint8_t g_emit_op_4_outer[256] = {
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
static const uint16_t g_emit_op_4_inner[86] = {
    0,   14,  28,  42,  56,  70,  84,  98,   112,  127,  141,  155, 169,
    183, 197, 211, 225, 239, 253, 267, 281,  295,  309,  323,  337, 351,
    365, 379, 393, 407, 421, 435, 449, 463,  477,  492,  506,  520, 534,
    548, 562, 576, 590, 604, 618, 632, 646,  660,  674,  688,  702, 716,
    730, 744, 758, 772, 786, 800, 814, 828,  842,  856,  870,  884, 899,
    913, 927, 941, 955, 969, 983, 997, 1011, 1025, 1039, 1053, 4,   5,
    6,   7,   8,   9,   10,  11,  12,  13};
uint16_t GetEmitOp4(size_t i) {
  return g_emit_op_4_inner[g_emit_op_4_outer[i]];
}
// max=207 unique=2 flat=16 nested=32
static const uint8_t g_emit_buffer_5[2] = {199, 207};
uint8_t GetEmitBuffer5(size_t i) { return g_emit_buffer_5[i]; }
// max=1 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_5[2] = {0, 1};
uint8_t GetEmitOp5(size_t i) { return g_emit_op_5[i]; }
// max=235 unique=2 flat=16 nested=32
static const uint8_t g_emit_buffer_6[2] = {234, 235};
uint8_t GetEmitBuffer6(size_t i) { return g_emit_buffer_6[i]; }
// max=1 unique=2 flat=16 nested=32
static const uint8_t g_emit_op_6[2] = {0, 1};
uint8_t GetEmitOp6(size_t i) { return g_emit_op_6[i]; }
// max=201 unique=4 flat=32 nested=64
static const uint8_t g_emit_buffer_7[4] = {192, 193, 200, 201};
uint8_t GetEmitBuffer7(size_t i) { return g_emit_buffer_7[i]; }
// max=3 unique=4 flat=32 nested=64
static const uint8_t g_emit_op_7[4] = {0, 1, 2, 3};
uint8_t GetEmitOp7(size_t i) { return g_emit_op_7[i]; }
// max=213 unique=4 flat=32 nested=64
static const uint8_t g_emit_buffer_8[4] = {202, 205, 210, 213};
uint8_t GetEmitBuffer8(size_t i) { return g_emit_buffer_8[i]; }
// max=3 unique=4 flat=32 nested=64
static const uint8_t g_emit_op_8[4] = {0, 1, 2, 3};
uint8_t GetEmitOp8(size_t i) { return g_emit_op_8[i]; }
// max=240 unique=4 flat=32 nested=64
static const uint8_t g_emit_buffer_9[4] = {218, 219, 238, 240};
uint8_t GetEmitBuffer9(size_t i) { return g_emit_buffer_9[i]; }
// max=3 unique=4 flat=32 nested=64
static const uint8_t g_emit_op_9[4] = {0, 1, 2, 3};
uint8_t GetEmitOp9(size_t i) { return g_emit_op_9[i]; }
// max=244 unique=8 flat=64 nested=128
static const uint8_t g_emit_buffer_10[8] = {211, 212, 214, 221,
                                            222, 223, 241, 244};
uint8_t GetEmitBuffer10(size_t i) { return g_emit_buffer_10[i]; }
// max=7 unique=8 flat=64 nested=128
static const uint8_t g_emit_op_10[8] = {0, 1, 2, 3, 4, 5, 6, 7};
uint8_t GetEmitOp10(size_t i) { return g_emit_op_10[i]; }
// max=253 unique=8 flat=64 nested=128
static const uint8_t g_emit_buffer_11[8] = {245, 246, 247, 248,
                                            250, 251, 252, 253};
uint8_t GetEmitBuffer11(size_t i) { return g_emit_buffer_11[i]; }
// max=7 unique=8 flat=64 nested=128
static const uint8_t g_emit_op_11[8] = {0, 1, 2, 3, 4, 5, 6, 7};
uint8_t GetEmitOp11(size_t i) { return g_emit_op_11[i]; }
// max=254 unique=15 flat=120 nested=240
static const uint8_t g_emit_buffer_12[15] = {254, 2,  3,  4,  5,  6,  7, 8,
                                             11,  12, 14, 15, 16, 17, 18};
uint8_t GetEmitBuffer12(size_t i) { return g_emit_buffer_12[i]; }
// max=29 unique=15 flat=128 nested=248
static const uint8_t g_emit_op_12[16] = {0,  0,  3,  5,  7,  9,  11, 13,
                                         15, 17, 19, 21, 23, 25, 27, 29};
uint8_t GetEmitOp12(size_t i) { return g_emit_op_12[i]; }
// max=255 unique=5 flat=40 nested=80
static const uint8_t g_emit_buffer_13[5] = {242, 243, 255, 203, 204};
uint8_t GetEmitBuffer13(size_t i) { return g_emit_buffer_13[i]; }
// max=9 unique=5 flat=64 nested=104
static const uint8_t g_emit_op_13[8] = {0, 0, 2, 2, 4, 4, 7, 9};
uint8_t GetEmitOp13(size_t i) { return g_emit_op_13[i]; }
// max=256 unique=19 flat=304 nested=456
static const uint16_t g_emit_buffer_14[19] = {19,  20, 21, 23, 24, 25,  26,
                                              27,  28, 29, 30, 31, 127, 220,
                                              249, 10, 13, 22, 256};
uint16_t GetEmitBuffer14(size_t i) { return g_emit_buffer_14[i]; }
// max=37 unique=19 flat=512 nested=664
static const uint8_t g_emit_op_14[64] = {
    0,  0,  0,  0,  2,  2,  2,  2,  4,  4,  4,  4,  6,  6,  6,  6,
    8,  8,  8,  8,  10, 10, 10, 10, 12, 12, 12, 12, 14, 14, 14, 14,
    16, 16, 16, 16, 18, 18, 18, 18, 20, 20, 20, 20, 22, 22, 22, 22,
    24, 24, 24, 24, 26, 26, 26, 26, 28, 28, 28, 28, 31, 33, 35, 37};
uint8_t GetEmitOp14(size_t i) { return g_emit_op_14[i]; }
template <typename F>
bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {
  uint64_t buffer = 0;
  uint64_t index;
  size_t emit_ofs;
  int buffer_len = 0;
refill:
  while (buffer_len < 8) {
    if (begin == end) return buffer_len == 0;
    buffer <<= 8;
    buffer |= static_cast<uint64_t>(*begin++);
    buffer_len += 8;
  }
  index = buffer >> (buffer_len - 8);
  emit_ofs = GetEmitOp0(index) / 6;
  switch (GetEmitOp0(index) % 6) {
    case 5: {
      // 0:18/5,1:7fd8/15,2:fffe2/20,3:fffe3/20,4:fffe4/20,5:fffe5/20,6:fffe6/20,7:fffe7/20,8:fffe8/20,9:ffea/16,10:3ffffc/22,11:fffe9/20,12:fffea/20,13:3ffffd/22,14:fffeb/20,15:fffec/20,16:fffed/20,17:fffee/20,18:fffef/20,19:ffff0/20,20:ffff1/20,21:ffff2/20,22:3ffffe/22,23:ffff3/20,24:ffff4/20,25:ffff5/20,26:ffff6/20,27:ffff7/20,28:ffff8/20,29:ffff9/20,30:ffffa/20,31:ffffb/20,35:a/4,36:19/5,39:2/3,43:3/3,60:7c/7,62:b/4,63:0/2,64:1a/5,91:1b/5,92:7f0/11,93:1c/5,94:3c/6,96:7d/7,123:7e/7,124:4/3,125:3d/6,126:1d/5,127:ffffc/20,128:fe6/12,129:3fd2/14,130:fe7/12,131:fe8/12,132:3fd3/14,133:3fd4/14,134:3fd5/14,135:7fd9/15,136:3fd6/14,137:7fda/15,138:7fdb/15,139:7fdc/15,140:7fdd/15,141:7fde/15,142:ffeb/16,143:7fdf/15,144:ffec/16,145:ffed/16,146:3fd7/14,147:7fe0/15,148:ffee/16,149:7fe1/15,150:7fe2/15,151:7fe3/15,152:7fe4/15,153:1fdc/13,154:3fd8/14,155:7fe5/15,156:3fd9/14,157:7fe6/15,158:7fe7/15,159:ffef/16,160:3fda/14,161:1fdd/13,162:fe9/12,163:3fdb/14,164:3fdc/14,165:7fe8/15,166:7fe9/15,167:1fde/13,168:7fea/15,169:3fdd/14,170:3fde/14,171:fff0/16,172:1fdf/13,173:3fdf/14,174:7feb/15,175:7fec/15,176:1fe0/13,177:1fe1/13,178:3fe0/14,179:1fe2/13,180:7fed/15,181:3fe1/14,182:7fee/15,183:7fef/15,184:fea/12,185:3fe2/14,186:3fe3/14,187:3fe4/14,188:7ff0/15,189:3fe5/14,190:3fe6/14,191:7ff1/15,192:3ffe0/18,193:3ffe1/18,194:feb/12,195:7f1/11,196:3fe7/14,197:7ff2/15,198:3fe8/14,199:1ffec/17,200:3ffe2/18,201:3ffe3/18,202:3ffe4/18,203:7ffde/19,204:7ffdf/19,205:3ffe5/18,206:fff1/16,207:1ffed/17,208:7f2/11,209:1fe3/13,210:3ffe6/18,211:7ffe0/19,212:7ffe1/19,213:3ffe7/18,214:7ffe2/19,215:fff2/16,216:1fe4/13,217:1fe5/13,218:3ffe8/18,219:3ffe9/18,220:ffffd/20,221:7ffe3/19,222:7ffe4/19,223:7ffe5/19,224:fec/12,225:fff3/16,226:fed/12,227:1fe6/13,228:3fe9/14,229:1fe7/13,230:1fe8/13,231:7ff3/15,232:3fea/14,233:3feb/14,234:1ffee/17,235:1ffef/17,236:fff4/16,237:fff5/16,238:3ffea/18,239:7ff4/15,240:3ffeb/18,241:7ffe6/19,242:3ffec/18,243:3ffed/18,244:7ffe7/19,245:7ffe8/19,246:7ffe9/19,247:7ffea/19,248:7ffeb/19,249:ffffe/20,250:7ffec/19,251:7ffed/19,252:7ffee/19,253:7ffef/19,254:7fff0/19,255:3ffee/18,256:3fffff/22
      buffer_len -= 8;
      while (buffer_len < 8) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 8);
      emit_ofs = GetEmitOp2(index) / 8;
      switch (GetEmitOp2(index) % 8) {
        case 7: {
          // 1:58/7,2:fe2/12,3:fe3/12,4:fe4/12,5:fe5/12,6:fe6/12,7:fe7/12,8:fe8/12,9:ea/8,10:3ffc/14,11:fe9/12,12:fea/12,13:3ffd/14,14:feb/12,15:fec/12,16:fed/12,17:fee/12,18:fef/12,19:ff0/12,20:ff1/12,21:ff2/12,22:3ffe/14,23:ff3/12,24:ff4/12,25:ff5/12,26:ff6/12,27:ff7/12,28:ff8/12,29:ff9/12,30:ffa/12,31:ffb/12,127:ffc/12,129:12/6,132:13/6,133:14/6,134:15/6,135:59/7,136:16/6,137:5a/7,138:5b/7,139:5c/7,140:5d/7,141:5e/7,142:eb/8,143:5f/7,144:ec/8,145:ed/8,146:17/6,147:60/7,148:ee/8,149:61/7,150:62/7,151:63/7,152:64/7,154:18/6,155:65/7,156:19/6,157:66/7,158:67/7,159:ef/8,160:1a/6,163:1b/6,164:1c/6,165:68/7,166:69/7,168:6a/7,169:1d/6,170:1e/6,171:f0/8,173:1f/6,174:6b/7,175:6c/7,176:0/5,177:1/5,178:20/6,179:2/5,180:6d/7,181:21/6,182:6e/7,183:6f/7,185:22/6,186:23/6,187:24/6,188:70/7,189:25/6,190:26/6,191:71/7,192:3e0/10,193:3e1/10,196:27/6,197:72/7,198:28/6,199:1ec/9,200:3e2/10,201:3e3/10,202:3e4/10,203:7de/11,204:7df/11,205:3e5/10,206:f1/8,207:1ed/9,209:3/5,210:3e6/10,211:7e0/11,212:7e1/11,213:3e7/10,214:7e2/11,215:f2/8,216:4/5,217:5/5,218:3e8/10,219:3e9/10,220:ffd/12,221:7e3/11,222:7e4/11,223:7e5/11,225:f3/8,227:6/5,228:29/6,229:7/5,230:8/5,231:73/7,232:2a/6,233:2b/6,234:1ee/9,235:1ef/9,236:f4/8,237:f5/8,238:3ea/10,239:74/7,240:3eb/10,241:7e6/11,242:3ec/10,243:3ed/10,244:7e7/11,245:7e8/11,246:7e9/11,247:7ea/11,248:7eb/11,249:ffe/12,250:7ec/11,251:7ed/11,252:7ee/11,253:7ef/11,254:7f0/11,255:3ee/10,256:3fff/14
          buffer_len -= 8;
          while (buffer_len < 8) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 8);
          emit_ofs = GetEmitOp4(index) / 14;
          switch (GetEmitOp4(index) % 14) {
            case 13: {
              // 10:3c/6,13:3d/6,19:0/4,20:1/4,21:2/4,22:3e/6,23:3/4,24:4/4,25:5/4,26:6/4,27:7/4,28:8/4,29:9/4,30:a/4,31:b/4,127:c/4,220:d/4,249:e/4,256:3f/6
              buffer_len -= 8;
              while (buffer_len < 6) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 6);
              emit_ofs = GetEmitOp14(index) / 2;
              switch (GetEmitOp14(index) % 2) {
                case 0: {
                  sink(GetEmitBuffer14(emit_ofs + 0));
                  buffer_len -= 28;
                  goto refill;
                }
                case 1: {
                  sink(GetEmitBuffer14(emit_ofs + 0));
                  buffer_len -= 30;
                  goto refill;
                }
              }
              break;
            }
            case 6: {
              // 192:0/2,193:1/2,200:2/2,201:3/2
              buffer_len -= 8;
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              emit_ofs = GetEmitOp7(index) / 1;
              switch (GetEmitOp7(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer7(emit_ofs + 0));
                  buffer_len -= 26;
                  goto refill;
                }
              }
              break;
            }
            case 4: {
              // 199:0/1,207:1/1
              buffer_len -= 8;
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              emit_ofs = GetEmitOp5(index) / 1;
              switch (GetEmitOp5(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer5(emit_ofs + 0));
                  buffer_len -= 25;
                  goto refill;
                }
              }
              break;
            }
            case 7: {
              // 202:0/2,205:1/2,210:2/2,213:3/2
              buffer_len -= 8;
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              emit_ofs = GetEmitOp8(index) / 1;
              switch (GetEmitOp8(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer8(emit_ofs + 0));
                  buffer_len -= 26;
                  goto refill;
                }
              }
              break;
            }
            case 9: {
              // 203:6/3,204:7/3,242:0/2,243:1/2,255:2/2
              buffer_len -= 8;
              while (buffer_len < 3) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 3);
              emit_ofs = GetEmitOp13(index) / 2;
              switch (GetEmitOp13(index) % 2) {
                case 0: {
                  sink(GetEmitBuffer13(emit_ofs + 0));
                  buffer_len -= 26;
                  goto refill;
                }
                case 1: {
                  sink(GetEmitBuffer13(emit_ofs + 0));
                  buffer_len -= 27;
                  goto refill;
                }
              }
              break;
            }
            case 10: {
              // 211:0/3,212:1/3,214:2/3,221:3/3,222:4/3,223:5/3,241:6/3,244:7/3
              buffer_len -= 8;
              while (buffer_len < 3) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 3);
              emit_ofs = GetEmitOp10(index) / 1;
              switch (GetEmitOp10(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer10(emit_ofs + 0));
                  buffer_len -= 27;
                  goto refill;
                }
              }
              break;
            }
            case 8: {
              // 218:0/2,219:1/2,238:2/2,240:3/2
              buffer_len -= 8;
              while (buffer_len < 2) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 2);
              emit_ofs = GetEmitOp9(index) / 1;
              switch (GetEmitOp9(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer9(emit_ofs + 0));
                  buffer_len -= 26;
                  goto refill;
                }
              }
              break;
            }
            case 5: {
              // 234:0/1,235:1/1
              buffer_len -= 8;
              while (buffer_len < 1) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 1);
              emit_ofs = GetEmitOp6(index) / 1;
              switch (GetEmitOp6(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer6(emit_ofs + 0));
                  buffer_len -= 25;
                  goto refill;
                }
              }
              break;
            }
            case 11: {
              // 245:0/3,246:1/3,247:2/3,248:3/3,250:4/3,251:5/3,252:6/3,253:7/3
              buffer_len -= 8;
              while (buffer_len < 3) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 3);
              emit_ofs = GetEmitOp11(index) / 1;
              switch (GetEmitOp11(index) % 1) {
                case 0: {
                  sink(GetEmitBuffer11(emit_ofs + 0));
                  buffer_len -= 27;
                  goto refill;
                }
              }
              break;
            }
            case 12: {
              // 2:2/4,3:3/4,4:4/4,5:5/4,6:6/4,7:7/4,8:8/4,11:9/4,12:a/4,14:b/4,15:c/4,16:d/4,17:e/4,18:f/4,254:0/3
              buffer_len -= 8;
              while (buffer_len < 4) {
                if (begin == end) return buffer_len == 0;
                buffer <<= 8;
                buffer |= static_cast<uint64_t>(*begin++);
                buffer_len += 8;
              }
              index = buffer >> (buffer_len - 4);
              emit_ofs = GetEmitOp12(index) / 2;
              switch (GetEmitOp12(index) % 2) {
                case 0: {
                  sink(GetEmitBuffer12(emit_ofs + 0));
                  buffer_len -= 27;
                  goto refill;
                }
                case 1: {
                  sink(GetEmitBuffer12(emit_ofs + 0));
                  buffer_len -= 28;
                  goto refill;
                }
              }
              break;
            }
            case 0: {
              sink(GetEmitBuffer4(emit_ofs + 0));
              buffer_len -= 21;
              goto refill;
            }
            case 1: {
              sink(GetEmitBuffer4(emit_ofs + 0));
              buffer_len -= 22;
              goto refill;
            }
            case 2: {
              sink(GetEmitBuffer4(emit_ofs + 0));
              buffer_len -= 23;
              goto refill;
            }
            case 3: {
              sink(GetEmitBuffer4(emit_ofs + 0));
              buffer_len -= 24;
              goto refill;
            }
          }
          break;
        }
        case 6: {
          // 92:0/3,128:6/4,130:7/4,131:8/4,153:1c/5,161:1d/5,162:9/4,167:1e/5,172:1f/5,184:a/4,194:b/4,195:1/3,208:2/3,224:c/4,226:d/4
          buffer_len -= 8;
          while (buffer_len < 5) {
            if (begin == end) return buffer_len == 0;
            buffer <<= 8;
            buffer |= static_cast<uint64_t>(*begin++);
            buffer_len += 8;
          }
          index = buffer >> (buffer_len - 5);
          emit_ofs = GetEmitOp3(index) / 3;
          switch (GetEmitOp3(index) % 3) {
            case 0: {
              sink(GetEmitBuffer3(emit_ofs + 0));
              buffer_len -= 19;
              goto refill;
            }
            case 1: {
              sink(GetEmitBuffer3(emit_ofs + 0));
              buffer_len -= 20;
              goto refill;
            }
            case 2: {
              sink(GetEmitBuffer3(emit_ofs + 0));
              buffer_len -= 21;
              goto refill;
            }
          }
          break;
        }
        case 0: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          buffer_len -= 10;
          goto refill;
        }
        case 1: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          buffer_len -= 11;
          goto refill;
        }
        case 2: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          buffer_len -= 12;
          goto refill;
        }
        case 3: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          buffer_len -= 13;
          goto refill;
        }
        case 4: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          buffer_len -= 14;
          goto refill;
        }
        case 5: {
          sink(GetEmitBuffer2(emit_ofs + 0));
          buffer_len -= 15;
          goto refill;
        }
      }
      break;
    }
    case 4: {
      // 33:0/2,34:1/2,40:2/2,41:3/2
      buffer_len -= 8;
      while (buffer_len < 2) {
        if (begin == end) return buffer_len == 0;
        buffer <<= 8;
        buffer |= static_cast<uint64_t>(*begin++);
        buffer_len += 8;
      }
      index = buffer >> (buffer_len - 2);
      emit_ofs = GetEmitOp1(index) / 1;
      switch (GetEmitOp1(index) % 1) {
        case 0: {
          sink(GetEmitBuffer1(emit_ofs + 0));
          buffer_len -= 10;
          goto refill;
        }
      }
      break;
    }
    case 0: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      buffer_len -= 5;
      goto refill;
    }
    case 1: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      buffer_len -= 6;
      goto refill;
    }
    case 2: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      buffer_len -= 7;
      goto refill;
    }
    case 3: {
      sink(GetEmitBuffer0(emit_ofs + 0));
      buffer_len -= 8;
      goto refill;
    }
  }
  abort();
}
