// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_EVENT_ENGINE_CHANNEL_ARGS_H
#define GRPC_EVENT_ENGINE_CHANNEL_ARGS_H

#include <grpc/support/port_platform.h>

#include "absl/container/flat_hash_map.h"
#include "absl/types/variant.h"

namespace grpc_event_engine {
namespace experimental {

/// An unordered map of key-value pairs representing Endpoint configuration
/// options.
///
/// This class does not take ownership of any raw pointers passed to it.
class EndpointConfig {
 public:
  template <typename T>
  class Type {
   public:
    explicit Type(T t) : val_(t){};
    T val() const { return val_; }

   private:
    T val_;
  };
  using IntType = Type<int>;
  using BoolType = Type<bool>;
  using StrType = Type<std::string>;
  using PtrType = Type<void*>;
  using Setting =
      absl::variant<absl::monostate, IntType, BoolType, StrType, PtrType>;
  EndpointConfig() = default;
  ~EndpointConfig() = default;
  Setting& operator[](const std::string& key);
  Setting& operator[](std::string&& key);
  const Setting& at(const std::string& key) const;
  /// Execute a callback \a cb for every Setting in the EndpointConfig. The
  /// callback may return false to stop enumeration.
  void enumerate(
      std::function<bool(absl::string_view, const Setting&)> cb) const;
  void clear();
  size_t size() const;
  bool contains(absl::string_view key) const;

 private:
  absl::flat_hash_map<std::string, Setting> map_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_CHANNEL_ARGS_H
