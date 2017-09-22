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
#include <grpc/slice.h>

namespace grpc {

Slice::Slice() : slice_(grpc_empty_slice()) {}

Slice::~Slice() { grpc_slice_unref(slice_); }

Slice::Slice(grpc_slice slice, AddRef) : slice_(grpc_slice_ref(slice)) {}

Slice::Slice(grpc_slice slice, StealRef) : slice_(slice) {}

Slice::Slice(size_t len) : slice_(grpc_slice_malloc(len)) {}

Slice::Slice(const void* buf, size_t len)
    : slice_(grpc_slice_from_copied_buffer(reinterpret_cast<const char*>(buf),
                                           len)) {}

Slice::Slice(const grpc::string& str)
    : slice_(grpc_slice_from_copied_buffer(str.c_str(), str.length())) {}

Slice::Slice(const void* buf, size_t len, StaticSlice)
    : slice_(grpc_slice_from_static_buffer(reinterpret_cast<const char*>(buf),
                                           len)) {}

Slice::Slice(const Slice& other) : slice_(grpc_slice_ref(other.slice_)) {}

Slice::Slice(void* buf, size_t len, void (*destroy)(void*), void* user_data)
    : slice_(grpc_slice_new_with_user_data(buf, len, destroy, user_data)) {}

Slice::Slice(void* buf, size_t len, void (*destroy)(void*, size_t))
    : slice_(grpc_slice_new_with_len(buf, len, destroy)) {}

grpc_slice Slice::c_slice() const { return grpc_slice_ref(slice_); }

}  // namespace grpc
