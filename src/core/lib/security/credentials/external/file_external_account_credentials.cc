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

#include <grpc/slice.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/load_file.h"

namespace grpc_core {

//
// FileExternalAccountCredentials::FileFetchBody
//

FileExternalAccountCredentials::FileFetchBody::FileFetchBody(
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done,
    FileExternalAccountCredentials* creds)
    : FetchBody(std::move(on_done)), creds_(creds) {
  // Start work asynchronously, since we can't invoke the callback
  // synchronously without causing a deadlock.
  creds->event_engine().Run([self = RefAsSubclass<FileFetchBody>()]() mutable {
    ApplicationCallbackExecCtx application_exec_ctx;
    ExecCtx exec_ctx;
    self->ReadFile();
    self.reset();
  });
}

void FileExternalAccountCredentials::FileFetchBody::ReadFile() {
  // To retrieve the subject token, we read the file every time we make a
  // request because it may have changed since the last request.
  auto content_slice = LoadFile(creds_->file_, /*add_null_terminator=*/false);
  if (!content_slice.ok()) {
    Finish(content_slice.status());
    return;
  }
  absl::string_view content = content_slice->as_string_view();
  if (creds_->format_type_ == "json") {
    auto content_json = JsonParse(content);
    if (!content_json.ok() || content_json->type() != Json::Type::kObject) {
      Finish(GRPC_ERROR_CREATE(
          "The content of the file is not a valid json object."));
      return;
    }
    auto content_it =
        content_json->object().find(creds_->format_subject_token_field_name_);
    if (content_it == content_json->object().end()) {
      Finish(GRPC_ERROR_CREATE("Subject token field not present."));
      return;
    }
    if (content_it->second.type() != Json::Type::kString) {
      Finish(GRPC_ERROR_CREATE("Subject token field must be a string."));
      return;
    }
    Finish(content_it->second.string());
    return;
  }
  Finish(std::string(content));
}

//
// FileExternalAccountCredentials
//

absl::StatusOr<RefCountedPtr<FileExternalAccountCredentials>>
FileExternalAccountCredentials::Create(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  grpc_error_handle error;
  auto creds = MakeRefCounted<FileExternalAccountCredentials>(
      std::move(options), std::move(scopes), std::move(event_engine), &error);
  if (!error.ok()) return error;
  return creds;
}

FileExternalAccountCredentials::FileExternalAccountCredentials(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes),
                                 std::move(event_engine)) {
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

std::string FileExternalAccountCredentials::debug_string() {
  return absl::StrCat("FileExternalAccountCredentials{Audience:", audience(),
                      ")");
}

UniqueTypeName FileExternalAccountCredentials::Type() {
  static UniqueTypeName::Factory kFactory("FileExternalAccountCredentials");
  return kFactory.Create();
}

OrphanablePtr<ExternalAccountCredentials::FetchBody>
FileExternalAccountCredentials::RetrieveSubjectToken(
    Timestamp /*deadline*/,
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done) {
  return MakeOrphanable<FileFetchBody>(std::move(on_done), this);
}

absl::string_view FileExternalAccountCredentials::CredentialSourceType() {
  return "file";
}

}  // namespace grpc_core
