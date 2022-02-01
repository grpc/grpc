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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/xds/xds_channel_creds.h"

namespace grpc_core {

bool XdsChannelCredsRegistry::IsSupported(const std::string& creds_type) const {
  return factories_.find(creds_type) != factories_.end();
}

bool XdsChannelCredsRegistry::IsValidConfig(const std::string& creds_type,
                                            const Json& config) const {
  const auto iter = factories_.find(creds_type);
  return iter != factories_.cend() && iter->second->IsValidConfig(config);
}

grpc_channel_credentials* XdsChannelCredsRegistry::CreateXdsChannelCreds(
    const std::string& creds_type, const Json& config) const {
  const auto iter = factories_.find(creds_type);
  if (iter == factories_.cend()) return nullptr;
  return iter->second->CreateXdsChannelCreds(config);
}

void XdsChannelCredsRegistry::Builder::RegisterXdsChannelCredsFactory(
    std::unique_ptr<XdsChannelCredsFactory> factory) {
  factories_[factory->creds_type()] = std::move(factory);
}

XdsChannelCredsRegistry XdsChannelCredsRegistry::Builder::Build() {
  XdsChannelCredsRegistry registry;
  registry.factories_.swap(factories_);
  return registry;
}

}  // namespace grpc_core
