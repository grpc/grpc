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
#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/pluggable_auth_external_account_credentials.h"

#include <initializer_list>
#include <map>
#include <utility>

#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

#include <grpc/support/json.h>

#include "src/core/lib/json/json.h"

#define DEFAULT_EXECUTABLE_TIMEOUT_MS 30000  // 30 seconds
#define MIN_EXECUTABLE_TIMEOUT_MS 5000       // 5 seconds
#define MAX_EXECUTABLE_TIMEOUT_MS 120000     // 120 seconds

namespace grpc_core {

RefCountedPtr<PluggableAuthExternalAccountCredentials>
PluggableAuthExternalAccountCredentials::Create(Options options,
                                                std::vector<std::string> scopes,
                                                grpc_error_handle* error) {
  auto creds = MakeRefCounted<PluggableAuthExternalAccountCredentials>(
      std::move(options), std::move(scopes), error);
  if (error->ok())
    return creds;
  else
    return nullptr;
}

PluggableAuthExternalAccountCredentials::
    PluggableAuthExternalAccountCredentials(Options options,
                                            std::vector<std::string> scopes,
                                            grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes)) {
  auto it = options.credential_source.object().find("executable");
  if (it->second.type() != Json::Type::kObject) {
    *error = GRPC_ERROR_CREATE("executable field must be an object");
    return;
  }
  auto executable_json = it->second;
  auto executable_it = executable_json.object().find("command");
  if (executable_it == executable_json.object().end()) {
    *error = GRPC_ERROR_CREATE("command field not present.");
    return;
  }
  if (executable_it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("command field must be a string.");
    return;
  }
  command_ = executable_it->second.string();
  executable_timeout_ms_ = DEFAULT_EXECUTABLE_TIMEOUT_MS;
  executable_it = executable_json.object().find("timeout_millis");
  if (executable_it != executable_json.object().end()) {
    if (!absl::SimpleAtoi(executable_it->second.string(),
                          &executable_timeout_ms_)) {
      *error = GRPC_ERROR_CREATE("timeout_millis field must be a number.");
      return;
    }
    if (executable_timeout_ms_ > MAX_EXECUTABLE_TIMEOUT_MS ||
        executable_timeout_ms_ < MIN_EXECUTABLE_TIMEOUT_MS) {
      *error = GRPC_ERROR_CREATE(absl::StrFormat(
          "timeout_millis should be between %d and %d milliseconds.",
          MIN_EXECUTABLE_TIMEOUT_MS, MAX_EXECUTABLE_TIMEOUT_MS));
      return;
    }
  }
  executable_it = executable_json.object().find("output_file");
  if (executable_it != executable_json.object().end()) {
    if (executable_it->second.type() != Json::Type::kString) {
      *error = GRPC_ERROR_CREATE("output_file field must be a string.");
      return;
    }
    output_file_path_ = executable_it->second.string();
  }
}

void PluggableAuthExternalAccountCredentials::RetrieveSubjectToken(
    HTTPRequestContext* ctx, const Options& /*options*/,
    std::function<void(std::string, grpc_error_handle)> cb) {
  cb_ = cb;
  // TODO: Execute command
}

}  // namespace grpc_core
