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

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <grpc/event_engine/trace_context_list.h>

namespace grpc_event_engine {
namespace experimental {

void TraceContextList::IterateAndFree(
    TraceContextList* head,
    std::function<void(void*, size_t, int64_t, int64_t)> cb) {
  TraceContextList* to_be_freed;
  while (head != nullptr) {
    cb(head->trace_context_, head->byte_offset_,
       head->traced_bytes_relative_start_pos_, head->num_traced_bytes_);
    to_be_freed = head;
    head = head->next_;
    delete to_be_freed;
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
