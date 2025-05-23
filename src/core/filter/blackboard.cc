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

void Blackboard::Entry::Orphaned() {
  if (blackboard_ != nullptr) blackboard_->Remove(key_, this);
}

size_t Blackboard::ShardIndex(const Key& key) {
  return absl::HashOf(key) % kShards;
}

RefCountedPtr<Blackboard::Entry> Blackboard::Get(const Key& key) const {
  auto shard_index = ShardIndex(key);
  auto& read_shard = read_shards_[shard_index];
  read_shard.mu.Lock();
  auto map = read_shard.map;
  read_shard.mu.Unlock();
  auto* entry = map.Lookup(key);
  if (entry == nullptr) return nullptr;
  return (*entry)->RefIfNonZero();
}

RefCountedPtr<Blackboard::Entry> Blackboard::Set(
    const Key& key, RefCountedPtr<Entry> constructed) {
  auto shard_index = ShardIndex(key);
  auto& write_shard = write_shards_[shard_index];
  auto& read_shard = read_shards_[shard_index];
  Map old_map1;
  Map old_map2;
  MutexLock lock(&write_shard.mu);
  auto* existing = write_shard.map.Lookup(key);
  if (existing != nullptr) return (*existing)->RefIfNonZero();
  constructed->blackboard_ = Ref();
  constructed->key_ = key;
  old_map1 = std::exchange(write_shard.map,
                           write_shard.map.Add(key, constructed->WeakRef()));
  MutexLock lock_read(&read_shard.mu);
  old_map2 = std::exchange(read_shard.map, write_shard.map);
  return constructed;
}

void Blackboard::Remove(const Key& key, Entry* entry) {
  auto shard_index = ShardIndex(key);
  auto& write_shard = write_shards_[shard_index];
  auto& read_shard = read_shards_[shard_index];
  Map old_map1;
  Map old_map2;
  MutexLock lock(&write_shard.mu);
  auto* existing = write_shard.map.Lookup(key);
  // Delete only if key hasn't been re-registered to a different entry
  // between strong-unreffing and removing of entry.
  if (existing == nullptr || existing->get() != entry) return;
  old_map1 = std::exchange(write_shard.map, write_shard.map.Remove(key));
  MutexLock lock_read(&read_shard.mu);
  old_map2 = std::exchange(read_shard.map, write_shard.map);
}

}  // namespace grpc_core
