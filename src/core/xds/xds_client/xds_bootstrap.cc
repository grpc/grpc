//
// Copyright 2019 gRPC authors.
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

#include "src/core/xds/xds_client/xds_bootstrap.h"

#include "src/core/config/experiment_env_var.h"

namespace grpc_core {

// TODO(roth,apolcyn): remove this federation env var after the 1.55
// release.
bool XdsFederationEnabled() {
  return IsExperimentEnvVarEnabled("GRPC_EXPERIMENTAL_XDS_FEDERATION",
                                   /*default_value=*/true);
}

// TODO(roth): Remove this once the feature passes interop tests.
bool XdsDataErrorHandlingEnabled() {
  return IsExperimentEnvVarEnabled("GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
}

}  // namespace grpc_core
