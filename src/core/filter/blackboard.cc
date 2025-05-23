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

#include "src/core/filter/blackboard.h"

namespace grpc_core {

Blackboard::Entry::~Entry() {
  if (blackboard_ == nullptr) return;
  MutexLock lock(&blackboard_->mu_);
  blackboard_->map_.erase(it_);
}

RefCountedPtr<Blackboard::Entry> Blackboard::Get(UniqueTypeName type,
                                                 const std::string& key) const {
  MutexLock lock(&mu_);
  auto it = map_.find(std::pair(type, key));
  if (it != map_.end() && it->second != nullptr) {
    return it->second->RefIfNonZero();
  }
  return nullptr;
}

RefCountedPtr<Blackboard::Entry> Blackboard::Set(UniqueTypeName type,
                                                 const std::string& key,
                                                 RefCountedPtr<Entry> entry) {
  MutexLock lock(&mu_);
  auto [it, inserted] =
      map_.emplace(std::piecewise_construct, std::forward_as_tuple(type, key),
                   std::forward_as_tuple(entry.get()));
  if (!inserted) {
    auto existing_entry = it->second->RefIfNonZero();
    if (existing_entry != nullptr) return existing_entry;
    it->second = entry.get();
  }
  entry->blackboard_ = Ref();
  entry->it_ = it;
  return entry;
}

}  // namespace grpc_core
