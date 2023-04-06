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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CONTEXT_LIST_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CONTEXT_LIST_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_core {
/// A list of RPC Contexts
class ContextList {
 public:
  // Creates a new element with \a context as the value and appends it to the
  // list. Each RPC/stream is associated with a unique \a context. This method
  // is invoked when a chunk of data stored in an outgoing buffer is going to be
  // sent over the wire. A data chunk being written over the wire is multiplexed
  // with bytes from multiple RPCs. If one such RPC is traced, we store the
  // following information about the traced RPC:
  // - byte_offset_in_stream: Number of bytes belonging to that traced RPC which
  // have been sent so far from the start of the RPC stream.
  // - relative_start_pos_in_chunk: Starting offset of the traced RPC within
  // the current chunk that is being sent.
  // - num_traced_bytes_in_chunk: Number of bytes belonging to the traced RPC
  // within the current chunk.
  static void Append(ContextList** tail, void* context,
                     size_t byte_offset_in_stream,
                     int64_t relative_start_pos_in_chunk,
                     int64_t num_traced_bytes_in_chunk);

  // Executes the function set using grpc_http2_set_write_timestamps_callback
  // method with each context in the list and \a ts. It also frees up the entire
  // list after this operation. It is intended as a callback and hence does not
  // take a ref on \a error. The fn receives individual contexts in the same
  // order in which they were Appended.
  static void Execute(void* arg, Timestamps* ts, grpc_error_handle error);

  // Executes the passed function \a cb with each context in the list. The
  // arguments provided to cb include the trace_context_, byte_offset_,
  // traced_bytes_relative_start_pos_ and num_traced_bytes_ for each context in
  // the context list. It also frees up the entire list after this operation.
  // The cb receives individual contexts in the same order in which they were
  // Appended.
  static void ForEachExecuteCallback(
      ContextList* tail,
      std::function<void(void*, size_t, int64_t, int64_t)> cb);

 private:
  void* trace_context_ = nullptr;
  ContextList* next_ = nullptr;  // For iterating from head to tail.
  ContextList* head_ = nullptr;  // Point to the first element in the list.
  int64_t relative_start_pos_in_chunk_ = 0;
  int64_t num_traced_bytes_in_chunk_ = 0;
  size_t byte_offset_in_stream_ = 0;
};

void grpc_http2_set_write_timestamps_callback(
    void (*fn)(void*, Timestamps*, grpc_error_handle error));
void grpc_http2_set_fn_get_copied_context(void* (*fn)(void*));
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CONTEXT_LIST_H
