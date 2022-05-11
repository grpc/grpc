//
// Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resolver/resolver_attributes.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

ResolverAttributeMap::ResolverAttributeMap(
    std::vector<std::unique_ptr<AttributeInterface>> attributes) {
  for (auto& attribute : attributes) {
    map_.emplace(attribute->type(), std::move(attribute));
  }
}

ResolverAttributeMap::ResolverAttributeMap(const ResolverAttributeMap& other) {
  for (const auto& p : other.map_) {
    map_.emplace(p.second->type(), p.second->Copy());
  }
}

ResolverAttributeMap& ResolverAttributeMap::operator=(
    const ResolverAttributeMap& other) {
  if (&other != this) {
    map_.clear();
    for (const auto& p : other.map_) {
      map_.emplace(p.second->type(), p.second->Copy());
    }
  }
  return *this;
}

ResolverAttributeMap::ResolverAttributeMap(
    ResolverAttributeMap&& other) noexcept
    : map_(std::move(other.map_)) {}

ResolverAttributeMap& ResolverAttributeMap::operator=(
    ResolverAttributeMap&& other) noexcept {
  map_ = std::move(other.map_);
  return *this;
}

int ResolverAttributeMap::Compare(const ResolverAttributeMap& other) const {
  auto it = other.map_.begin();
  for (const auto& p : map_) {
    // other has fewer elements than this
    if (it == other.map_.end()) return -1;
    // compare key pointer addresses
    int retval = QsortCompare(p.first, it->first);
    if (retval != 0) return retval;
    // compare values
    retval = p.second->Compare(it->second.get());
    if (retval != 0) return retval;
    ++it;
  }
  // this has fewer elements than other
  if (it != other.map_.end()) return 1;
  // equal
  return 0;
}

const ResolverAttributeMap::AttributeInterface* ResolverAttributeMap::Get(
    const char* key) const {
  auto it = map_.find(key);
  if (it == map_.end()) return nullptr;
  return it->second.get();
}

void ResolverAttributeMap::Set(std::unique_ptr<AttributeInterface> attribute) {
  map_[attribute->type()] = std::move(attribute);
}

void ResolverAttributeMap::Remove(const char* key) { map_.erase(key); }

std::string ResolverAttributeMap::ToString() const {
  std::vector<std::string> attrs;
  for (const auto& p : map_) {
    attrs.emplace_back(absl::StrCat(p.first, "=", p.second->ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(attrs, ", "), "}");
}

}  // namespace grpc_core
