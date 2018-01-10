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

#include <stddef.h>

#include <grpc/support/port_platform.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_table.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {
namespace chttp2 {

class HpackParser {
 public:
  HpackParser();
  ~HpackParser();

  grpc_error* ParseSlice(grpc_slice slice,
                         grpc_core::metadata::Collection* parsing_metadata);

 private:
  class ParserString {
   public:
    void ResetReferenced(grpc_slice_refcount* refcount, const uint8_t* beg,
                         size_t len);
    void ResetCopied();
    grpc_slice Take(bool intern);
    void AppendBytes(const uint8_t* data, size_t length);

   private:
    enum class State { COPIED, REFERENCED, EMPTY };
    State state_ = State::EMPTY;
    union {
      grpc_slice referenced;
      struct {
        char* str;
        uint32_t length;
        uint32_t capacity;
      } copied;
    } data_;
  };

  class ParseContext;
  typedef grpc_error* (ParseContext::*State)();

  grpc_error* last_error_;

  enum class BinaryParseState : uint8_t {
    NOT_BINARY,
    BINARY_BEGIN,
    B64_BYTE0,
    B64_BYTE1,
    B64_BYTE2,
    B64_BYTE3
  };

  /* current parse state - or a function that implements it */
  State state_;
  /* future states dependent on the opening op code */
  const State* next_state_;
  /* what to do after skipping prioritization data */
  State after_prioritization_;
  /* the value we're currently parsing */
  union {
    uint32_t* value;
    ParserString* str;
  } parsing_;
  /* string parameters for each chunk */
  ParserString key_;
  ParserString value_;
  /* parsed index */
  uint32_t index_;
  /* length of source bytes for the currently parsing string */
  uint32_t strlen_;
  /* number of source bytes read for the currently parsing string */
  uint32_t strgot_;
  uint32_t base64_buffer_;
  /* huffman decoding state */
  int16_t huff_state_;
  /* is the string being decoded binary? */
  BinaryParseState binary_ : 3;
  /* is the current string huffman encoded? */
  bool huff_ : 1;
  /* how many dynamic table updates are allowed this http frame? */
  uint8_t dynamic_table_update_allowed_ : 2;  // 0, 1, 2
  /* set by higher layers, used by grpc_chttp2_header_parser_parse to signal
     it should append a metadata boundary at the end of frame */
  bool is_boundary_ : 1;
  bool is_eof_ : 1;

  HpackTable table_;
};

}  // namespace chttp2
}  // namespace grpc_core

/* wraps grpc_chttp2_hpack_parser_parse to provide a frame level parser for
   the transport */
grpc_error* grpc_chttp2_header_parser_parse(void* hpack_parser,
                                            grpc_chttp2_transport* t,
                                            grpc_chttp2_stream* s,
                                            grpc_slice slice, int is_last);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H */
