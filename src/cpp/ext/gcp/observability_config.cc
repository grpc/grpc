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

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/slice.h>
#include <grpc/status.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/memory.h"
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
  grpc_core::UniquePtr<char> path(gpr_getenv("GRPC_OBSERVABILITY_CONFIG_FILE"));
  if (path != nullptr) {
    grpc_slice contents;
    grpc_error_handle error =
        grpc_load_file(path.get(), /*add_null_terminator=*/true, &contents);
    if (!GRPC_ERROR_IS_NONE(error)) {
      return grpc_error_to_absl_status(grpc_error_set_int(
          error, GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_FAILED_PRECONDITION));
    }
    std::string contents_str(grpc_core::StringViewFromSlice(contents));
    grpc_slice_unref_internal(contents);
    return contents_str;
  }
  // Next, try GRPC_OBSERVABILITY_CONFIG env var.
  grpc_core::UniquePtr<char> env_config(
      gpr_getenv("GRPC_OBSERVABILITY_CONFIG"));
  if (env_config != nullptr) {
    return env_config.get();
  }
  // No observability config found.
  return absl::FailedPreconditionError(
      "Environment variables GRPC_OBSERVABILITY_CONFIG_FILE or "
      "GRPC_OBSERVABILITY_CONFIG "
      "not defined");
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
  return grpc_core::LoadFromJson<GcpObservabilityConfig>(*config_json);
}

}  // namespace internal
}  // namespace grpc
