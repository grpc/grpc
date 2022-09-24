//
// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/gcp/observability_config.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/slice.h>
#include <grpc/status.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc {
namespace internal {

namespace {

// Loads the contents of the file pointed by env var
// GRPC_OBSERVABILITY_CONFIG_FILE. If unset, falls back to the contents of
// GRPC_OBSERVABILITY_CONFIG.
absl::StatusOr<std::string> GetGcpObservabilityConfigContents() {
  // First, try GRPC_OBSERVABILITY_CONFIG_FILE
  std::string contents_str;
  auto path = grpc_core::GetEnv("GRPC_OBSERVABILITY_CONFIG_FILE");
  if (path.has_value()) {
    grpc_slice contents;
    grpc_error_handle error =
        grpc_load_file(path->c_str(), /*add_null_terminator=*/true, &contents);
    if (!GRPC_ERROR_IS_NONE(error)) {
      return grpc_error_to_absl_status(grpc_error_set_int(
          error, GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_FAILED_PRECONDITION));
    }
    std::string contents_str(grpc_core::StringViewFromSlice(contents));
    grpc_slice_unref_internal(contents);
    return std::move(contents_str);
  }
  // Next, try GRPC_OBSERVABILITY_CONFIG env var.
  auto env_config = grpc_core::GetEnv("GRPC_OBSERVABILITY_CONFIG");
  if (env_config.has_value()) {
    return std::move(*env_config);
  }
  // No observability config found.
  return absl::FailedPreconditionError(
      "Environment variables GRPC_OBSERVABILITY_CONFIG_FILE or "
      "GRPC_OBSERVABILITY_CONFIG "
      "not defined");
}

// Tries to get the GCP Project ID from environment variables, or returns an
// empty string if not found.
std::string GetProjectIdFromGcpEnvVar() {
  // First check GCP_PROEJCT
  absl::optional<std::string> project_id = grpc_core::GetEnv("GCP_PROJECT");
  if (project_id.has_value() && !project_id->empty()) {
    return project_id.value();
  }
  // Next, try GCLOUD_PROJECT
  project_id = grpc_core::GetEnv("GCLOUD_PROJECT");
  if (project_id.has_value() && !project_id->empty()) {
    return project_id.value();
  }
  // Lastly, try GOOGLE_CLOUD_PROJECT
  project_id = grpc_core::GetEnv("GOOGLE_CLOUD_PROJECT");
  if (project_id.has_value() && !project_id->empty()) {
    return project_id.value();
  }
  return "";
}

}  // namespace

absl::StatusOr<GcpObservabilityConfig> GcpObservabilityConfig::ReadFromEnv() {
  auto config_contents = GetGcpObservabilityConfigContents();
  if (!config_contents.ok()) {
    return config_contents.status();
  }
  auto config_json = grpc_core::Json::Parse(*config_contents);
  if (!config_json.ok()) {
    return config_json.status();
  }
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(*config_json);
  if (!config.ok()) {
    return config.status();
  }
  if (config->project_id.empty()) {
    // Get project ID from GCP environment variables since project ID was not
    // set it in the GCP observability config.
    config->project_id = GetProjectIdFromGcpEnvVar();
    if (config->project_id.empty()) {
      // Could not find project ID from GCP environment variables either.
      return absl::FailedPreconditionError("GCP Project ID not found.");
    }
  }
  return config;
}

}  // namespace internal
}  // namespace grpc
