//
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
//

#ifndef GRPC_CORE_EXT_XDS_XDS_SERVER_CONFIG_SELECTOR_H
#define GRPC_CORE_EXT_XDS_XDS_SERVER_CONFIG_SELECTOR_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_server_config_fetcher.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

struct XdsServerConfigSelectorArg
    : public RefCounted<XdsServerConfigSelectorArg> {
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsServerConfigSelectorArg> GetFromChannelArgs(
      const grpc_channel_args& args);

  static const char* kChannelArgName;
  std::string resource_name;  // RDS resource name to watch
  absl::optional<absl::StatusOr<XdsApi::RdsUpdate>>
      rds_update;  // inline config selector update
  XdsServerConfigFetcher* server_config_fetcher;  // Owned by the server object
  std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>
      http_filters;
};

extern const grpc_channel_filter kXdsServerConfigSelectorFilter;

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_SERVER_CONFIG_SELECTOR_H
