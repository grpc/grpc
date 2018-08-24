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

/** A list of RPC Contexts */
class ContextList {\
 public:
  /* Creates a new element with \a context as the value and appends it to the
   * list. */
  void Append(ContextList **head, void *context) {
    /* Make sure context is not already present */
    ContextList *ptr = *head;
    while(ptr != nullptr) {
      if(ptr->context == context) {
        GPR_ASSERT(false);
      }
    }
    ContextList *elem = static_cast<ContextListElement *>(gpr_malloc(sizeof(ContextList)));
    elem->context = context;
    elem->next = nullptr;
    if(*head_ == nullptr) {
      *head = elem;
      return;
    }
    ptr = *head;
    while(ptr->next != nullptr) {
      ptr = ptr->next;
    }
    ptr->next = elem;
  }

  /* Executes a function \a fn with each context in the list and \a arg. It also
   * frees up the entire list after this operation. */
  void Execute(ContextList *head, grpc_core::Timestamps *ts, grpc_error* error);

 private:
    void *context;
    ContextListElement *next;
};

grpc_http2_set_write_timestamps_callback(void (*fn)(void *, grpc_core::Timestamps*));

#endif /* GRPC_CORE_EXT_TRANSPORT_CONTEXT_LIST_H */
