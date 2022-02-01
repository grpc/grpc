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

class XdsChannelCredsFactory {
 public:
  virtual ~XdsChannelCredsFactory() {}
  virtual absl::string_view creds_type() const = 0;
  virtual bool IsValidConfig(const Json& config) const = 0;
  virtual grpc_channel_credentials* CreateXdsChannelCreds(
      const Json& config) const = 0;
};

class XdsChannelCredsRegistry {
 public:
  class Builder {
   public:
    void RegisterXdsChannelCredsFactory(
        std::unique_ptr<XdsChannelCredsFactory> factory);
    XdsChannelCredsRegistry Build();

   private:
    std::map<absl::string_view, std::unique_ptr<XdsChannelCredsFactory>>
        factories_;
  };

  bool IsSupported(const std::string& creds_type) const;
  bool IsValidConfig(const std::string& creds_type, const Json& config) const;
  grpc_channel_credentials* CreateXdsChannelCreds(const std::string& creds_type,
                                                  const Json& config) const;

 private:
  XdsChannelCredsRegistry() = default;
  std::map<absl::string_view, std::unique_ptr<XdsChannelCredsFactory>>
      factories_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CHANNEL_CREDS_H
