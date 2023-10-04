//
//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CONTEXT_LIST_ENTRY_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CONTEXT_LIST_ENTRY_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/core/lib/channel/tcp_tracer.h"

namespace grpc_core {

/// An RPC trace context and associated information. Each RPC/stream is
/// associated with a unique \a context. A new ContextList entry is created when
/// a chunk of data stored in an outgoing buffer is going to be
// sent over the wire. A data chunk being written over the wire is multiplexed
// with bytes from multiple RPCs. If one such RPC is traced, we store the
// following information about the traced RPC:
// - byte_offset_in_stream: Number of bytes belonging to that traced RPC which
// have been sent so far from the start of the RPC stream.
// - relative_start_pos_in_chunk: Starting offset of the traced RPC within
// the current chunk that is being sent.
// - num_traced_bytes_in_chunk: Number of bytes belonging to the traced RPC
// within the current chunk.
class ContextListEntry {
 public:
  ContextListEntry(void* context, int64_t relative_start_pos,
                   int64_t num_traced_bytes, size_t byte_offset,
                   std::shared_ptr<TcpTracerInterface> tcp_tracer)
      : trace_context_(context),
        relative_start_pos_in_chunk_(relative_start_pos),
        num_traced_bytes_in_chunk_(num_traced_bytes),
        byte_offset_in_stream_(byte_offset),
        tcp_tracer_(std::move(tcp_tracer)) {}

  ContextListEntry() = delete;

  void* TraceContext() { return trace_context_; }
  int64_t RelativeStartPosInChunk() { return relative_start_pos_in_chunk_; }
  int64_t NumTracedBytesInChunk() { return num_traced_bytes_in_chunk_; }
  size_t ByteOffsetInStream() { return byte_offset_in_stream_; }
  std::shared_ptr<TcpTracerInterface> ReleaseTcpTracer() {
    return std::move(tcp_tracer_);
  }

 private:
  void* trace_context_;
  int64_t relative_start_pos_in_chunk_;
  int64_t num_traced_bytes_in_chunk_;
  size_t byte_offset_in_stream_;
  std::shared_ptr<TcpTracerInterface> tcp_tracer_;
};

/// A list of RPC Contexts
typedef std::vector<ContextListEntry> ContextList;
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CONTEXT_LIST_ENTRY_H
