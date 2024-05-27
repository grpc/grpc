//
// Copyright 2020 gRPC authors.
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
#include "src/core/lib/security/credentials/external/file_external_account_credentials.h"

#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/slice.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"

namespace grpc_core {

RefCountedPtr<FileExternalAccountCredentials>
FileExternalAccountCredentials::Create(Options options,
                                       std::vector<std::string> scopes,
                                       grpc_error_handle* error) {
  auto creds = MakeRefCounted<FileExternalAccountCredentials>(
      std::move(options), std::move(scopes), error);
  if (error->ok()) {
    return creds;
  } else {
    return nullptr;
  }
}

FileExternalAccountCredentials::FileExternalAccountCredentials(
    Options options, std::vector<std::string> scopes, grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes)) {
  auto it = options.credential_source.object().find("file");
  if (it == options.credential_source.object().end()) {
    *error = GRPC_ERROR_CREATE("file field not present.");
    return;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("file field must be a string.");
    return;
  }
  file_ = it->second.string();
  it = options.credential_source.object().find("format");
  if (it != options.credential_source.object().end()) {
    const Json& format_json = it->second;
    if (format_json.type() != Json::Type::kObject) {
      *error = GRPC_ERROR_CREATE(
          "The JSON value of credential source format is not an object.");
      return;
    }
    auto format_it = format_json.object().find("type");
    if (format_it == format_json.object().end()) {
      *error = GRPC_ERROR_CREATE("format.type field not present.");
      return;
    }
    if (format_it->second.type() != Json::Type::kString) {
      *error = GRPC_ERROR_CREATE("format.type field must be a string.");
      return;
    }
    format_type_ = format_it->second.string();
    if (format_type_ == "json") {
      format_it = format_json.object().find("subject_token_field_name");
      if (format_it == format_json.object().end()) {
        *error = GRPC_ERROR_CREATE(
            "format.subject_token_field_name field must be present if the "
            "format is in Json.");
        return;
      }
      if (format_it->second.type() != Json::Type::kString) {
        *error = GRPC_ERROR_CREATE(
            "format.subject_token_field_name field must be a string.");
        return;
      }
      format_subject_token_field_name_ = format_it->second.string();
    }
  }
}

void FileExternalAccountCredentials::RetrieveSubjectToken(
    HTTPRequestContext* /*ctx*/, const Options& /*options*/,
    std::function<void(std::string, grpc_error_handle)> cb) {
  // To retrieve the subject token, we read the file every time we make a
  // request because it may have changed since the last request.
  auto content_slice = LoadFile(file_, /*add_null_terminator=*/false);
  if (!content_slice.ok()) {
    cb("", content_slice.status());
    return;
  }
  absl::string_view content = content_slice->as_string_view();
  if (format_type_ == "json") {
    auto content_json = JsonParse(content);
    if (!content_json.ok() || content_json->type() != Json::Type::kObject) {
      cb("", GRPC_ERROR_CREATE(
                 "The content of the file is not a valid json object."));
      return;
    }
    auto content_it =
        content_json->object().find(format_subject_token_field_name_);
    if (content_it == content_json->object().end()) {
      cb("", GRPC_ERROR_CREATE("Subject token field not present."));
      return;
    }
    if (content_it->second.type() != Json::Type::kString) {
      cb("", GRPC_ERROR_CREATE("Subject token field must be a string."));
      return;
    }
    cb(content_it->second.string(), absl::OkStatus());
    return;
  }
  cb(std::string(content), absl::OkStatus());
}

absl::string_view FileExternalAccountCredentials::CredentialSourceType() {
  return "file";
}

}  // namespace grpc_core
