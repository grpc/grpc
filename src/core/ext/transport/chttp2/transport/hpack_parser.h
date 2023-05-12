//
//
// Copyright 2015 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <vector>

#include <grpc/slice.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"
#include "src/core/lib/backoff/random_early_detection.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/metadata_batch.h"

// IWYU pragma: no_include <type_traits>

namespace grpc_core {

// Top level interface for parsing a sequence of header, continuation frames.
class HPackParser {
 public:
  // What kind of stream boundary is provided by this frame?
  enum class Boundary : uint8_t {
    // More continuations are expected
    None,
    // This marks the end of headers, so data frames should follow
    EndOfHeaders,
    // This marks the end of headers *and* the end of the stream
    EndOfStream
  };
  // What kind of priority is represented in the next frame
  enum class Priority : uint8_t {
    // No priority field
    None,
    // Yes there's a priority field
    Included
  };
  // Details about a frame we only need to know for logging
  struct LogInfo {
    // The stream ID
    uint32_t stream_id;
    // Headers or trailers?
    enum Type : uint8_t {
      kHeaders,
      kTrailers,
      kDontKnow,
    };
    Type type;
    // Client or server?
    bool is_client;
  };

  HPackParser();
  ~HPackParser();

  // Non-copyable/movable
  HPackParser(const HPackParser&) = delete;
  HPackParser& operator=(const HPackParser&) = delete;

  // Begin parsing a new frame
  // Sink receives each parsed header,
  void BeginFrame(grpc_metadata_batch* metadata_buffer,
                  uint32_t metadata_size_soft_limit,
                  uint32_t metadata_size_hard_limit, Boundary boundary,
                  Priority priority, LogInfo log_info);
  // Start throwing away any received headers after parsing them.
  void StopBufferingFrame() { metadata_buffer_ = nullptr; }
  // Parse one slice worth of data
  grpc_error_handle Parse(const grpc_slice& slice, bool is_last);
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

  grpc_error_handle ParseInput(Input input, bool is_last);
  void ParseInputInner(Input* input);
  GPR_ATTRIBUTE_NOINLINE
  void HandleMetadataSoftSizeLimitExceeded(Input* input);

  // Target metadata buffer
  grpc_metadata_batch* metadata_buffer_ = nullptr;

  // Bytes that could not be parsed last parsing round
  std::vector<uint8_t> unparsed_bytes_;
  // Buffer kind of boundary
  // TODO(ctiller): see if we can move this argument to Parse, and avoid
  // buffering.
  Boundary boundary_;
  // Buffer priority
  // TODO(ctiller): see if we can move this argument to Parse, and avoid
  // buffering.
  Priority priority_;
  uint8_t dynamic_table_updates_allowed_;
  // Length of frame so far.
  uint32_t frame_length_;
  RandomEarlyDetection metadata_early_detection_;
  // Information for logging
  LogInfo log_info_;

  // hpack table
  HPackTable table_;
};

}  // namespace grpc_core

// wraps grpc_chttp2_hpack_parser_parse to provide a frame level parser for
// the transport
grpc_error_handle grpc_chttp2_header_parser_parse(void* hpack_parser,
                                                  grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s,
                                                  const grpc_slice& slice,
                                                  int is_last);

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H
