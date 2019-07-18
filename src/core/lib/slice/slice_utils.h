/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SLICE_SLICE_UTILS_H
#define GRPC_CORE_LIB_SLICE_SLICE_UTILS_H

#include <grpc/support/port_platform.h>

#include <cstring>

#include <grpc/slice.h>

// Internal slice type declarations.
// Externally, a grpc_slice is a grpc_slice is a grpc_slice.
// Internally, we may have heap allocated slices, static slices, interned
// slices, and inlined slices. If we know the specific type of slice
// we're dealing with, we can save cycles (e.g. fast-paths when we know we don't
// need to take a reference on a slice). Rather than introducing new methods
// ad-hoc in these cases, we rely on type-system backed overloads to keep
// internal APIs clean.
//
// For each overload, the definition and layout of the underlying slice does not
// change; this is purely type-system information.
namespace grpc_core {

// Archtypes: a slice is either extern or not.
// An extern slice is any slice where:
// 1) refcount is null, OR
// 2) refcount is not null and
//                not grpc_slice_refcount::Type::STATIC and
//                not grpc_slice_refcount::Type::INTERNED
//
// Hierarchy:
// ----------------------------------
// |          grpc_slice            |
// |--------------------------------|
// | InternalSlice |    ExternSlice |
// | --------------|                |
// | InternedSlice |                |
// |   StaticSlice |                |
// ----------------------------------
//
struct InternalSlice : public grpc_slice {};
struct ExternSlice : public grpc_slice {
  ExternSlice() {
    refcount = nullptr;
    data.inlined.length = 0;
  }
  explicit ExternSlice(const char* source);
  ExternSlice(const char* source, size_t length);
  explicit ExternSlice(size_t length);
  explicit ExternSlice(size_t length, bool);

 private:
  void HeapInit(size_t length);
};
struct StaticSlice : public InternalSlice {
  StaticSlice(grpc_slice_refcount* ref, size_t length, uint8_t* bytes) {
    refcount = ref;
    data.refcounted.length = length;
    data.refcounted.bytes = bytes;
  }
};
struct InternedSlice : public InternalSlice {
  InternedSlice() = default;
  explicit InternedSlice(const grpc_slice& slice);
  InternedSlice& operator=(const StaticSlice& other) {
    memcpy(this, &other, sizeof(*this));
    return *this;
  }
};

}  // namespace grpc_core

// When we compare two slices, and we know the latter is not inlined, we can
// short circuit our comparison operator. We specifically use differs()
// semantics instead of equals() semantics due to more favourable code
// generation when using differs(). Specifically, we may use the output of
// grpc_slice_differs_refcounted for control flow. If we use differs()
// semantics, we end with a tailcall to memcmp(). If we use equals() semantics,
// we need to invert the result that memcmp provides us, which costs several
// instructions to do so. If we're using the result for control flow (i.e.
// branching based on the output) then we're just performing the extra
// operations to invert the result pointlessly. Concretely, we save 6 ops on
// x86-64/clang with differs().
int grpc_slice_differs_refcounted(const grpc_slice& a,
                                  const grpc_slice& b_not_inline);
// When we compare two slices, and we *know* that one of them is static or
// interned, we can short circuit our slice equality function. The second slice
// here must be static or interned; slice a can be any slice, inlined or not.
inline bool grpc_slice_eq_static_interned(const grpc_slice& a,
                                          const grpc_slice& b_static_interned) {
  if (a.refcount == b_static_interned.refcount) {
    return true;
  }
  return !grpc_slice_differs_refcounted(a, b_static_interned);
}

#endif /* GRPC_CORE_LIB_SLICE_SLICE_UTILS_H */
