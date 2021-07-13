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
  class Sink {
   public:
    virtual void OnHeader(grpc_mdelem md) = 0;
  };

  HPackParser();
  ~HPackParser();

  HPackParser(const HPackParser&) = delete;
  HPackParser& operator=(const HPackParser&) = delete;

  void BeginFrame(Sink* sink, Boundary boundary, Priority priority);

  grpc_error_handle Parse(grpc_chttp2_hpack_parser* p, const grpc_slice& slice);

 private:
  using State = grpc_error_handle (*)(grpc_chttp2_hpack_parser* p,
                                      const uint8_t* beg, const uint8_t* end);

  struct String {
    bool copied;
    struct {
      grpc_slice referenced;
      struct {
        char* str;
        uint32_t length;
        uint32_t capacity;
      } copied;
    } data;
  };

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
    grpc_chttp2_hpack_parser_string* str;
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
  bool binary_;
  // is the current string huffman encoded?
  bool huff_;
  // is a dynamic table update allowed?
  uint8_t dynamic_table_update_allowed_;
  // set by higher layers, used by grpc_chttp2_header_parser_parse to signal
  // it should append a metadata boundary at the end of frame
  Boundary boundary_;
  uint32_t base64_buffer;

  // hpack table
  grpc_chttp2_hptbl table;
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
