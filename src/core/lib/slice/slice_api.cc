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

#include <grpc/support/port_platform.h>

#include <grpc/event_engine/slice.h>
#include <grpc/slice.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/slice/slice_refcount_base.h"

/* Public API */
grpc_slice grpc_slice_ref(grpc_slice slice) {
  return grpc_slice_ref_internal(slice);
}

/* Public API */
void grpc_slice_unref(grpc_slice slice) {
  if (grpc_core::ExecCtx::Get() == nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_slice_unref_internal(slice);
  } else {
    grpc_slice_unref_internal(slice);
  }
}

namespace grpc_event_engine {
namespace experimental {

namespace slice_detail {

uint32_t BaseSlice::Hash() const { return grpc_slice_hash_internal(slice_); }

template <>
MutableSlice CopyConstructors<MutableSlice>::FromCopiedString(std::string s) {
  return MutableSlice(grpc_slice_from_cpp_string(std::move(s)));
}

template <>
Slice StaticConstructors<Slice>::FromStaticBuffer(const void* s, size_t len) {
  grpc_slice slice;
  slice.refcount = grpc_slice_refcount::NoopRefcount();
  slice.data.refcounted.bytes =
      const_cast<uint8_t*>(static_cast<const uint8_t*>(s));
  slice.data.refcounted.length = len;
  return Slice(slice);
}

template <>
Slice CopyConstructors<Slice>::FromCopiedString(std::string s) {
  return Slice(grpc_slice_from_cpp_string(std::move(s)));
}

}  // namespace slice_detail

MutableSlice::MutableSlice(const grpc_slice& slice)
    : slice_detail::BaseSlice(slice) {
  GPR_DEBUG_ASSERT(slice.refcount == nullptr || slice.refcount->IsUnique());
}

MutableSlice::~MutableSlice() { grpc_slice_unref_internal(c_slice()); }

Slice Slice::TakeOwned() {
  if (c_slice().refcount == nullptr) {
    return Slice(c_slice());
  }
  if (c_slice().refcount == grpc_slice_refcount::NoopRefcount()) {
    return Slice(grpc_slice_copy(c_slice()));
  }
  return Slice(TakeCSlice());
}

Slice Slice::AsOwned() const {
  if (c_slice().refcount == nullptr) {
    return Slice(c_slice());
  }
  if (c_slice().refcount == grpc_slice_refcount::NoopRefcount()) {
    return Slice(grpc_slice_copy(c_slice()));
  }
  return Slice(grpc_slice_ref_internal(c_slice()));
}

MutableSlice Slice::TakeMutable() {
  if (c_slice().refcount == nullptr) {
    return MutableSlice(c_slice());
  }
  if (c_slice().refcount != grpc_slice_refcount::NoopRefcount() &&
      c_slice().refcount->IsUnique()) {
    return MutableSlice(TakeCSlice());
  }
  return MutableSlice(grpc_slice_copy(c_slice()));
}

Slice::~Slice() { grpc_slice_unref_internal(c_slice()); }

Slice Slice::Ref() const { return Slice(grpc_slice_ref_internal(c_slice())); }

Slice Slice::FromRefcountAndBytes(grpc_slice_refcount* r, const uint8_t* begin,
                                  const uint8_t* end) {
  grpc_slice out;
  out.refcount = r;
  if (r != grpc_slice_refcount::NoopRefcount()) r->Ref();
  out.data.refcounted.bytes = const_cast<uint8_t*>(begin);
  out.data.refcounted.length = end - begin;
  return Slice(out);
}

}  // namespace experimental
}  // namespace grpc_event_engine
