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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_BLACKBOARD_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_BLACKBOARD_H

#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/useful.h"

namespace grpc_core {

// A blackboard is a place where dynamic filters can stash global state
// that they may want to retain across resolver updates.  Entries are
// identified by by the unique type and a name that identifies the instance,
// which means that it's possible for two filter instances to use the same
// type (e.g., if there are two instantiations of the same filter).
class Blackboard : public RefCounted<Blackboard> {
 public:
  // All entries must derive from this type.
  class Entry : public RefCounted<Entry> {
   public:
    // The unique type of the entry.
    virtual UniqueTypeName type() const = 0;
  };

  // Returns an entry for a particular type and name, or null if not present.
  RefCountedPtr<Entry> Get(UniqueTypeName type, const std::string& key) const;

  template <typename T>
  RefCountedPtr<T> Get(const std::string& key) {
    return Get(T::Type(), key).template TakeAsSubclass<T>();
  }

  // Sets an entry for a particular type and name.
  void Set(UniqueTypeName type, const std::string& key,
           RefCountedPtr<Entry> entry);

  static absl::string_view ChannelArgName() {
    return GRPC_ARG_NO_SUBCHANNEL_PREFIX "internal.blackboard";
  }
  static int ChannelArgsCompare(const Blackboard* a, const Blackboard* b) {
    return QsortCompare(a, b);
  }
  static constexpr bool ChannelArgUseConstPtr() { return true; }

 private:
  absl::flat_hash_map<std::pair<UniqueTypeName, std::string>,
                      RefCountedPtr<Entry>>
      map_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_BLACKBOARD_H
