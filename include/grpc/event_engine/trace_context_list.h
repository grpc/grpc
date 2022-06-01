// Copyright 2022 gRPC authors.
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

#ifndef GRPC_EVENT_ENGINE_TRACE_CONTEXT_LIST_H
#define GRPC_EVENT_ENGINE_TRACE_CONTEXT_LIST_H

#include <grpc/support/port_platform.h>

#include <functional>

#include <grpc/support/log.h>

namespace grpc_event_engine {
namespace experimental {

// A public type which holds Tracing related information for RPCs. The
// cumulative set of bytes sent over the wire in each endpoint write operation
// may contain bytes from multiple RPCs and some of them may be traced. Prior to
// sending bytes over the wire, an event engine endpoint may optionally be
// provided with a TraceContextList. The TraceContextList will hold one entry
// describing bytes belonging to each traced RPC within the cumulative set of
// bytes to be sent. Each entry contains an opaque trace_context pointer, the
// relative start position of the traced RPC bytes within the cumulative set of
// bytes to be written, the size of the traced RPC, and a byte_offset which
// represents the total number of written bytes belonging to that RPC so far.
class TraceContextList {
 public:
  /* Executes a function \a cb with each context in the list. The arguments
   * provided to cb include the trace_context_, byte_offset_,
   * traced_bytes_relative_start_pos_ and num_traced_bytes_ for each context in
   * the context list. It also frees up the entire list after this operation. */
  static void IterateAndFree(
      TraceContextList* head,
      std::function<void(void*, size_t, int64_t, int64_t)> cb);

  virtual ~TraceContextList() = default;

 protected:
  void* trace_context_ = nullptr;
  TraceContextList* next_ = nullptr;
  int64_t traced_bytes_relative_start_pos_ = 0;
  int64_t num_traced_bytes_ = 0;
  size_t byte_offset_ = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_TRACE_CONTEXT_LIST_H
