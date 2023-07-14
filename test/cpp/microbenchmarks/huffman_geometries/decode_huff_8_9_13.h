// Copyright 2023 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file is autogenerated: see
// tools/codegen/core/gen_huffman_decompressor.cc

#ifndef GRPC_TEST_CPP_MICROBENCHMARKS_HUFFMAN_GEOMETRIES_DECODE_HUFF_8_9_13_H
#define GRPC_TEST_CPP_MICROBENCHMARKS_HUFFMAN_GEOMETRIES_DECODE_HUFF_8_9_13_H
#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
namespace grpc_core {
namespace geometry_8_9_13 {
class HuffDecoderCommon {
 protected:
  static inline uint64_t GetOp2(size_t i) { return table2_0_ops_[i]; }
  static inline uint64_t GetEmit2(size_t, size_t emit) {
    return table2_0_emit_[emit];
  }
  static inline uint64_t GetOp3(size_t i) { return table3_0_ops_[i]; }
  static inline uint64_t GetEmit3(size_t, size_t emit) {
    return table3_0_emit_[emit];
  }
  static inline uint64_t GetOp4(size_t i) {
    return table4_ops_[i >> 6][i & 0x3f];
  }
  static inline uint64_t GetEmit4(size_t i, size_t emit) {
    return table4_emit_[i >> 6][emit];
  }
  static inline uint64_t GetOp1(size_t i) {
    return table1_0_inner_[table1_0_outer_[i]];
  }
  static inline uint64_t GetEmit1(size_t, size_t emit) {
    return table1_0_emit_[emit];
  }
  static inline uint64_t GetOp5(size_t i) {
    return (i < 2 ? (i ? 6 : 2) : ((i - 2) ? 14 : 10));
  }
  static inline uint64_t GetEmit5(size_t, size_t emit) {
    return (emit < 2 ? (emit + 33) : ((emit - 2) + 40));
  }
  static inline uint64_t GetOp7(size_t i) {
    return (i < 2 ? (i) : ((i - 2) + 1));
  }
  static inline uint64_t GetEmit7(size_t, size_t emit) {
    return ((void)emit, 63);
  }
  static inline uint64_t GetOp8(size_t i) {
    return table8_0_inner_[table8_0_outer_[i]];
  }
  static inline uint64_t GetEmit8(size_t, size_t emit) {
    return (emit < 2 ? (emit ? 39 : 63) : ((emit - 2) ? 124 : 43));
  }
  static inline uint64_t GetOp9(size_t i) {
    return table9_0_inner_[table9_0_outer_[i]];
  }
  static inline uint64_t GetEmit9(size_t, size_t emit) {
    return table9_0_emit_[emit];
  }
  static inline uint64_t GetOp10(size_t i) { return table10_0_ops_[i]; }
  static inline uint64_t GetEmit10(size_t, size_t emit) {
    return table10_0_emit_[emit];
  }
  static inline uint64_t GetOp11(size_t i) { return table11_0_ops_[i]; }
  static inline uint64_t GetEmit11(size_t, size_t emit) {
    return table11_0_emit_[emit];
  }
  static inline uint64_t GetOp12(size_t i) { return table12_0_ops_[i]; }
  static inline uint64_t GetEmit12(size_t, size_t emit) {
    return table12_0_emit_[emit];
  }
  static inline uint64_t GetOp13(size_t i) {
    return table13_ops_[i >> 5][i & 0x1f];
  }
  static inline uint64_t GetEmit13(size_t i, size_t emit) {
    return table13_emit_[i >> 5][emit];
  }
  static inline uint64_t GetOp6(size_t i) {
    return table6_ops_[i >> 5][i & 0x1f];
  }
  static inline uint64_t GetEmit6(size_t i, size_t emit) {
    return table6_emit_[i >> 5][emit];
  }
  static inline uint64_t GetOp15(size_t i) {
    return (i < 2 ? (i ? 2 : 0) : ((i - 2) ? 1 : 4));
  }
  static inline uint64_t GetEmit15(size_t, size_t emit) {
    return (emit < 1 ? (((void)emit, 92)) : ((emit - 1) ? 208 : 195));
  }
  static inline uint64_t GetOp14(size_t i) {
    return table14_0_inner_[(i < 5 ? (i / 2 + 0) : ((i - 5) + 2))];
  }
  static inline uint64_t GetEmit14(size_t, size_t emit) {
    return table14_0_emit_[emit];
  }
  static inline uint64_t GetOp17(size_t i) {
    return table17_0_inner_[(i < 6 ? (i) : (((void)(i - 6), 6)))];
  }
  static inline uint64_t GetEmit17(size_t, size_t emit) {
    return table17_0_emit_[emit];
  }
  static inline uint64_t GetOp16(size_t i) {
    return table16_0_inner_[(i < 11 ? (i / 2 + 0) : ((i - 11) + 5))];
  }
  static inline uint64_t GetEmit16(size_t, size_t emit) {
    return table16_0_emit_[emit];
  }
  static inline uint64_t GetOp19(size_t i) {
    return table19_0_inner_[(i < 9 ? (i) : (((void)(i - 9), 9)))];
  }
  static inline uint64_t GetEmit19(size_t, size_t emit) {
    return table19_0_emit_[emit];
  }
  static inline uint64_t GetOp18(size_t i) { return table18_0_ops_[i]; }
  static inline uint64_t GetEmit18(size_t, size_t emit) {
    return table18_0_emit_[emit];
  }
  static inline uint64_t GetOp21(size_t i) { return table21_0_ops_[i]; }
  static inline uint64_t GetEmit21(size_t, size_t emit) {
    return table21_0_emit_[emit];
  }
  static inline uint64_t GetOp22(size_t i) { return table22_0_ops_[i]; }
  static inline uint64_t GetEmit22(size_t, size_t emit) {
    return table22_0_emit_[emit];
  }
  static inline uint64_t GetOp23(size_t i) { return table23_0_ops_[i]; }
  static inline uint64_t GetEmit23(size_t, size_t emit) {
    return table23_0_emit_[emit];
  }
  static inline uint64_t GetOp24(size_t i) { return table24_0_ops_[i]; }
  static inline uint64_t GetEmit24(size_t, size_t emit) {
    return table24_0_emit_[emit];
  }
  static inline uint64_t GetOp25(size_t i) {
    return table25_ops_[i >> 6][i & 0x3f];
  }
  static inline uint64_t GetEmit25(size_t i, size_t emit) {
    return table25_emit_[i >> 6][emit];
  }
  static inline uint64_t GetOp26(size_t i) {
    return table26_ops_[i >> 6][i & 0x3f];
  }
  static inline uint64_t GetEmit26(size_t i, size_t emit) {
    return table26_emit_[i >> 6][emit];
  }
  static inline uint64_t GetOp27(size_t i) {
    return table27_ops_[i >> 7][i & 0x7f];
  }
  static inline uint64_t GetEmit27(size_t i, size_t emit) {
    return table27_emit_[i >> 7][emit];
  }
  static inline uint64_t GetOp28(size_t i) {
    return table28_ops_[i >> 7][i & 0x7f];
  }
  static inline uint64_t GetEmit28(size_t i, size_t emit) {
    return table28_emit_[i >> 7][emit];
  }
  static inline uint64_t GetOp20(size_t i) {
    return table20_ops_[i >> 7][i & 0x7f];
  }
  static inline uint64_t GetEmit20(size_t i, size_t emit) {
    return table20_emit_[i >> 7][emit];
  }

