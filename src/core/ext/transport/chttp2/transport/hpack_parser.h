/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_table.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

class HPackParser {
 public:
  enum class Boundary { None, EndOfHeaders, EndOfStream };
  enum class Priority { None, Included };

  // User specified structure called for each received header.
  using Sink = std::function<grpc_error_handle(grpc_mdelem)>;

  HPackParser();
  ~HPackParser();

  HPackParser(const HPackParser&) = delete;
  HPackParser& operator=(const HPackParser&) = delete;

  void BeginFrame(Sink sink, Boundary boundary, Priority priority);
  void ResetSink(Sink sink) { sink_ = std::move(sink); }
  grpc_error_handle Parse(const grpc_slice& slice);
  void FinishFrame();

  grpc_chttp2_hptbl* hpack_table() { return &table_; }
  bool is_boundary() const { return boundary_ != Boundary::None; }
  bool is_eof() const { return boundary_ == Boundary::EndOfStream; }
  bool is_in_begin_state() const { return state_ == &HPackParser::parse_begin; }

 private:
  enum class BinaryState {
    kNotBinary,
    kBinaryBegin,
    kBase64Byte0,
    kBase64Byte1,
    kBase64Byte2,
    kBase64Byte3,
  };

  struct String {
    bool copied_;
    struct {
      grpc_slice referenced;
      struct {
        char* str;
        uint32_t length;
        uint32_t capacity;
      } copied;
    } data_;

    UnmanagedMemorySlice TakeExtern();
    ManagedMemorySlice TakeIntern();
    void AppendBytes(const uint8_t* data, size_t length);
  };

  using State = grpc_error_handle (HPackParser::*)(const uint8_t* beg,
                                                   const uint8_t* end);

  // Forward declarations for parsing states.
  // These are keeping their old (C-style) names until a future refactor where
  // they will be eliminated.
  grpc_error_handle parse_next(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_begin(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_error(const uint8_t* cur, const uint8_t* end,
                                grpc_error_handle error);
  grpc_error_handle still_parse_error(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_illegal_op(const uint8_t* cur, const uint8_t* end);

  grpc_error_handle parse_string_prefix(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_key_string(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value_string(const uint8_t* cur, const uint8_t* end,
                                       bool is_binary);
  grpc_error_handle parse_value_string_with_indexed_key(const uint8_t* cur,
                                                        const uint8_t* end);
  grpc_error_handle parse_value_string_with_literal_key(const uint8_t* cur,
                                                        const uint8_t* end);
  grpc_error_handle parse_stream_weight(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value0(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value1(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value2(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value3(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value4(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_value5up(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_stream_dep0(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_stream_dep1(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_stream_dep2(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_stream_dep3(const uint8_t* cur, const uint8_t* end);

  grpc_error_handle parse_indexed_field(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_indexed_field_x(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_lithdr_incidx(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_lithdr_incidx_x(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_lithdr_incidx_v(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_lithdr_notidx(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_lithdr_notidx_x(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_lithdr_notidx_v(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_lithdr_nvridx(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_lithdr_nvridx_x(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_lithdr_nvridx_v(const uint8_t* cur,
                                          const uint8_t* end);
  grpc_error_handle parse_max_tbl_size(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle parse_max_tbl_size_x(const uint8_t* cur,
                                         const uint8_t* end);
  grpc_error_handle parse_string(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle begin_parse_string(const uint8_t* cur, const uint8_t* end,
                                       BinaryState binary, String* str);

  grpc_error_handle finish_indexed_field(const uint8_t* cur,
                                         const uint8_t* end);
  grpc_error_handle finish_lithdr_incidx(const uint8_t* cur,
                                         const uint8_t* end);
  grpc_error_handle finish_lithdr_incidx_v(const uint8_t* cur,
                                           const uint8_t* end);
  grpc_error_handle finish_lithdr_notidx(const uint8_t* cur,
                                         const uint8_t* end);
  grpc_error_handle finish_lithdr_notidx_v(const uint8_t* cur,
                                           const uint8_t* end);
  grpc_error_handle finish_lithdr_nvridx(const uint8_t* cur,
                                         const uint8_t* end);
  grpc_error_handle finish_lithdr_nvridx_v(const uint8_t* cur,
                                           const uint8_t* end);
  grpc_error_handle finish_max_tbl_size(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle finish_str(const uint8_t* cur, const uint8_t* end);

  enum class TableAction {
    kAddToTable,
    kOmitFromTable,
  };

  GPR_ATTRIBUTE_NOINLINE grpc_error_handle InvalidHPackIndexError();
  GPR_ATTRIBUTE_NOINLINE void LogHeader(grpc_mdelem md);
  grpc_error_handle AddHeaderToTable(grpc_mdelem md);
  template <TableAction table_action>
  grpc_error_handle FinishHeader(grpc_mdelem md);

  grpc_mdelem GetPrecomputedMDForIndex();
  void SetPrecomputedMDIndex(grpc_mdelem md);
  bool IsBinaryLiteralHeader();
  grpc_error_handle IsBinaryIndexedHeader(bool* is);

  grpc_error_handle AppendString(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle AppendHuffNibble(uint8_t nibble);
  grpc_error_handle AppendHuffBytes(const uint8_t* cur, const uint8_t* end);
  grpc_error_handle AppendStrBytes(const uint8_t* cur, const uint8_t* end);

  Sink sink_;
  grpc_error_handle last_error_;

  // current parse state - or a function that implements it
  State state_;
  // future states dependent on the opening op code
  const State* next_state_;
  // what to do after skipping prioritization data
  State after_prioritization_;
  // the refcount of the slice that we're currently parsing
  grpc_slice_refcount* current_slice_refcount_;
  // the value we're currently parsing
  union {
    uint32_t* value;
    String* str;
  } parsing_;
  // string parameters for each chunk
  String key_;
  String value_;
  // parsed index
  uint32_t index_;
  // When we parse a value string, we determine the metadata element for a
  // specific index, which we need again when we're finishing up with that
  // header. To avoid calculating the metadata element for that index a second
  // time at that stage, we cache (and invalidate) the element here.
  grpc_mdelem md_for_index_;
#ifndef NDEBUG
  int64_t precomputed_md_index_;
#endif
  // length of source bytes for the currently parsing string
  uint32_t strlen_;
  // number of source bytes read for the currently parsing string
  uint32_t strgot_;
  // huffman decoding state
  int16_t huff_state_;
  // is the string being decoded binary?
  BinaryState binary_;
  // is the current string huffman encoded?
  bool huff_;
  // is a dynamic table update allowed?
  uint8_t dynamic_table_updates_allowed_;
  // set by higher layers, used by grpc_chttp2_header_parser_parse to signal
  // it should append a metadata boundary at the end of frame
  Boundary boundary_;
  uint32_t base64_buffer_;

  // hpack table
  grpc_chttp2_hptbl table_;
};

}  // namespace grpc_core

/* wraps grpc_chttp2_hpack_parser_parse to provide a frame level parser for
   the transport */
grpc_error_handle grpc_chttp2_header_parser_parse(void* hpack_parser,
                                                  grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s,
                                                  const grpc_slice& slice,
                                                  int is_last);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H */
