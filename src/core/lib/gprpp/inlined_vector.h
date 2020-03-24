/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_INLINED_VECTOR_H
#define GRPC_CORE_LIB_GPRPP_INLINED_VECTOR_H

#include <grpc/support/port_platform.h>

#include <cassert>
#include <cstring>

#include "absl/container/inlined_vector.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

template <typename T, size_t N, typename A = std::allocator<T>>
using InlinedVector = absl::InlinedVector<T, N, A>;

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_INLINED_VECTOR_H */
