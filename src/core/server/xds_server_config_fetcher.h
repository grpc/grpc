//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_SERVER_XDS_SERVER_CONFIG_FETCHER_H
#define GRPC_SRC_CORE_SERVER_XDS_SERVER_CONFIG_FETCHER_H

#include <string>

#include "src/core/util/ref_counted.h"
#include "src/core/util/useful.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Generates the LDS resource name to watch for a given listening address.
// An implementation can be supplied via a channel arg to override the default
// behavior of formatting the resource name from the bootstrap's
// server_listener_resource_name_template.
class XdsResourceNameGenerator : public RefCounted<XdsResourceNameGenerator> {
 public:
  virtual std::string GetResourceName(absl::string_view listening_address) = 0;

  static absl::string_view ChannelArgName() {
    return "grpc.xds_resource_name_generator";
  }
  static int ChannelArgsCompare(const XdsResourceNameGenerator* a,
                                const XdsResourceNameGenerator* b) {
    return QsortCompare(a, b);
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVER_XDS_SERVER_CONFIG_FETCHER_H
