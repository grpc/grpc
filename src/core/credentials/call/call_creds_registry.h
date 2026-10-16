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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_CALL_CREDS_REGISTRY_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_CALL_CREDS_REGISTRY_H

#include <map>
#include <memory>
#include <type_traits>
#include <utility>

#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/string_view.h"

struct grpc_call_credentials;

namespace grpc_core {

class CallCredsConfig : public RefCounted<CallCredsConfig> {
 public:
  virtual absl::string_view type() const = 0;

  virtual absl::string_view proto_type() const = 0;

  virtual bool Equals(const CallCredsConfig& other) const = 0;

  virtual std::string ToString() const = 0;
};

template <typename T = grpc_call_credentials>
class CallCredsFactory final {
 public:
  virtual ~CallCredsFactory() {}
  virtual absl::string_view type() const = delete;
  virtual RefCountedPtr<const CallCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const = delete;
  virtual absl::string_view proto_type() const = delete;
  virtual RefCountedPtr<const CallCredsConfig> ParseProto(
      absl::string_view serialized_proto,
      ValidationErrors* errors) const = delete;
  virtual RefCountedPtr<T> CreateCallCreds(
      RefCountedPtr<const CallCredsConfig> config) const = delete;
};

template <>
class CallCredsFactory<grpc_call_credentials> {
 public:
  virtual ~CallCredsFactory() {}
  virtual absl::string_view type() const = 0;
  virtual RefCountedPtr<const CallCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const = 0;
  virtual absl::string_view proto_type() const = 0;
  virtual RefCountedPtr<const CallCredsConfig> ParseProto(
      absl::string_view serialized_proto, ValidationErrors* errors) const = 0;
  virtual RefCountedPtr<grpc_call_credentials> CreateCallCreds(
      RefCountedPtr<const CallCredsConfig> config) const = 0;
};

template <typename T = grpc_call_credentials>
class CallCredsRegistry {
 private:
  using FactoryMap =
      std::map<absl::string_view, std::shared_ptr<CallCredsFactory<T>>>;

 public:
  static_assert(std::is_base_of<grpc_call_credentials, T>::value,
                "CallCredsRegistry must be instantiated with "
                "grpc_call_credentials.");

  class Builder {
   public:
    void RegisterCallCredsFactory(
        std::unique_ptr<CallCredsFactory<T>> factory) {
      std::shared_ptr<CallCredsFactory<T>> shared_factory(std::move(factory));
      absl::string_view type = shared_factory->type();
      if (!type.empty()) name_map_[type] = shared_factory;
      absl::string_view proto_type = shared_factory->proto_type();
      if (!proto_type.empty()) proto_map_[proto_type] = shared_factory;
    }

    CallCredsRegistry Build() {
      return CallCredsRegistry<T>(std::move(name_map_), std::move(proto_map_));
    }

   private:
    FactoryMap name_map_;
    FactoryMap proto_map_;
  };

  bool IsSupported(absl::string_view type) const {
    return name_map_.find(type) != name_map_.end();
  }

  RefCountedPtr<const CallCredsConfig> ParseConfig(
      absl::string_view type, const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const {
    const auto it = name_map_.find(type);
    if (it == name_map_.cend()) return nullptr;
    return it->second->ParseConfig(config, args, errors);
  }

  bool IsProtoSupported(absl::string_view type) const {
    return proto_map_.find(type) != proto_map_.end();
  }

  RefCountedPtr<const CallCredsConfig> ParseProto(
      absl::string_view proto_type, absl::string_view serialized_proto,
      ValidationErrors* errors) const {
    const auto it = proto_map_.find(proto_type);
    if (it == proto_map_.cend()) return nullptr;
    return it->second->ParseProto(serialized_proto, errors);
  }

  RefCountedPtr<T> CreateCallCreds(
      RefCountedPtr<const CallCredsConfig> config) const {
    if (config == nullptr) return nullptr;
    auto it = name_map_.find(config->type());
    if (it == name_map_.cend()) {
      it = proto_map_.find(config->proto_type());
      if (it == proto_map_.cend()) return nullptr;
    }
    return it->second->CreateCallCreds(std::move(config));
  }

 private:
  CallCredsRegistry(FactoryMap name_map, FactoryMap proto_map)
      : name_map_(std::move(name_map)), proto_map_(std::move(proto_map)) {}

  FactoryMap name_map_;
  FactoryMap proto_map_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_CALL_CREDS_REGISTRY_H
