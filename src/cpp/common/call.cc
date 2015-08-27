/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/impl/call.h>

#include <grpc/support/alloc.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/support/byte_buffer.h>
#include "src/core/profiling/timers.h"

namespace grpc {

void FillMetadataMap(
    grpc_metadata_array* arr,
    std::multimap<grpc::string_ref, grpc::string_ref>* metadata) {
  for (size_t i = 0; i < arr->count; i++) {
    // TODO(yangg) handle duplicates?
    metadata->insert(std::pair<grpc::string_ref, grpc::string_ref>(
        arr->metadata[i].key, grpc::string_ref(arr->metadata[i].value,
                                               arr->metadata[i].value_length)));
  }
  grpc_metadata_array_destroy(arr);
  grpc_metadata_array_init(arr);
}

// TODO(yangg) if the map is changed before we send, the pointers will be a
// mess. Make sure it does not happen.
grpc_metadata* FillMetadataArray(
    const std::multimap<grpc::string, grpc::string>& metadata) {
  if (metadata.empty()) {
    return nullptr;
  }
  grpc_metadata* metadata_array =
      (grpc_metadata*)gpr_malloc(metadata.size() * sizeof(grpc_metadata));
  size_t i = 0;
  for (auto iter = metadata.cbegin(); iter != metadata.cend(); ++iter, ++i) {
    metadata_array[i].key = iter->first.c_str();
    metadata_array[i].value = iter->second.c_str();
    metadata_array[i].value_length = iter->second.size();
  }
  return metadata_array;
}

Call::Call(grpc_call* call, CallHook* call_hook, CompletionQueue* cq)
    : call_hook_(call_hook), cq_(cq), call_(call), max_message_size_(-1) {}

Call::Call(grpc_call* call, CallHook* call_hook, CompletionQueue* cq,
           int max_message_size)
    : call_hook_(call_hook),
      cq_(cq),
      call_(call),
      max_message_size_(max_message_size) {}

void Call::PerformOps(CallOpSetInterface* ops) {
  if (max_message_size_ > 0) {
    ops->set_max_message_size(max_message_size_);
  }
  call_hook_->PerformOpsOnCall(ops, this);
}

}  // namespace grpc
