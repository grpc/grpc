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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/context_list.h"

#include <stdint.h>

#include <algorithm>

namespace {
void (*write_timestamps_callback_g)(void*, grpc_core::Timestamps*,
                                    grpc_error_handle error) = nullptr;
void* (*get_copied_context_fn_g)(void*) = nullptr;
}  // namespace

namespace grpc_core {
void ContextList::Append(void* context, size_t byte_offset_in_stream,
                         int64_t relative_start_pos_in_chunk,
                         int64_t num_traced_bytes_in_chunk) {
  if (get_copied_context_fn_g == nullptr ||
      write_timestamps_callback_g == nullptr) {
    return;
  }
  context_list_entries_.emplace_back(
      get_copied_context_fn_g(context), relative_start_pos_in_chunk,
      num_traced_bytes_in_chunk, byte_offset_in_stream);
}

void ContextList::Execute(void* arg, Timestamps* ts, grpc_error_handle error) {
  ContextList* list = static_cast<ContextList*>(arg);
  if (!list) {
    return;
  }
  for (auto it = list->context_list_entries_.begin();
       it != list->context_list_entries_.end(); it++) {
    ContextListEntry& entry = (*it);
    if (ts) {
      ts->byte_offset = static_cast<uint32_t>(entry.byte_offset_in_stream);
    }
    write_timestamps_callback_g(entry.trace_context, ts, error);
  }
  delete list;
}

void ContextList::ForEachExecuteCallback(
    ContextList* list,
    absl::FunctionRef<void(void*, size_t, int64_t, int64_t)> cb) {
  if (!list) {
    return;
  }
  for (auto it = list->context_list_entries_.begin();
       it != list->context_list_entries_.end(); it++) {
    ContextListEntry& entry = (*it);
    cb(entry.trace_context, entry.byte_offset_in_stream,
       entry.relative_start_pos_in_chunk, entry.num_traced_bytes_in_chunk);
  }
  delete list;
}

void grpc_http2_set_write_timestamps_callback(
    void (*fn)(void*, Timestamps*, grpc_error_handle error)) {
  write_timestamps_callback_g = fn;
}

void grpc_http2_set_fn_get_copied_context(void* (*fn)(void*)) {
  get_copied_context_fn_g = fn;
}
}  // namespace grpc_core
