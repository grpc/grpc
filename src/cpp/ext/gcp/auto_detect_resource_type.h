//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_GCP_AUTO_DETECT_RESOURCE_TYPE_H
#define GRPC_SRC_CPP_EXT_GCP_AUTO_DETECT_RESOURCE_TYPE_H

#include <grpc/support/port_platform.h>

#include <map>
#include <string>

namespace grpc {
namespace internal {

class ResourceType {
 public:
  static ResourceType AutoDetect();

  const std::string& resource_type() const { return resource_type_; }

  const std::map<std::string, std::string>& labels() const { return labels_; }

 private:
  // For example, "gce_instance", "gke_container", etc.
  std::string resource_type_;
  // Values for all the labels listed in the associa
  std::map<std::string, std::string> labels_;
};

ResourceType AutoDetectEnvironmentResource();

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_GCP_AUTO_DETECT_RESOURCE_TYPE_H
