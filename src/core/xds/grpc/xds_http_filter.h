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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_HTTP_FILTER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_HTTP_FILTER_H

#include <optional>
#include <string>

#include "src/core/filter/filter_chain.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "upb/reflection/def.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class XdsHttpFilterFactory {
 public:
  virtual ~XdsHttpFilterFactory() = default;

  // Returns the top-level filter config proto message name.
  virtual absl::string_view ConfigProtoName() const = 0;

  // Returns the override filter config proto message name.
  // If empty, no override type is supported.
  virtual absl::string_view OverrideConfigProtoName() const = 0;

  // Loads the proto message into the upb symtab.
  virtual void PopulateSymtab(upb_DefPool* symtab) const = 0;

  // Adds the filter to the builder.
  virtual void AddFilter(FilterChainBuilder& builder,
                         RefCountedPtr<const FilterConfig> config) const = 0;

  // Parses the top-level filter config.
  virtual RefCountedPtr<const FilterConfig> ParseTopLevelConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const = 0;

  // Parses an override config.
  virtual RefCountedPtr<const FilterConfig> ParseOverrideConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const = 0;

  // Returns a new filter config that takes into account any necessary
  // overrides.  May also add objects to config from the blackboard.
  // Base class returns the most specific filter config; subclasses can
  // override if needed.
  virtual RefCountedPtr<const FilterConfig> MergeConfigs(
      RefCountedPtr<const FilterConfig> top_level_config,
      RefCountedPtr<const FilterConfig> virtual_host_override_config,
      RefCountedPtr<const FilterConfig> route_override_config,
      RefCountedPtr<const FilterConfig> cluster_weight_override_config,
      XdsTransportFactory& transport_factory, Blackboard& blackboard) const;

  // Returns true if the filter is supported on clients; false otherwise
  virtual bool IsSupportedOnClients() const = 0;

  // Returns true if the filter is supported on servers; false otherwise
  virtual bool IsSupportedOnServers() const = 0;

  // Returns true if the filter must be the last filter in the chain.
  virtual bool IsTerminalFilter() const { return false; }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_HTTP_FILTER_H
