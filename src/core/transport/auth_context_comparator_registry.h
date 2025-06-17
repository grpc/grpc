//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_TRANSPORT_AUTH_CONTEXT_COMPARATOR_REGISTRY_H
#define GRPC_SRC_CORE_TRANSPORT_AUTH_CONTEXT_COMPARATOR_REGISTRY_H

#include <grpc/impl/grpc_types.h>

#include "absl/functional/any_invocable.h"
#include "src/core/lib/channel/channel_args.h"

struct grpc_auth_context;

class AuthContextComparatorRegistry {
 private:
  using Compare = absl::AnyInvocable<bool(const grpc_auth_context*,
                                          const grpc_auth_context*)>;
  using ComparatorMap = std::map<std::string, std::unique_ptr<Compare>>;

 public:
  class Builder {
   public:
    void RegisterComparator(
        std::string name,
        std::unique_ptr<absl::AnyInvocable<bool(const grpc_auth_context*,
                                                const grpc_auth_context*)>>
            comparator) {
      if (comparators_.find(name) != comparators_.end()) {
        LOG(FATAL) << "Duplicate comparator registration: " << name;
      }
      comparators_[name] = std::move(comparator);
    }

    AuthContextComparatorRegistry Build() {
      return AuthContextComparatorRegistry(std::move(comparators_));
    }

   private:
    ComparatorMap comparators_;
  };

  Compare* GetComparator(absl::string_view name) const {
    auto it = comparators_.find(std::string(name));
    if (it == comparators_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

 private:
  explicit AuthContextComparatorRegistry(ComparatorMap comparators)
      : comparators_(std::move(comparators)) {}
  ComparatorMap comparators_;
};

#endif  // GRPC_SRC_CORE_TRANSPORT_AUTH_CONTEXT_COMPARATOR_REGISTRY_H
