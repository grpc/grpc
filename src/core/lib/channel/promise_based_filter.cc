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

#include "src/core/lib/channel/promise_based_filter.h"

#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {
namespace promise_filter_detail {

// We don't form ActivityPtr's to this type, and consequently don't need
// Orphan().
void BaseCallData::Orphan() { abort(); }

// For now we don't care about owning/non-owning wakers, instead just share
// implementation.
Waker BaseCallData::MakeNonOwningWaker() { return MakeOwningWaker(); }

Waker BaseCallData::MakeOwningWaker() {
  GRPC_CALL_STACK_REF(call_stack_, "waker");
  return Waker(this);
}

void BaseCallData::Wakeup() {
  auto wakeup = [](void* p, grpc_error_handle error) {
    auto* self = static_cast<BaseCallData*>(p);
    self->OnWakeup();
    self->Drop();
  };
  auto* closure = GRPC_CLOSURE_CREATE(wakeup, this, nullptr);
  GRPC_CALL_COMBINER_START(call_combiner_, closure, GRPC_ERROR_NONE, "wakeup");
}

void BaseCallData::Drop() { GRPC_CALL_STACK_UNREF(call_stack_, "waker"); }

}  // namespace promise_filter_detail
}  // namespace grpc_core
