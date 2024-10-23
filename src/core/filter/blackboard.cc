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

RefCountedPtr<Blackboard::Entry> Blackboard::Get(UniqueTypeName type,
                                                 const std::string& key) const {
  auto it = map_.find(std::make_pair(type, key));
  if (it == map_.end()) return nullptr;
  return it->second;
}

void Blackboard::Set(UniqueTypeName type, const std::string& key,
                     RefCountedPtr<Entry> entry) {
  map_[std::make_pair(type, key)] = std::move(entry);
}

}  // namespace grpc_core
