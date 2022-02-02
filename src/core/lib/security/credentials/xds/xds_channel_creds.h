//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CHANNEL_CREDS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CHANNEL_CREDS_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/json/json.h"

struct grpc_channel_credentials;

namespace grpc_core {

template <typename T = grpc_channel_credentials>
class XdsChannelCredsFactory final {
 public:
  virtual ~XdsChannelCredsFactory() {}
  virtual absl::string_view creds_type() const = delete;
  virtual bool IsValidConfig(const Json& config) const = delete;
  virtual RefCountedPtr<T> CreateXdsChannelCreds(const Json& config) const =
      delete;
};

template <>
class XdsChannelCredsFactory<grpc_channel_credentials> {
 public:
  virtual ~XdsChannelCredsFactory() {}
  virtual absl::string_view creds_type() const = 0;
  virtual bool IsValidConfig(const Json& config) const = 0;
  virtual RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& config) const = 0;
};

template <typename T = grpc_channel_credentials>
class XdsChannelCredsRegistry {
 public:
  static_assert(std::is_base_of<grpc_channel_credentials, T>::value,
                "XdsChannelCredsRegistry must be instantiated with "
                "grpc_channel_credentials.");
  class Builder {
   public:
    void RegisterXdsChannelCredsFactory(
        std::unique_ptr<XdsChannelCredsFactory<T>> factory) {
      factories_[factory->creds_type()] = std::move(factory);
    }
    XdsChannelCredsRegistry Build() {
      XdsChannelCredsRegistry<T> registry;
      registry.factories_.swap(factories_);
      return registry;
    }

   private:
    std::map<absl::string_view, std::unique_ptr<XdsChannelCredsFactory<T>>>
        factories_;
  };

  bool IsSupported(const std::string& creds_type) const {
    return factories_.find(creds_type) != factories_.end();
  }

  bool IsValidConfig(const std::string& creds_type, const Json& config) const {
    const auto iter = factories_.find(creds_type);
    return iter != factories_.cend() && iter->second->IsValidConfig(config);
  }

  RefCountedPtr<T> CreateXdsChannelCreds(const std::string& creds_type,
                                         const Json& config) const {
    const auto iter = factories_.find(creds_type);
    if (iter == factories_.cend()) return nullptr;
    return iter->second->CreateXdsChannelCreds(config);
  }

 private:
  XdsChannelCredsRegistry() = default;
  std::map<absl::string_view, std::unique_ptr<XdsChannelCredsFactory<T>>>
      factories_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CHANNEL_CREDS_H
