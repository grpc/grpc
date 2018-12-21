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
void (*write_timestamps_callback_g)(void*, grpc_core::Timestamps*) = nullptr;
}

namespace grpc_core {
void ContextList::Execute(void* arg, grpc_core::Timestamps* ts,
                          grpc_error* error) {
  ContextList* head = static_cast<ContextList*>(arg);
  ContextList* to_be_freed;
  while (head != nullptr) {
    if (error == GRPC_ERROR_NONE && ts != nullptr) {
      if (write_timestamps_callback_g) {
        ts->byte_offset = static_cast<uint32_t>(head->byte_offset_);
        write_timestamps_callback_g(head->s_->context, ts);
      }
    }
    GRPC_CHTTP2_STREAM_UNREF(static_cast<grpc_chttp2_stream*>(head->s_),
                             "timestamp");
    to_be_freed = head;
    head = head->next_;
    grpc_core::Delete(to_be_freed);
  }
}

void grpc_http2_set_write_timestamps_callback(
    void (*fn)(void*, grpc_core::Timestamps*)) {
  write_timestamps_callback_g = fn;
}
} /* namespace grpc_core */
