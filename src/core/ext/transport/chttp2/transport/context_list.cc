/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/context_list.h"

namespace {
void (*write_timestamps_callback_g)(void*, grpc_core::Timestamps*,
                                    grpc_error* error) = nullptr;
void* (*get_copied_context_fn_g)(void*) = nullptr;
}  // namespace

namespace grpc_core {
void ContextList::Append(ContextList** head, grpc_chttp2_stream* s) {
  if (get_copied_context_fn_g == nullptr ||
      write_timestamps_callback_g == nullptr) {
    return;
  }
  /* Create a new element in the list and add it at the front */
  ContextList* elem = grpc_core::New<ContextList>();
  elem->trace_context_ = get_copied_context_fn_g(s->context);
  elem->byte_offset_ = s->byte_counter;
  elem->next_ = *head;
  *head = elem;
}

void ContextList::Execute(void* arg, grpc_core::Timestamps* ts,
                          grpc_error* error) {
  ContextList* head = static_cast<ContextList*>(arg);
  ContextList* to_be_freed;
  while (head != nullptr) {
    if (write_timestamps_callback_g) {
      if (ts) {
        ts->byte_offset = static_cast<uint32_t>(head->byte_offset_);
      }
      write_timestamps_callback_g(head->trace_context_, ts, error);
    }
    to_be_freed = head;
    head = head->next_;
    grpc_core::Delete(to_be_freed);
  }
}

void grpc_http2_set_write_timestamps_callback(void (*fn)(void*,
                                                         grpc_core::Timestamps*,
                                                         grpc_error* error)) {
  write_timestamps_callback_g = fn;
}

void grpc_http2_set_fn_get_copied_context(void* (*fn)(void*)) {
  get_copied_context_fn_g = fn;
}
} /* namespace grpc_core */
