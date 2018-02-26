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

namespace grpc_core {

template <typename T>
class WeakSliceHashTable : public RefCounted<WeakSliceHashTable<T>> {
 public:
  struct Entry {
    grpc_slice key;
    T value;  // Must not be null.
    bool is_set;
  };

  /// Creates a new table of at most \a max_size entries.
  static RefCountedPtr<WeakSliceHashTable> Create(size_t max_size);

  /// Add a mapping from \a key to \a value. This operation will always succeed.
  /// It may discard older entries.
  void Add(grpc_slice key, T value);

  /// Returns a constant pointer to the value from the table associated with \a
  /// key. Returns null if \a key is not found.
  const T* Get(const grpc_slice key) const;

  /// Returns a modifiable pointer to the value from the table associated with
  /// \a key. Returns null if \a key is not found.
  T* Get(const grpc_slice key);

 private:
  // So New() can call our private ctor.
  template <typename T2, typename... Args>
  friend T2* New(Args&&... args);

  WeakSliceHashTable(size_t max_size);
  ~WeakSliceHashTable();

  T* GetInternal(const grpc_slice key);

  const size_t max_size_;
  Entry* entries_;
};

//
// implementation -- no user-serviceable parts below
//

template <typename T>
RefCountedPtr<WeakSliceHashTable<T>> WeakSliceHashTable<T>::Create(
    size_t max_size) {
  return MakeRefCounted<WeakSliceHashTable<T>>(max_size);
}

template <typename T>
WeakSliceHashTable<T>::WeakSliceHashTable(size_t max_size)
    : max_size_(max_size) {
  entries_ = static_cast<Entry*>(gpr_zalloc(sizeof(Entry) * max_size_));
}

template <typename T>
WeakSliceHashTable<T>::~WeakSliceHashTable() {
  for (size_t i = 0; i < max_size_; ++i) {
    Entry* entry = &entries_[i];
    if (entry->is_set) {
      grpc_slice_unref_internal(entry->key);
      entry->value.~T();
    }
  }
  gpr_free(entries_);
}

template <typename T>
T* WeakSliceHashTable<T>::GetInternal(const grpc_slice key) {
  const size_t idx = grpc_slice_hash(key) % max_size_;
  if (!entries_[idx].is_set) return nullptr;
  if (grpc_slice_eq(entries_[idx].key, key)) return &entries_[idx].value;
  return nullptr;
}

template <typename T>
void WeakSliceHashTable<T>::Add(grpc_slice key, T value) {
  const size_t idx = grpc_slice_hash(key) % max_size_;
  Entry* entry = &entries_[idx];
  if (entry->is_set) grpc_slice_unref_internal(entry->key);
  entries_[idx].key = key;
  entries_[idx].value = std::move(value);
  entries_[idx].is_set = true;
  return;
}

template <typename T>
T* WeakSliceHashTable<T>::Get(const grpc_slice key) {
  return GetInternal(key);
}

template <typename T>
const T* WeakSliceHashTable<T>::Get(const grpc_slice key) const {
  return GetInternal(key);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SLICE_WEAK_SLICE_HASH_TABLE_H */
