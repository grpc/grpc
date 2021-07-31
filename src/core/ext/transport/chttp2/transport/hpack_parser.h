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

// Top level interface for parsing a sequence of header, continuation frames.
class HPackParser {
 public:
  // What kind of stream boundary is provided by this frame?
  enum class Boundary {
    // More continuations are expected
    None,
    // This marks the end of headers, so data frames should follow
    EndOfHeaders,
    // This marks the end of headers *and* the end of the stream
    EndOfStream
  };
  // What kind of priority is represented in the next frame
  enum class Priority {
    // No priority field
    None,
    // Yes there's a priority field
    Included
  };

  // User specified structure called for each received header.
  using Sink = std::function<grpc_error_handle(grpc_mdelem)>;

  HPackParser();
  ~HPackParser();

  // Non-copyable/movable
  HPackParser(const HPackParser&) = delete;
  HPackParser& operator=(const HPackParser&) = delete;

  // Begin parsing a new frame
  // Sink receives each parsed header,
  void BeginFrame(Sink sink, Boundary boundary, Priority priority);
  // Change the header sink mid parse
  void ResetSink(Sink sink) { sink_ = std::move(sink); }
  // Enqueue some data to parse later on
  void QueueBufferToParse(const grpc_slice& slice);
  // Parse all queued data, with last_slice concatenated at the end
  grpc_error_handle Parse(const grpc_slice& last_slice);
  // Reset state ready for the next BeginFrame
  void FinishFrame();

  // Retrieve the associated hpack table (for tests, debugging)
  HPackTable* hpack_table() { return &table_; }
  // Is the current frame a boundary of some sort
  bool is_boundary() const { return boundary_ != Boundary::None; }
  // Is the current frame the end of a stream
  bool is_eof() const { return boundary_ == Boundary::EndOfStream; }

 private:
  // Helper classes: see implementation
  class Parser;
  class Input;
  class String;

  // Callback per header received
  Sink sink_;

  // Buffer incoming bytes
  ParseBuffer buffer_;
  // Buffer kind of boundary
  // TODO(ctiller): see if we can move this argument to Parse, and avoid
  // buffering.
  Boundary boundary_;
  // Buffer priority
  // TODO(ctiller): see if we can move this argument to Parse, and avoid
  // buffering.
  Priority priority_;

  // hpack table
  HPackTable table_;
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
