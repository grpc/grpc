//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_FILTER_BLACKBOARD_H
#define GRPC_SRC_CORE_FILTER_BLACKBOARD_H

#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/avl.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/useful.h"

namespace grpc_core {

// A blackboard is a place where dynamic filters can stash global state
// that they may want to retain across resolver updates.  Entries are
// identified by by the unique type and a name that identifies the instance,
// which means that it's possible for two filter instances to use the same
// type (e.g., if there are two instantiations of the same filter).
class Blackboard : public RefCounted<Blackboard> {
 private:
  using Key = std::pair<UniqueTypeName, std::string>;

 public:
  // All entries must derive from this type.
  // They must also have a static method with the following signature:
  //  static UniqueTypeName Type();
  class Entry : public DualRefCounted<Entry> {
   public:
    void Orphaned() final;

   private:
    friend class Blackboard;

    RefCountedPtr<Blackboard> blackboard_;
    Key key_ = {GRPC_UNIQUE_TYPE_NAME_HERE(""), ""};
  };

  // Returns an entry for a particular type and name, or null if not present.
  template <typename T>
  RefCountedPtr<T> Get(const std::string& key) const {
    return Get({T::Type(), key}).template TakeAsSubclass<T>();
  }

  // Sets an entry for a particular type and name if it doesn't already
  // exist.  Returns the actual entry, which may not be the one passed in
  // if the entry already exists.
  template <typename T>
  RefCountedPtr<T> Set(const std::string& key, RefCountedPtr<T> constructed) {
    return Set({T::Type(), key}, std::move(constructed))
        .template TakeAsSubclass<T>();
  }

 private:
  static const size_t kShards = 3;  // Can increase as needed.
  using Map = AVL<Key, WeakRefCountedPtr<Entry>>;
  struct LockedMap {
    mutable Mutex mu;
    Map map ABSL_GUARDED_BY(mu);
  };
  using ShardedMap = std::array<LockedMap, kShards>;

  static size_t ShardIndex(const Key& key);

  RefCountedPtr<Entry> Get(const Key& key) const;
  RefCountedPtr<Entry> Set(const Key& key, RefCountedPtr<Entry> constructed);
  void Remove(const Key& key, Entry* entry);

  ShardedMap write_shards_;
  ShardedMap read_shards_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_BLACKBOARD_H
