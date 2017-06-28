/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc++/support/slice.h>

namespace grpc {

Slice::Slice() : slice_(grpc_empty_slice()) {}

Slice::~Slice() { grpc_slice_unref(slice_); }

Slice::Slice(grpc_slice slice, AddRef) : slice_(grpc_slice_ref(slice)) {}

Slice::Slice(grpc_slice slice, StealRef) : slice_(slice) {}

Slice::Slice(const Slice& other) : slice_(grpc_slice_ref(other.slice_)) {}

}  // namespace grpc
