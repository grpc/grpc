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

#ifndef GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H
#define GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.upb.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

class XdsHttpFilterImpl {
 public:
  struct FilterConfig {
    absl::string_view config_proto_type_name;
    Json config;

    bool operator==(const FilterConfig& other) const {
      return config_proto_type_name == other.config_proto_type_name &&
             config == other.config;
    }
    std::string ToString() const {
      return absl::StrCat("{config_proto_type_name=", config_proto_type_name,
                          " config=", config.Dump(), "}");
    }
  };

  virtual ~XdsHttpFilterImpl() = default;

  // Filter config protobuf type name.
  virtual absl::string_view config_proto_type_name() const = 0;

  // Generates a Config from the xDS filter config proto.
  // Used for the top-level config in the HCM HTTP filter list.
  virtual absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_strview serialized_xds_config, upb_arena* arena) const = 0;

  // Generates a Config from the xDS filter config proto.
  // Used for the typed_per_filter_config override in VirtualHost and Route.
  virtual absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_strview serialized_xds_config, upb_arena* arena) const = 0;

  // C-core channel filter implementation.
  virtual const grpc_channel_filter* channel_filter() const = 0;

  // Modifies channel args.  Takes ownership of args.
  // Caller takes ownership of return value.
  virtual grpc_channel_args* ModifyChannelArgs(grpc_channel_args* args) const {
    return args;
  }

  // Function to convert the Configs into a JSON string to be added to the
  // per-method part of the service config.
  virtual std::string GenerateServiceConfig(
      const FilterConfig& hcm_filter_config,
      const FilterConfig& virtual_host_filter_config,
      const FilterConfig& route_filter_config) const = 0;
};

class XdsHttpFilterRegistry {
 public:
  static XdsHttpFilterImpl* GetFilterForType(absl::string_view proto_type_name);

  static void RegisterFilter(std::unique_ptr<XdsHttpFilterImpl> filter);

  // Global init and shutdown.
  static void Init();
  static void Shutdown();
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H */
