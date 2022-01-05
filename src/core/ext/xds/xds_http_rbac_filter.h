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

#ifndef GRPC_CORE_EXT_XDS_XDS_HTTP_RBAC_FILTER_H
#define GRPC_CORE_EXT_XDS_XDS_HTTP_RBAC_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_http_filters.h"

namespace grpc_core {

extern const char* kXdsHttpRbacFilterConfigName;
extern const char* kXdsHttpRbacFilterConfigOverrideName;

class XdsHttpRbacFilter : public XdsHttpFilterImpl {
 public:
  void PopulateSymtab(upb_symtab* symtab) const override;

  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_strview serialized_filter_config, upb_arena* arena) const override;

  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_strview serialized_filter_config, upb_arena* arena) const override;

  const grpc_channel_filter* channel_filter() const override;

  grpc_channel_args* ModifyChannelArgs(grpc_channel_args* args) const override;

  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& hcm_filter_config,
      const FilterConfig* filter_config_override) const override;

  bool IsSupportedOnClients() const override { return false; }

  bool IsSupportedOnServers() const override { return true; }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_HTTP_RBAC_FILTER_H