 private:
  static const uint8_t table2_0_emit_[10];
  static const uint8_t table2_0_ops_[32];
  static const uint8_t table3_0_emit_[36];
  static const uint8_t table3_0_ops_[64];
  static const uint8_t table4_0_emit_[22];
  static const uint8_t table4_0_ops_[64];
  static const uint8_t table4_1_emit_[46];
  static const uint8_t table4_1_ops_[64];
  static const uint8_t* const table4_emit_[2];
  static const uint8_t* const table4_ops_[2];
  static const uint8_t table1_0_emit_[74];
  static const uint16_t table1_0_inner_[76];
  static const uint8_t table1_0_outer_[256];
  static const uint8_t table8_0_inner_[6];
  static const uint8_t table8_0_outer_[8];
  static const uint8_t table9_0_emit_[6];
  static const uint8_t table9_0_inner_[8];
  static const uint8_t table9_0_outer_[16];
  static const uint8_t table10_0_emit_[12];
  static const uint8_t table10_0_ops_[32];
  static const uint8_t table11_0_emit_[14];
  static const uint8_t table11_0_ops_[64];
  static const uint8_t table12_0_emit_[36];
  static const uint8_t table12_0_ops_[128];
  static const uint8_t table13_0_emit_[44];
  static const uint8_t table13_0_ops_[32];
  static const uint8_t table13_1_emit_[28];
  static const uint8_t table13_1_ops_[32];
  static const uint8_t table13_2_emit_[20];
  static const uint8_t table13_2_ops_[32];
  static const uint8_t table13_3_emit_[20];
  static const uint8_t table13_4_emit_[20];
  static const uint8_t table13_5_emit_[2];
  static const uint8_t table13_5_ops_[32];
  static const uint8_t table13_6_emit_[4];
  static const uint8_t table13_6_ops_[32];
  static const uint8_t table13_7_emit_[7];
  static const uint8_t table13_7_ops_[32];
  static const uint8_t* const table13_emit_[8];
  static const uint8_t* const table13_ops_[8];
  static const uint8_t table6_0_emit_[1];
  static const uint16_t table6_0_ops_[32];
  static const uint8_t table6_4_emit_[1];
  static const uint16_t table6_4_ops_[32];
  static const uint8_t table6_6_emit_[1];
  static const uint8_t table6_8_emit_[1];
  static const uint8_t table6_10_emit_[1];
  static const uint16_t table6_10_ops_[32];
  static const uint8_t table6_11_emit_[1];
  static const uint8_t table6_12_emit_[2];
  static const uint16_t table6_12_ops_[32];
  static const uint8_t table6_13_emit_[2];
  static const uint8_t table6_14_emit_[2];
  static const uint8_t table6_15_emit_[5];
  static const uint16_t table6_15_ops_[32];
  static const uint8_t* const table6_emit_[16];
  static const uint16_t* const table6_ops_[16];
  static const uint8_t table14_0_emit_[5];
  static const uint8_t table14_0_inner_[5];
  static const uint8_t table17_0_emit_[6];
  static const uint8_t table17_0_inner_[7];
  static const uint8_t table16_0_emit_[10];
  static const uint8_t table16_0_inner_[10];
  static const uint8_t table19_0_emit_[9];
  static const uint8_t table19_0_inner_[10];
  static const uint8_t table18_0_emit_[23];
  static const uint8_t table18_0_ops_[32];
  static const uint8_t table21_0_emit_[12];
  static const uint8_t table21_0_ops_[32];
  static const uint8_t table22_0_emit_[41];
  static const uint8_t table22_0_ops_[64];
  static const uint8_t table23_0_emit_[53];
  static const uint8_t table23_0_ops_[128];
  static const uint8_t table24_0_emit_[57];
  static const uint8_t table24_0_ops_[256];
  static const uint8_t table25_0_emit_[4];
  static const uint8_t table25_0_ops_[64];
  static const uint8_t table25_1_emit_[4];
  static const uint8_t table25_2_emit_[4];
  static const uint8_t table25_3_emit_[8];
  static const uint8_t table25_3_ops_[64];
  static const uint8_t table25_4_emit_[8];
  static const uint8_t table25_5_emit_[8];
  static const uint8_t table25_6_emit_[11];
  static const uint8_t table25_6_ops_[64];
  static const uint8_t table25_7_emit_[25];
  static const uint8_t table25_7_ops_[64];
  static const uint8_t* const table25_emit_[8];
  static const uint8_t* const table25_ops_[8];
  static const uint8_t table26_0_emit_[40];
  static const uint8_t table26_0_ops_[64];
  static const uint8_t table26_1_emit_[40];
  static const uint8_t table26_2_emit_[40];
  static const uint8_t table26_3_emit_[40];
  static const uint8_t table26_4_emit_[40];
  static const uint8_t table26_5_emit_[40];
  static const uint8_t table26_6_emit_[4];
  static const uint8_t table26_6_ops_[64];
  static const uint8_t table26_7_emit_[4];
  static const uint8_t table26_8_emit_[4];
  static const uint8_t table26_9_emit_[4];
  static const uint8_t table26_10_emit_[4];
  static const uint8_t table26_11_emit_[4];
  static const uint8_t table26_12_emit_[4];
  static const uint8_t table26_13_emit_[7];
  static const uint8_t table26_13_ops_[64];
  static const uint8_t table26_14_emit_[10];
  static const uint8_t table26_14_ops_[64];
  static const uint8_t table26_15_emit_[34];
  static const uint8_t table26_15_ops_[64];
  static const uint8_t* const table26_emit_[16];
  static const uint8_t* const table26_ops_[16];
  static const uint8_t table27_0_emit_[144];
  static const uint16_t table27_0_ops_[128];
  static const uint8_t table27_1_emit_[144];
  static const uint8_t table27_2_emit_[144];
  static const uint8_t table27_3_emit_[144];
  static const uint8_t table27_4_emit_[144];
  static const uint8_t table27_5_emit_[144];
  static const uint8_t table27_6_emit_[80];
  static const uint16_t table27_6_ops_[128];
  static const uint8_t table27_7_emit_[80];
  static const uint8_t table27_8_emit_[80];
  static const uint8_t table27_9_emit_[80];
  static const uint8_t table27_10_emit_[80];
  static const uint8_t table27_11_emit_[80];
  static const uint8_t table27_12_emit_[80];
  static const uint8_t table27_13_emit_[26];
  static const uint16_t table27_13_ops_[128];
  static const uint16_t table27_14_ops_[128];
  static const uint8_t table27_15_emit_[63];
  static const uint16_t table27_15_ops_[128];
  static const uint8_t* const table27_emit_[16];
  static const uint16_t* const table27_ops_[16];
  static const uint8_t table28_0_emit_[136];
  static const uint16_t table28_0_ops_[128];
  static const uint8_t table28_1_emit_[136];
  static const uint8_t table28_2_emit_[136];
  static const uint8_t table28_3_emit_[136];
  static const uint8_t table28_4_emit_[136];
  static const uint8_t table28_5_emit_[136];
  static const uint8_t table28_6_emit_[136];
  static const uint8_t table28_7_emit_[136];
  static const uint8_t table28_8_emit_[136];
  static const uint8_t table28_9_emit_[136];
  static const uint8_t table28_10_emit_[136];
  static const uint8_t table28_11_emit_[136];
  static const uint8_t table28_12_emit_[144];
  static const uint8_t table28_13_emit_[144];
  static const uint8_t table28_14_emit_[144];
  static const uint8_t table28_15_emit_[144];
  static const uint8_t table28_16_emit_[144];
  static const uint8_t table28_17_emit_[144];
  static const uint8_t table28_18_emit_[144];
  static const uint8_t table28_19_emit_[144];
  static const uint8_t table28_20_emit_[144];
  static const uint8_t table28_21_emit_[144];
  static const uint8_t table28_22_emit_[144];
  static const uint8_t table28_23_emit_[144];
  static const uint8_t table28_24_emit_[144];
  static const uint8_t table28_25_emit_[144];
  static const uint8_t table28_26_emit_[112];
  static const uint16_t table28_26_ops_[128];
  static const uint8_t table28_27_emit_[80];
  static const uint8_t table28_28_emit_[80];
  static const uint8_t table28_29_emit_[44];
  static const uint16_t table28_29_ops_[128];
  static const uint8_t table28_30_emit_[17];
  static const uint16_t table28_30_ops_[128];
  static const uint8_t table28_31_emit_[46];
  static const uint16_t table28_31_ops_[128];
  static const uint8_t* const table28_emit_[32];
  static const uint16_t* const table28_ops_[32];
  static const uint8_t table20_0_emit_[1];
  static const uint16_t table20_0_ops_[128];
  static const uint8_t table20_2_emit_[1];
  static const uint8_t table20_4_emit_[1];
  static const uint8_t table20_6_emit_[1];
  static const uint8_t table20_8_emit_[1];
  static const uint8_t table20_10_emit_[1];
  static const uint8_t table20_12_emit_[1];
  static const uint8_t table20_14_emit_[1];
  static const uint8_t table20_16_emit_[1];
  static const uint8_t table20_18_emit_[1];
  static const uint8_t table20_20_emit_[1];
  static const uint8_t table20_22_emit_[1];
  static const uint8_t table20_24_emit_[1];
  static const uint16_t table20_24_ops_[128];
  static const uint8_t table20_25_emit_[1];
  static const uint8_t table20_26_emit_[1];
  static const uint8_t table20_27_emit_[1];
  static const uint8_t table20_28_emit_[1];
  static const uint8_t table20_29_emit_[1];
  static const uint8_t table20_30_emit_[1];
  static const uint8_t table20_31_emit_[1];
  static const uint8_t table20_32_emit_[1];
  static const uint8_t table20_33_emit_[1];
  static const uint8_t table20_34_emit_[1];
  static const uint8_t table20_35_emit_[1];
  static const uint8_t table20_36_emit_[1];
  static const uint8_t table20_37_emit_[1];
  static const uint8_t table20_38_emit_[1];
  static const uint8_t table20_39_emit_[1];
  static const uint8_t table20_40_emit_[1];
  static const uint8_t table20_41_emit_[1];
  static const uint8_t table20_42_emit_[1];
  static const uint8_t table20_43_emit_[1];
  static const uint8_t table20_44_emit_[1];
  static const uint8_t table20_45_emit_[1];
  static const uint8_t table20_46_emit_[1];
  static const uint8_t table20_47_emit_[1];
  static const uint8_t table20_48_emit_[1];
  static const uint8_t table20_49_emit_[1];
  static const uint8_t table20_50_emit_[1];
  static const uint8_t table20_51_emit_[1];
  static const uint8_t table20_52_emit_[1];
  static const uint8_t table20_53_emit_[2];
  static const uint16_t table20_53_ops_[128];
  static const uint8_t table20_54_emit_[2];
  static const uint8_t table20_55_emit_[2];
  static const uint8_t table20_56_emit_[2];
  static const uint8_t table20_57_emit_[2];
  static const uint8_t table20_58_emit_[2];
  static const uint8_t table20_59_emit_[4];
  static const uint16_t table20_59_ops_[128];
  static const uint8_t table20_60_emit_[8];
  static const uint16_t table20_60_ops_[128];
  static const uint8_t table20_61_emit_[9];
  static const uint16_t table20_61_ops_[128];
  static const uint8_t table20_62_emit_[16];
  static const uint16_t table20_62_ops_[128];
  static const uint8_t table20_63_emit_[33];
  static const uint16_t table20_63_ops_[128];
  static const uint8_t* const table20_emit_[64];
  static const uint16_t* const table20_ops_[64];
};
template <typename F>
class HuffDecoder : public HuffDecoderCommon {
 public:
  HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end)
      : sink_(sink), begin_(begin), end_(end) {}
  bool Run() {
    while (!done_) {
      if (!RefillTo8()) {
        Done0();
        break;
      }
      const auto index = (buffer_ >> (buffer_len_ - 8)) & 0xff;
      const auto op = GetOp1(index);
      const int consumed = op & 15;
      buffer_len_ -= consumed;
      const auto emit_ofs = op >> 6;
      switch ((op >> 4) & 3) {
        case 0: {
          sink_(GetEmit1(index, emit_ofs + 0));
          break;
        }
        case 1: {
          DecodeStep0();
          break;
        }
        case 2: {
          DecodeStep1();
          break;
        }
      }
    }
    return ok_;
  }

