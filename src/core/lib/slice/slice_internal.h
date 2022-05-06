/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H
#define GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_refcount.h"

void grpc_slice_buffer_reset_and_unref_internal(grpc_slice_buffer* sb);
void grpc_slice_buffer_partial_unref_internal(grpc_slice_buffer* sb,
                                              size_t idx);
void grpc_slice_buffer_destroy_internal(grpc_slice_buffer* sb);

// Returns a pointer to the first slice in the slice buffer without giving
// ownership to or a reference count on that slice.
inline grpc_slice* grpc_slice_buffer_peek_first(grpc_slice_buffer* sb) {
  GPR_DEBUG_ASSERT(sb->count > 0);
  return &sb->slices[0];
}

// Removes the first slice from the slice buffer.
void grpc_slice_buffer_remove_first(grpc_slice_buffer* sb);

// Calls grpc_slice_sub with the given parameters on the first slice.
void grpc_slice_buffer_sub_first(grpc_slice_buffer* sb, size_t begin,
                                 size_t end);

void grpc_test_only_set_slice_hash_seed(uint32_t seed);
// if slice matches a static slice, returns the static slice
// otherwise returns the passed in slice (without reffing it)
// used for surface boundaries where we might receive an un-interned static
// string
grpc_slice grpc_slice_maybe_static_intern(grpc_slice slice,
                                          bool* returned_slice_is_different);
uint32_t grpc_static_slice_hash(grpc_slice s);
int grpc_static_slice_eq(grpc_slice a, grpc_slice b);

inline uint32_t grpc_slice_hash_internal(const grpc_slice& s) {
  return gpr_murmur_hash3(GRPC_SLICE_START_PTR(s), GRPC_SLICE_LENGTH(s),
                          grpc_core::g_hash_seed);
}

grpc_slice grpc_slice_from_moved_buffer(grpc_core::UniquePtr<char> p,
                                        size_t len);
grpc_slice grpc_slice_from_moved_string(grpc_core::UniquePtr<char> p);
grpc_slice grpc_slice_from_cpp_string(std::string str);

// Returns the memory used by this slice, not counting the slice structure
// itself. This means that inlined and slices from static strings will return
// 0. All other slices will return the size of the allocated chars.
size_t grpc_slice_memory_usage(grpc_slice s);

namespace grpc_core {

struct SliceHash {
  std::size_t operator()(const grpc_slice& slice) const {
    return grpc_slice_hash_internal(slice);
  }
};

extern uint32_t g_hash_seed;

// Converts grpc_slice to absl::string_view.
inline absl::string_view StringViewFromSlice(const grpc_slice& slice) {
  return absl::string_view(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(slice)),
      GRPC_SLICE_LENGTH(slice));
}

}  // namespace grpc_core

inline bool operator==(const grpc_slice& s1, const grpc_slice& s2) {
  return grpc_slice_eq(s1, s2);
}

#endif /* GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H */
