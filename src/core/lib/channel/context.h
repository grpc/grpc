//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_CONTEXT_H
#define GRPC_SRC_CORE_LIB_CHANNEL_CONTEXT_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/context.h"

/// Call object context pointers.

/// Call context is represented as an array of \a grpc_call_context_elements.
/// This enum represents the indexes into the array, where each index
/// contains a different type of value.
typedef enum { GRPC_CONTEXT_COUNT } grpc_context_index;

struct grpc_call_context_element {
  void* value = nullptr;
  void (*destroy)(void*) = nullptr;
};

namespace grpc_core {
class ServiceConfigCallData;

// Bind the legacy context array into the new style structure
// TODO(ctiller): remove as we migrate these contexts to the new system.
template <>
struct ContextType<grpc_call_context_element> {};

// Also as a transition step allow exposing a GetContext<T> that can peek into
// the legacy context array.
namespace promise_detail {
template <typename T>
struct OldStyleContext;

template <typename T>
class Context<T, absl::void_t<decltype(OldStyleContext<T>::kIndex)>> {
 public:
  static T* get() {
    return static_cast<T*>(
        GetContext<grpc_call_context_element>()[OldStyleContext<T>::kIndex]
            .value);
  }
  static void set(T* value) {
    auto& elem =
        GetContext<grpc_call_context_element>()[OldStyleContext<T>::kIndex];
    if (elem.destroy != nullptr) {
      elem.destroy(elem.value);
      elem.destroy = nullptr;
    }
    elem.value = value;
  }
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_CONTEXT_H
