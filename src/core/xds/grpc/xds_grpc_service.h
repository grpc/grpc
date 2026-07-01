//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_GRPC_SERVICE_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_GRPC_SERVICE_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_server_grpc.h"

namespace grpc_core {

struct XdsGrpcService {
  std::unique_ptr<GrpcXdsServerTarget> server_target;
  Duration timeout;
  std::vector<std::pair<std::string, std::string>> initial_metadata;

  bool operator==(const XdsGrpcService& other) const {
    if (timeout != other.timeout) return false;
    if (initial_metadata != other.initial_metadata) return false;
    if (server_target == nullptr) return other.server_target == nullptr;
    if (other.server_target == nullptr) return false;
    return server_target->Equals(*other.server_target);
  }

  std::string ToString() const;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_GRPC_SERVICE_H
