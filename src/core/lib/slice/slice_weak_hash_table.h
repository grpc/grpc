/*
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
 */

#ifndef GRPC_CORE_LIB_SLICE_WEAK_SLICE_HASH_TABLE_H
#define GRPC_CORE_LIB_SLICE_WEAK_SLICE_HASH_TABLE_H

#include <grpc/support/port_platform.h>

#include <new>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/slice/slice_internal.h"

/// Weak hash table implementation.
///
/// This entries in this table are weak: an entry may be removed at any time due
/// to a number of reasons: memory pressure, hash collisions, etc.
///
/// The keys are \a grpc_slice objects. The values are of arbitrary type.
///
/// This class is thread unsafe. It's the caller's responsibility to ensure
/// proper locking when accessing its methods.

namespace grpc_core {

template <typename T>
class SliceWeakHashTable : public RefCounted<SliceWeakHashTable<T>> {
 public:
  struct Entry {
    grpc_slice key;
    T value;
    bool is_set;
  };

  /// Creates a new table of at most \a size entries.
  static RefCountedPtr<SliceWeakHashTable> Create(size_t size) {
    return MakeRefCounted<SliceWeakHashTable<T>>(size);
  }

  /// Add a mapping from \a key to \a value, taking ownership of \a key. This
  /// operation will always succeed. It may discard older entries.
  void Add(grpc_slice key, T value) {
    const size_t idx = grpc_slice_hash(key) % size_;
    Entry* entry = &entries_[idx];
    if (entry->is_set) grpc_slice_unref_internal(entry->key);
    entries_[idx].key = key;
    entries_[idx].value = std::move(value);
    entries_[idx].is_set = true;
    return;
  }

  /// Returns the value from the table associated with / \a key or null if not
  /// found.
  const T* Get(const grpc_slice key) const {
    const size_t idx = grpc_slice_hash(key) % size_;
    if (!entries_[idx].is_set) return nullptr;
    if (grpc_slice_eq(entries_[idx].key, key)) return &entries_[idx].value;
    return nullptr;
  }

 private:
  // So New() can call our private ctor.
  template <typename T2, typename... Args>
  friend T2* New(Args&&... args);

  SliceWeakHashTable(size_t size) : size_(size) {
    // TODO(dgq): replace the following with a call to the array version of our
    // own New operator.
    entries_ = static_cast<Entry*>(
        alignof(Entry) > kAllignmentForDefaultAllocationInBytes
            ? gpr_malloc_aligned(sizeof(Entry) * size_, alignof(Entry))
            : gpr_malloc(sizeof(Entry) * size_));
    for (size_t i = 0; i < size_; ++i) new (&entries_[i]) Entry();
  }

  ~SliceWeakHashTable() {
    for (size_t i = 0; i < size_; ++i) {
      Entry* entry = &entries_[i];
      if (entry->is_set) {
        grpc_slice_unref_internal(entry->key);
        entry->value.~T();
      }
    }
    gpr_free(entries_);
  }

  const size_t size_;
  Entry* entries_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SLICE_WEAK_SLICE_HASH_TABLE_H */
