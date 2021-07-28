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

// Buffer bytes from one or more slices, to present to some parser as a
// contiguous block.
// TODO(ctiller): move this to it's own module
class ParseBuffer {
 public:
  void Queue(const grpc_slice& buffer) {
    queued_.insert(queued_.end(), GRPC_SLICE_START_PTR(buffer),
                   GRPC_SLICE_END_PTR(buffer));
  }

  template <typename F>
  auto Finalize(const grpc_slice& last, F consume)
      -> decltype(consume(nullptr, nullptr, nullptr)) {
    if (queued_.empty()) {
      return consume(last.refcount, GRPC_SLICE_START_PTR(last),
                     GRPC_SLICE_END_PTR(last));
    } else {
      Queue(last);
      auto r =
          consume(nullptr, queued_.data(), queued_.data() + queued_.size());
      queued_.clear();
      return r;
    }
  }

 private:
  std::vector<uint8_t> queued_;
};

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
  void QueueBufferToParse(const grpc_slice& slice);
  grpc_error_handle Parse(const grpc_slice& last_slice);
  void FinishFrame();

  grpc_chttp2_hptbl* hpack_table() { return &table_; }
  bool is_boundary() const { return boundary_ != Boundary::None; }
  bool is_eof() const { return boundary_ == Boundary::EndOfStream; }

 private:
  class Parser;
  class Input;
  class String;

  grpc_error_handle ParseOneSlice(const grpc_slice& slice);

  Sink sink_;

  ParseBuffer buffer_;
  Boundary boundary_;
  Priority priority_;

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
