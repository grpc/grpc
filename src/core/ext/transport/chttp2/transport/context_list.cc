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
void (*cb)(void *, grpc_core::Timestamps*);
}

void ContextList::Execute(ContextList *head, grpc_core::Timestamps *ts, grpc_error* error) {
  ContextList *ptr;
  while(head != nullptr) {
    if(error == GRPC_ERROR_NONE) {
      cb(head->context, ts);
    }
    ptr = head;
    head_ = head->next;
    gpr_free(ptr);
  }
}


grpc_http2_set_write_timestamps_callback(void (*fn)(void *, grpc_core::Timestamps *)) {
  cb = fn;
}
