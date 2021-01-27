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

#include "absl/strings/string_view.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {

class XdsHttpFilterImpl {
 public:
  virtual ~XdsHttpFilterImpl() = default;

  // Filter config protobuf type name.
  virtual absl::string_view config_proto_type_name() const = 0;

  // C-core channel filter implementation.
  virtual const grpc_channel_filter* channel_filter() const = 0;

  // Modifies channel args.  Takes ownership of args.
  // Caller takes ownership of return value.
  virtual grpc_channel_args* ModifyChannelArgs(grpc_channel_args* args) const {
    return args;
  }

  // Function to convert the configs returned by the XdsClient into a string
  // to be added to the per-method part of the service config.
  virtual std::string GenerateServiceConfig(
      absl::string_view serialized_hcm_filter_config,
      absl::string_view serialized_virtual_host_config,
      absl::string_view serialized_route_config) const = 0;
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
