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

#ifndef GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_GRPC_H
#define GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_GRPC_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <set>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "upb/arena.h"
#include "upb/def.h"
#include "upb/upb.h"

#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

class GrpcXdsHttpFilter : public XdsHttpFilter {
 public:
  // Service config data for the filter, returned by GenerateServiceConfig().
  struct ServiceConfigJsonEntry {
    // The top-level field name in the method config.
    // Filter implementations should use their primary config proto type
    // name for this.
    // The value of this field in the method config will be a JSON array,
    // which will be populated with the elements returned by each filter
    // instance.
    std::string service_config_field_name;
    // The element to add to the JSON array.
    std::string element;
  };

  // C-core channel filter implementation.
  virtual const grpc_channel_filter* channel_filter() const = 0;

  // Modifies channel args that may affect service config parsing (not
  // visible to the channel as a whole).
  // Takes ownership of args.  Caller takes ownership of return value.
  virtual ChannelArgs ModifyChannelArgs(const ChannelArgs& args) const {
    return args;
  }

  // Function to convert the Configs into a JSON string to be added to the
  // per-method part of the service config.
  // The hcm_filter_config comes from the HttpConnectionManager config.
  // The filter_config_override comes from the first of the ClusterWeight,
  // Route, or VirtualHost entries that it is found in, or null if
  // there is no override in any of those locations.
  virtual absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& hcm_filter_config,
      const FilterConfig* filter_config_override) const = 0;
};

void RegisterGrpcXdsHttpFilters(XdsHttpFilterRegistry* registry);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_GRPC_H
