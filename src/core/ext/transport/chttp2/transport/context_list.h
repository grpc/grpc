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
class ContextList {
  struct ContextListElement {
    void *context;
    ContextListElement *next;
  };

 public:
  ContextList() : head_(nullptr);

  /* Creates a new element with \a context as the value and appends it to the 
   * list. */
  void Append(void *context) {
    ContextListElement *elem = static_cast<ContextListElement *>(gpr_malloc(sizeof(ContextlistELement)));
    elem->context = context;
    elem->next = nullptr;
    if(head_ == nullptr) {
      head = elem;
      return;
    }
    ContextListElement *ptr = head_;
    while(ptr->next != nullptr) {
      ptr = ptr->next;
    }
    ptr->next = elem;
  }

  /* Executes a function \a fn with each context in the list and \a arg. It also
   * frees up the entire list after this operation. */
  void Execute(void (*fn)(void *context, void *arg)) {
    ContextListElement *ptr;
    while(head_ != nullptr){
      fn(head->context, arg);
      ptr = head_;
      head_ = head_->next;
      gpr_free(ptr);
    }
  }
 private:
  ContextListElement *head_;
};

#endif /* GRPC_CORE_EXT_TRANSPORT_CONTEXT_LIST_H */
