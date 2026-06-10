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

#ifndef GRPC_SRC_CORE_XDS_GRPC_BLACKBOARD_H
#define GRPC_SRC_CORE_XDS_GRPC_BLACKBOARD_H

#include <string>
#include <utility>

#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/useful.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A blackboard is a place where dynamic filters can stash global state
// that they may want to retain across resolver updates.  Entries are
// identified by by the unique type and a name that identifies the instance,
// which means that it's possible for two filter instances to use the same
// type (e.g., if there are two instantiations of the same filter).
class Blackboard : public RefCounted<Blackboard> {
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
    UniqueTypeName type_{GRPC_UNIQUE_TYPE_NAME_HERE("")};
    std::string key_;
  };

  // Returns an entry for a particular type and name, or null if not present.
  template <typename T>
  RefCountedPtr<T> Get(const std::string& key) const {
    return Get(T::Type(), key).template TakeAsSubclass<T>();
  }

  // Gets the entry for the specified key.  If not present, invokes
  // construct to create it.  Returns the entry.
  template <typename T>
  RefCountedPtr<T> GetOrSet(const std::string& key,
                            absl::FunctionRef<RefCountedPtr<T>()> construct) {
    return GetOrSet(T::Type(), key, std::move(construct))
        .template TakeAsSubclass<T>();
  }

 private:
  RefCountedPtr<Entry> Get(UniqueTypeName type, const std::string& key) const;
  RefCountedPtr<Entry> GetOrSet(
      UniqueTypeName type, const std::string& key,
      absl::FunctionRef<RefCountedPtr<Entry>()> construct);
  void Remove(UniqueTypeName type, const std::string& key, Entry* entry);

  mutable Mutex mu_;
  absl::flat_hash_map<std::pair<UniqueTypeName, std::string>,
                      WeakRefCountedPtr<Entry>>
      map_ ABSL_GUARDED_BY(&mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_BLACKBOARD_H
