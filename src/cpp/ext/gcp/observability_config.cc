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

#include <stddef.h>

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/slice.h>
#include <grpc/status.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc {
namespace internal {

namespace {

// Loads the contents of the file pointed by env var
// GRPC_GCP_OBSERVABILITY_CONFIG_FILE. If unset, falls back to the contents of
// GRPC_GCP_OBSERVABILITY_CONFIG.
absl::StatusOr<std::string> GetGcpObservabilityConfigContents() {
  // First, try GRPC_GCP_OBSERVABILITY_CONFIG_FILE
  std::string contents_str;
  auto path = grpc_core::GetEnv("GRPC_GCP_OBSERVABILITY_CONFIG_FILE");
  if (path.has_value() && !path.value().empty()) {
    grpc_slice contents;
    grpc_error_handle error =
        grpc_load_file(path->c_str(), /*add_null_terminator=*/true, &contents);
    if (!error.ok()) {
      return grpc_error_to_absl_status(
          grpc_error_set_int(error, grpc_core::StatusIntProperty::kRpcStatus,
                             GRPC_STATUS_FAILED_PRECONDITION));
    }
    std::string contents_str(grpc_core::StringViewFromSlice(contents));
    grpc_slice_unref(contents);
    return std::move(contents_str);
  }
  // Next, try GRPC_GCP_OBSERVABILITY_CONFIG env var.
  auto env_config = grpc_core::GetEnv("GRPC_GCP_OBSERVABILITY_CONFIG");
  if (env_config.has_value() && !env_config.value().empty()) {
    return std::move(*env_config);
  }
  // No observability config found.
  return absl::FailedPreconditionError(
      "Environment variables GRPC_GCP_OBSERVABILITY_CONFIG_FILE or "
      "GRPC_GCP_OBSERVABILITY_CONFIG "
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

//
// GcpObservabilityConfig::CloudLogging::RpcEventConfiguration
//

const grpc_core::JsonLoaderInterface*
GcpObservabilityConfig::CloudLogging::RpcEventConfiguration::JsonLoader(
    const grpc_core::JsonArgs&) {
  static const auto* loader =
      grpc_core::JsonObjectLoader<RpcEventConfiguration>()
          .OptionalField("methods", &RpcEventConfiguration::qualified_methods)
          .OptionalField("exclude", &RpcEventConfiguration::exclude)
          .OptionalField("max_metadata_bytes",
                         &RpcEventConfiguration::max_metadata_bytes)
          .OptionalField("max_message_bytes",
                         &RpcEventConfiguration::max_message_bytes)
          .Finish();
  return loader;
}

void GcpObservabilityConfig::CloudLogging::RpcEventConfiguration::JsonPostLoad(
    const grpc_core::Json& /* json */, const grpc_core::JsonArgs& /* args */,
    grpc_core::ValidationErrors* errors) {
  grpc_core::ValidationErrors::ScopedField methods_field(errors, ".methods");
  parsed_methods.reserve(qualified_methods.size());
  for (size_t i = 0; i < qualified_methods.size(); ++i) {
    grpc_core::ValidationErrors::ScopedField methods_index(
        errors, absl::StrCat("[", i, "]"));
    std::vector<absl::string_view> parts =
        absl::StrSplit(qualified_methods[i], '/', absl::SkipEmpty());
    if (parts.size() > 2) {
      errors->AddError("methods[] can have at most a single '/'");
      continue;
    } else if (parts.empty()) {
      errors->AddError("Empty configuration");
      continue;
    } else if (parts.size() == 1) {
      if (parts[0] != "*") {
        errors->AddError("Illegal methods[] configuration");
        continue;
      }
      if (exclude) {
        errors->AddError(
            "Wildcard match '*' not allowed when 'exclude' is set");
        continue;
      }
      parsed_methods.push_back(ParsedMethod{parts[0], ""});
    } else {
      // parts.size() == 2
      if (absl::StrContains(parts[0], '*')) {
        errors->AddError("Configuration of type '*/method' not allowed");
        continue;
      }
      if (absl::StrContains(parts[1], '*') && parts[1].size() != 1) {
        errors->AddError("Wildcard specified for method in incorrect manner");
        continue;
      }
      parsed_methods.push_back(ParsedMethod{parts[0], parts[1]});
    }
  }
}

absl::StatusOr<GcpObservabilityConfig> GcpObservabilityConfig::ReadFromEnv() {
  std::cout << "GetGcpObservabilityConfigContents... " << std::endl;
  auto config_contents = GetGcpObservabilityConfigContents();
  if (!config_contents.ok()) {
    return config_contents.status();
  }
  std::cout << "grpc_core::JsonParse... " << std::endl;
  auto config_json = grpc_core::JsonParse(*config_contents);
  if (!config_json.ok()) {
    std::cout << "grpc_core::JsonParse !config_json.ok()... " << std::endl;
    return config_json.status();
  }
  std::cout << "grpc_core::LoadFromJson... " << std::endl;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(*config_json);
  if (!config.ok()) {
    std::cout << "grpc_core::LoadFromJson !config_json.ok()... " << std::endl;
    return config.status();
  }
  if (config->project_id.empty()) {
    std::cout << "project_id.empty()... " << std::endl;
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
