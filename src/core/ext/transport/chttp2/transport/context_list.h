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

#ifndef GRPC_CORE_EXT_TRANSPORT_CONTEXT_LIST_H
#define GRPC_CORE_EXT_TRANSPORT_CONTEXT_LIST_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/buffer_list.h"

#include "src/core/ext/transport/chttp2/transport/internal.h"

namespace grpc_core {
/** A list of RPC Contexts */
class ContextList {
 public:
  /* Creates a new element with \a context as the value and appends it to the
   * list. */
  static void Append(ContextList** head, grpc_chttp2_stream* s) {
    /* Make sure context is not already present */
    ContextList* ptr = *head;
    GRPC_CHTTP2_STREAM_REF(s, "timestamp");
    while (ptr != nullptr) {
      if (ptr->s_ == s) {
        GPR_ASSERT(
            false &&
            "Trying to append a stream that is already present in the list");
      }
      ptr = ptr->next_;
    }
    ContextList* elem = grpc_core::New<ContextList>();
    elem->s_ = s;
    if (*head == nullptr) {
      *head = elem;
      return;
    }
    ptr = *head;
    while (ptr->next_ != nullptr) {
      ptr = ptr->next_;
    }
    ptr->next_ = elem;
  }

  /* Executes a function \a fn with each context in the list and \a ts. It also
   * frees up the entire list after this operation. */
  static void Execute(void* arg, grpc_core::Timestamps* ts, grpc_error* error);

 private:
  grpc_chttp2_stream* s_ = nullptr;
  ContextList* next_ = nullptr;
};

void grpc_http2_set_write_timestamps_callback(
    void (*fn)(void*, grpc_core::Timestamps*));
} /* namespace grpc_core */

#endif /* GRPC_CORE_EXT_TRANSPORT_CONTEXT_LIST_H */
