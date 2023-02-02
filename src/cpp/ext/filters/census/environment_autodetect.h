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

#ifndef GRPC_SRC_CPP_EXT_FILTERS_CENSUS_ENVIRONMENT_AUTODETECT_H
#define GRPC_SRC_CPP_EXT_FILTERS_CENSUS_ENVIRONMENT_AUTODETECT_H

#include <grpc/support/port_platform.h>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/iomgr/polling_entity.h"

namespace grpc {
namespace internal {

class EnvironmentAutoDetect {
 public:
  struct ResourceType {
    // For example, "gce_instance", "gke_container", etc.
    std::string resource_type;
    // Values for all the labels listed in the associa
    std::map<std::string, std::string> labels;
  };

  static EnvironmentAutoDetect& Get();
  // Use this, if project_id is already known
  static EnvironmentAutoDetect& GetWithProjectId(std::string project_id);

  // Provides \a pollent that might be uesd by EnvironmentAutoDetect for
  // detecting the environment, and \a callback that will be invoked once the
  // environment is done being detected.
  void NotifyOnDone(grpc_polling_entity* pollent,
                    absl::AnyInvocable<void()> callback);

  const ResourceType* resource() {
    grpc_core::MutexLock lock(&mu_);
    return resource_.get();
  }

 private:
  explicit EnvironmentAutoDetect(std::string project_id)
      : project_id_(std::move(project_id)) {}

  const std::string project_id_;
  grpc_core::Mutex mu_;
  std::unique_ptr<ResourceType> resource_ ABSL_GUARDED_BY(mu_);
  grpc_polling_entity* pollent_ ABSL_GUARDED_BY(mu_) = nullptr;
  std::vector<absl::AnyInvocable<void()>> callbacks_ ABSL_GUARDED_BY(mu_);
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_FILTERS_CENSUS_ENVIRONMENT_AUTODETECT_H
