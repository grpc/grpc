//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_SERVER_CONFIG_SELECTOR_SERVER_CONFIG_SELECTOR_H
#define GRPC_CORE_EXT_FILTERS_SERVER_CONFIG_SELECTOR_SERVER_CONFIG_SELECTOR_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// ServerConfigSelector allows for choosing the service config to apply to a
// server-side call based on the received initial metadata.
class ServerConfigSelector : public RefCounted<ServerConfigSelector> {
 public:
  // Configuration to apply to an incoming call
  struct CallConfig {
    grpc_error_handle error = GRPC_ERROR_NONE;
    const ServiceConfigParser::ParsedConfigVector* method_configs = nullptr;
    RefCountedPtr<ServiceConfig> service_config;
  };

  ~ServerConfigSelector() override = default;
  // Returns the CallConfig to apply to a call based on the incoming \a metadata
  virtual CallConfig GetCallConfig(grpc_metadata_batch* metadata) = 0;
};

// ServerConfigSelectorProvider allows for subscribers to watch for updates on
// ServerConfigSelector. It is propagated via channel args.
class ServerConfigSelectorProvider
    : public DualRefCounted<ServerConfigSelectorProvider> {
 public:
  class ServerConfigSelectorWatcher {
   public:
    virtual ~ServerConfigSelectorWatcher() = default;
    virtual void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> update) = 0;
  };

  ~ServerConfigSelectorProvider() override = default;
  // Only a single watcher is allowed at present
  virtual absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorWatcher> watcher) = 0;
  virtual void CancelWatch() = 0;

  static absl::string_view ChannelArgName();

  grpc_arg MakeChannelArg() const;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_SERVER_CONFIG_SELECTOR_SERVER_CONFIG_SELECTOR_H