 private:
  bool RefillTo8() {
    switch (buffer_len_) {
      case 0: {
        return Read1to8Bytes();
      }
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7: {
        return Read1to7Bytes();
      }
    }
    return true;
  }
  bool Read1to8Bytes() {
    switch (end_ - begin_) {
      case 0: {
        return false;
      }
      case 1: {
        Fill1();
        return true;
      }
      case 2: {
        Fill2();
        return true;
      }
      case 3: {
        Fill3();
        return true;
      }
      case 4: {
        Fill4();
        return true;
      }
      case 5: {
        Fill5();
        return true;
      }
      case 6: {
        Fill6();
        return true;
      }
      case 7: {
        Fill7();
        return true;
      }
      default: {
        Fill8();
        return true;
      }
    }
  }
  void Fill1() {
    buffer_ = (buffer_ << 8) | (static_cast<uint64_t>(begin_[0]) << 0);
    begin_ += 1;
    buffer_len_ += 8;
  }
  void Fill2() {
    buffer_ = (buffer_ << 16) | (static_cast<uint64_t>(begin_[0]) << 8) |
              (static_cast<uint64_t>(begin_[1]) << 0);
    begin_ += 2;
    buffer_len_ += 16;
  }
  void Fill3() {
    buffer_ = (buffer_ << 24) | (static_cast<uint64_t>(begin_[0]) << 16) |
              (static_cast<uint64_t>(begin_[1]) << 8) |
              (static_cast<uint64_t>(begin_[2]) << 0);
    begin_ += 3;
    buffer_len_ += 24;
  }
  void Fill4() {
    buffer_ = (buffer_ << 32) | (static_cast<uint64_t>(begin_[0]) << 24) |
              (static_cast<uint64_t>(begin_[1]) << 16) |
              (static_cast<uint64_t>(begin_[2]) << 8) |
              (static_cast<uint64_t>(begin_[3]) << 0);
    begin_ += 4;
    buffer_len_ += 32;
  }
  void Fill5() {
    buffer_ = (buffer_ << 40) | (static_cast<uint64_t>(begin_[0]) << 32) |
              (static_cast<uint64_t>(begin_[1]) << 24) |
              (static_cast<uint64_t>(begin_[2]) << 16) |
              (static_cast<uint64_t>(begin_[3]) << 8) |
              (static_cast<uint64_t>(begin_[4]) << 0);
    begin_ += 5;
    buffer_len_ += 40;
  }
  void Fill6() {
    buffer_ = (buffer_ << 48) | (static_cast<uint64_t>(begin_[0]) << 40) |
              (static_cast<uint64_t>(begin_[1]) << 32) |
              (static_cast<uint64_t>(begin_[2]) << 24) |
              (static_cast<uint64_t>(begin_[3]) << 16) |
              (static_cast<uint64_t>(begin_[4]) << 8) |
              (static_cast<uint64_t>(begin_[5]) << 0);
    begin_ += 6;
    buffer_len_ += 48;
  }
  void Fill7() {
    buffer_ = (buffer_ << 56) | (static_cast<uint64_t>(begin_[0]) << 48) |
              (static_cast<uint64_t>(begin_[1]) << 40) |
              (static_cast<uint64_t>(begin_[2]) << 32) |
              (static_cast<uint64_t>(begin_[3]) << 24) |
              (static_cast<uint64_t>(begin_[4]) << 16) |
              (static_cast<uint64_t>(begin_[5]) << 8) |
              (static_cast<uint64_t>(begin_[6]) << 0);
    begin_ += 7;
    buffer_len_ += 56;
  }
  void Fill8() {
    buffer_ = 0 | (static_cast<uint64_t>(begin_[0]) << 56) |
              (static_cast<uint64_t>(begin_[1]) << 48) |
              (static_cast<uint64_t>(begin_[2]) << 40) |
              (static_cast<uint64_t>(begin_[3]) << 32) |
              (static_cast<uint64_t>(begin_[4]) << 24) |
              (static_cast<uint64_t>(begin_[5]) << 16) |
              (static_cast<uint64_t>(begin_[6]) << 8) |
              (static_cast<uint64_t>(begin_[7]) << 0);
    begin_ += 8;
    buffer_len_ += 64;
  }
  bool Read1to7Bytes() {
    switch (end_ - begin_) {
      case 0: {
        return false;
      }
      case 1: {
        Fill1();
        return true;
      }
      case 2: {
        Fill2();
        return true;
      }
      case 3: {
        Fill3();
        return true;
      }
      case 4: {
        Fill4();
        return true;
      }
      case 5: {
        Fill5();
        return true;
      }
      case 6: {
        Fill6();
        return true;
      }
      default: {
        Fill7();
        return true;
      }
    }
  }
  void Done0() {
    done_ = true;
    switch (end_ - begin_) {}
    switch (buffer_len_) {
      case 1:
      case 2:
      case 3:
      case 4: {
        ok_ = (buffer_ & ((1 << buffer_len_) - 1)) == (1 << buffer_len_) - 1;
        return;
      }
      case 5: {
        const auto index = buffer_ & 31;
        const auto op = GetOp2(index);
        switch (op & 3) {
          case 0: {
            sink_(GetEmit2(index, (op >> 2) + 0));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
        }
        return;
      }
      case 6: {
        const auto index = buffer_ & 63;
        const auto op = GetOp3(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit3(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 7: {
        const auto index = buffer_ & 127;
        const auto op = GetOp4(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit4(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 0: {
        return;
      }
    }
  }
  void DecodeStep0() {
    if (!RefillTo2()) {
      Done1();
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 2)) & 0x3;
    const auto op = GetOp5(index);
    const int consumed = op & 3;
    buffer_len_ -= consumed;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit5(index, emit_ofs + 0));
  }
  bool RefillTo2() {
    switch (buffer_len_) {
      case 0: {
        return Read1to8Bytes();
      }
      case 1: {
        return Read1to7Bytes();
      }
    }
    return true;
  }
  void Done1() {
    done_ = true;
    switch (buffer_len_) {
      case 1:
      case 0: {
        ok_ = false;
        return;
      }
    }
  }
  void DecodeStep1() {
    if (!RefillTo9()) {
      Done2();
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 9)) & 0x1ff;
    const auto op = GetOp6(index);
    const int consumed = op & 15;
    buffer_len_ -= consumed;
    const auto emit_ofs = op >> 7;
    switch ((op >> 4) & 7) {
      case 0: {
        sink_(GetEmit6(index, emit_ofs + 0));
        break;
      }
      case 1: {
        DecodeStep2();
        break;
      }
      case 2: {
        DecodeStep3();
        break;
      }
      case 3: {
        DecodeStep4();
        break;
      }
      case 4: {
        DecodeStep5();
        break;
      }
    }
  }
  bool RefillTo9() {
    switch (buffer_len_) {
      case 0: {
        return Read2to8Bytes();
      }
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8: {
        return Read1to7Bytes();
      }
    }
    return true;
  }
  bool Read2to8Bytes() {
    switch (end_ - begin_) {
      case 0:
      case 1: {
        return false;
      }
      case 2: {
        Fill2();
        return true;
      }
      case 3: {
        Fill3();
        return true;
      }
      case 4: {
        Fill4();
        return true;
      }
      case 5: {
        Fill5();
        return true;
      }
      case 6: {
        Fill6();
        return true;
      }
      case 7: {
        Fill7();
        return true;
      }
      default: {
        Fill8();
        return true;
      }
    }
  }
  void Done2() {
    done_ = true;
    switch (end_ - begin_) {
      case 1: {
        Fill1();
        break;
      }
    }
    switch (buffer_len_) {
      case 1: {
        ok_ = (buffer_ & ((1 << buffer_len_) - 1)) == (1 << buffer_len_) - 1;
        return;
      }
      case 2: {
        const auto index = buffer_ & 3;
        const auto op = GetOp7(index);
        switch (op & 3) {
          case 0: {
            sink_(GetEmit7(index, (op >> 2) + 0));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
        }
        return;
      }
      case 3: {
        const auto index = buffer_ & 7;
        const auto op = GetOp8(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit8(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 4: {
        const auto index = buffer_ & 15;
        const auto op = GetOp9(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit9(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 5: {
        const auto index = buffer_ & 31;
        const auto op = GetOp10(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit10(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 6: {
        const auto index = buffer_ & 63;
        const auto op = GetOp11(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit11(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 7: {
        const auto index = buffer_ & 127;
        const auto op = GetOp12(index);
        switch (op & 3) {
          case 0: {
            sink_(GetEmit12(index, (op >> 2) + 0));
            sink_(GetEmit12(index, (op >> 2) + 1));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
          case 2: {
            sink_(GetEmit12(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 8: {
        const auto index = buffer_ & 255;
        const auto op = GetOp13(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit13(index, (op >> 2) + 0));
            sink_(GetEmit13(index, (op >> 2) + 1));
            break;
          }
          case 2: {
            sink_(GetEmit13(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 0: {
        return;
      }
    }
  }
  void DecodeStep2() {
    if (!RefillTo3()) {
      Done3();
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 3)) & 0x7;
    const auto op = GetOp14(index);
    const int consumed = op & 3;
    buffer_len_ -= consumed;
    const auto emit_ofs = op >> 2;
    sink_(GetEmit14(index, emit_ofs + 0));
  }
  bool RefillTo3() {
    switch (buffer_len_) {
      case 0: {
        return Read1to8Bytes();
      }
      case 1:
      case 2: {
        return Read1to7Bytes();
      }
    }
    return true;
  }
  void Done3() {
    done_ = true;
    switch (buffer_len_) {
      case 1:
      case 0: {
        ok_ = false;
        return;
      }
      case 2: {
        const auto index = buffer_ & 3;
        const auto op = GetOp15(index);
        switch (op & 1) {
          case 0: {
            sink_(GetEmit15(index, (op >> 1) + 0));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
        }
        return;
      }
    }
  }
  void DecodeStep3() {
    if (!RefillTo4()) {
      Done4();
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 4)) & 0xf;
    const auto op = GetOp16(index);
    const int consumed = op & 7;
    buffer_len_ -= consumed;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit16(index, emit_ofs + 0));
  }
  bool RefillTo4() {
    switch (buffer_len_) {
      case 0: {
        return Read1to8Bytes();
      }
      case 1:
      case 2:
      case 3: {
        return Read1to7Bytes();
      }
    }
    return true;
  }
  void Done4() {
    done_ = true;
    switch (buffer_len_) {
      case 1:
      case 2:
      case 0: {
        ok_ = false;
        return;
      }
      case 3: {
        const auto index = buffer_ & 7;
        const auto op = GetOp17(index);
        switch (op & 1) {
          case 0: {
            sink_(GetEmit17(index, (op >> 1) + 0));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
        }
        return;
      }
    }
  }
  void DecodeStep4() {
    if (!RefillTo5()) {
      Done5();
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 5)) & 0x1f;
    const auto op = GetOp18(index);
    const int consumed = op & 7;
    buffer_len_ -= consumed;
    const auto emit_ofs = op >> 3;
    sink_(GetEmit18(index, emit_ofs + 0));
  }
  bool RefillTo5() {
    switch (buffer_len_) {
      case 0: {
        return Read1to8Bytes();
      }
      case 1:
      case 2:
      case 3:
      case 4: {
        return Read1to7Bytes();
      }
    }
    return true;
  }
  void Done5() {
    done_ = true;
    switch (buffer_len_) {
      case 1:
      case 2:
      case 3:
      case 0: {
        ok_ = false;
        return;
      }
      case 4: {
        const auto index = buffer_ & 15;
        const auto op = GetOp19(index);
        switch (op & 1) {
          case 0: {
            sink_(GetEmit19(index, (op >> 1) + 0));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
        }
        return;
      }
    }
  }
  void DecodeStep5() {
    if (!RefillTo13()) {
      Done6();
      return;
    }
    const auto index = (buffer_ >> (buffer_len_ - 13)) & 0x1fff;
    const auto op = GetOp20(index);
    const int consumed = op & 15;
    buffer_len_ -= consumed;
    const auto emit_ofs = op >> 5;
    switch ((op >> 4) & 1) {
      case 0: {
        sink_(GetEmit20(index, emit_ofs + 0));
        break;
      }
      case 1: {
        begin_ = end_;
        buffer_len_ = 0;
        break;
      }
    }
  }
  bool RefillTo13() {
    switch (buffer_len_) {
      case 0: {
        return Read2to8Bytes();
      }
      case 1:
      case 2:
      case 3:
      case 4: {
        return Read2to7Bytes();
      }
      case 5:
      case 6:
      case 7:
      case 8: {
        return Read1to7Bytes();
      }
      case 9:
      case 10:
      case 11:
      case 12: {
        return Read1to6Bytes();
      }
    }
    return true;
  }
  bool Read2to7Bytes() {
    switch (end_ - begin_) {
      case 0:
      case 1: {
        return false;
      }
      case 2: {
        Fill2();
        return true;
      }
      case 3: {
        Fill3();
        return true;
      }
      case 4: {
        Fill4();
        return true;
      }
      case 5: {
        Fill5();
        return true;
      }
      case 6: {
        Fill6();
        return true;
      }
      default: {
        Fill7();
        return true;
      }
    }
  }
  bool Read1to6Bytes() {
    switch (end_ - begin_) {
      case 0: {
        return false;
      }
      case 1: {
        Fill1();
        return true;
      }
      case 2: {
        Fill2();
        return true;
      }
      case 3: {
        Fill3();
        return true;
      }
      case 4: {
        Fill4();
        return true;
      }
      case 5: {
        Fill5();
        return true;
      }
      default: {
        Fill6();
        return true;
      }
    }
  }
  void Done6() {
    done_ = true;
    switch (end_ - begin_) {
      case 1: {
        Fill1();
        break;
      }
    }
    switch (buffer_len_) {
      case 1:
      case 2:
      case 3:
      case 4: {
        ok_ = (buffer_ & ((1 << buffer_len_) - 1)) == (1 << buffer_len_) - 1;
        return;
      }
      case 5: {
        const auto index = buffer_ & 31;
        const auto op = GetOp21(index);
        switch (op & 3) {
          case 0: {
            sink_(GetEmit21(index, (op >> 2) + 0));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
        }
        return;
      }
      case 6: {
        const auto index = buffer_ & 63;
        const auto op = GetOp22(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit22(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 7: {
        const auto index = buffer_ & 127;
        const auto op = GetOp23(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit23(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 8: {
        const auto index = buffer_ & 255;
        const auto op = GetOp24(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit24(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 9: {
        const auto index = buffer_ & 511;
        const auto op = GetOp25(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit25(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 10: {
        const auto index = buffer_ & 1023;
        const auto op = GetOp26(index);
        switch (op & 3) {
          case 0: {
            sink_(GetEmit26(index, (op >> 2) + 0));
            sink_(GetEmit26(index, (op >> 2) + 1));
            break;
          }
          case 1: {
            ok_ = false;
            break;
          }
          case 2: {
            sink_(GetEmit26(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 11: {
        const auto index = buffer_ & 2047;
        const auto op = GetOp27(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit27(index, (op >> 2) + 0));
            sink_(GetEmit27(index, (op >> 2) + 1));
            break;
          }
          case 2: {
            sink_(GetEmit27(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 12: {
        const auto index = buffer_ & 4095;
        const auto op = GetOp28(index);
        switch (op & 3) {
          case 0: {
            ok_ = false;
            break;
          }
          case 1: {
            sink_(GetEmit28(index, (op >> 2) + 0));
            sink_(GetEmit28(index, (op >> 2) + 1));
            break;
          }
          case 2: {
            sink_(GetEmit28(index, (op >> 2) + 0));
            break;
          }
        }
        return;
      }
      case 0: {
        return;
      }
    }
  }
  F sink_;
  const uint8_t* begin_;
  const uint8_t* const end_;
  uint64_t buffer_ = 0;
  int buffer_len_ = 0;
  bool ok_ = true;
  bool done_ = false;
};
}  // namespace geometry_8_9_13
}  // namespace grpc_core
#endif  // GRPC_TEST_CPP_MICROBENCHMARKS_HUFFMAN_GEOMETRIES_DECODE_HUFF_8_9_13_H
