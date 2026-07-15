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

#include "src/core/xds/grpc/blackboard.h"

namespace grpc_core {

void Blackboard::Entry::Orphaned() {
  if (blackboard_ != nullptr) blackboard_->Remove(type_, key_, this);
}

RefCountedPtr<Blackboard::Entry> Blackboard::Get(UniqueTypeName type,
                                                 const std::string& key) const {
  MutexLock lock(&mu_);
  auto it = map_.find(std::pair(type, key));
  if (it == map_.end()) return nullptr;
  return it->second->RefIfNonZero();
}

RefCountedPtr<Blackboard::Entry> Blackboard::GetOrSet(
    UniqueTypeName type, const std::string& key,
    absl::FunctionRef<RefCountedPtr<Entry>()> construct) {
  MutexLock lock(&mu_);
  auto& entry = map_[std::pair(type, key)];
  if (entry != nullptr) {
    auto reffed_entry = entry->RefIfNonZero();
    if (reffed_entry != nullptr) return reffed_entry;
  }
  auto constructed = construct();
  entry = constructed->WeakRef();
  constructed->blackboard_ = Ref();
  constructed->type_ = type;
  constructed->key_ = key;
  return constructed;
}

void Blackboard::Remove(UniqueTypeName type, const std::string& key,
                        Entry* entry) {
  MutexLock lock(&mu_);
  auto it = map_.find(std::pair(type, key));
  if (it == map_.end()) return;
  if (it->second == entry) map_.erase(it);
}

}  // namespace grpc_core
