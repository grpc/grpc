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
#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/file_external_account_credentials.h"

#include <fstream>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_utils.h"

namespace grpc_core {

RefCountedPtr<FileExternalAccountCredentials>
FileExternalAccountCredentials::Create(Options options,
                                       std::vector<std::string> scopes,
                                       grpc_error_handle* error) {
  auto creds = MakeRefCounted<FileExternalAccountCredentials>(
      std::move(options), std::move(scopes), error);
  if (*error == GRPC_ERROR_NONE) {
    return creds;
  } else {
    return nullptr;
  }
}

FileExternalAccountCredentials::FileExternalAccountCredentials(
    Options options, std::vector<std::string> scopes, grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes)) {
  auto it = options.credential_source.object_value().find("file");
  if (it == options.credential_source.object_value().end()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("file field not present.");
    return;
  }
  if (it->second.type() != Json::Type::STRING) {
    *error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("file field must be a string.");
    return;
  }
  file_ = it->second.string_value();
  it = options.credential_source.object_value().find("format");
  if (it != options.credential_source.object_value().end()) {
    const Json& format_json = it->second;
    if (format_json.type() != Json::Type::OBJECT) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "The JSON value of credential source format is not an object.");
      return;
    }
    auto format_it = format_json.object_value().find("type");
    if (format_it == format_json.object_value().end()) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "format.type field not present.");
      return;
    }
    if (format_it->second.type() != Json::Type::STRING) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "format.type field must be a string.");
      return;
    }
    format_type_ = format_it->second.string_value();
    if (format_type_ == "json") {
      format_it = format_json.object_value().find("subject_token_field_name");
      if (format_it == format_json.object_value().end()) {
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "format.subject_token_field_name field must be present if the "
            "format is in Json.");
        return;
      }
      if (format_it->second.type() != Json::Type::STRING) {
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "format.subject_token_field_name field must be a string.");
        return;
      }
      format_subject_token_field_name_ = format_it->second.string_value();
    }
  }
}

void FileExternalAccountCredentials::RetrieveSubjectToken(
    HTTPRequestContext* /*ctx*/, const Options& /*options*/,
    std::function<void(std::string, grpc_error_handle)> cb) {
  struct SliceWrapper {
    ~SliceWrapper() { grpc_slice_unref_internal(slice); }
    grpc_slice slice = grpc_empty_slice();
  };
  SliceWrapper content_slice;
  // To retrieve the subject token, we read the file every time we make a
  // request because it may have changed since the last request.
  grpc_error_handle error =
      grpc_load_file(file_.c_str(), 0, &content_slice.slice);
  if (error != GRPC_ERROR_NONE) {
    cb("", error);
    return;
  }
  absl::string_view content = StringViewFromSlice(content_slice.slice);
  if (format_type_ == "json") {
    Json content_json = Json::Parse(content, &error);
    if (error != GRPC_ERROR_NONE || content_json.type() != Json::Type::OBJECT) {
      cb("", GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                 "The content of the file is not a valid json object."));
      GRPC_ERROR_UNREF(error);
      return;
    }
    auto content_it =
        content_json.object_value().find(format_subject_token_field_name_);
    if (content_it == content_json.object_value().end()) {
      cb("", GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                 "Subject token field not present."));
      return;
    }
    if (content_it->second.type() != Json::Type::STRING) {
      cb("", GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                 "Subject token field must be a string."));
      return;
    }
    cb(content_it->second.string_value(), GRPC_ERROR_NONE);
    return;
  }
  cb(std::string(content), GRPC_ERROR_NONE);
}

}  // namespace grpc_core
