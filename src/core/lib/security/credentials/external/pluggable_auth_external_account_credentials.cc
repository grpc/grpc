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

#include <cxxabi.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <thread>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/subprocess.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_util.h"
#include "src/core/lib/security/credentials/external/external_account_credentials.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"

extern char** environ;

#define DEFAULT_EXECUTABLE_TIMEOUT_MS 30000  // 30 seconds
#define MIN_EXECUTABLE_TIMEOUT_MS 5000       // 5 seconds
#define MAX_EXECUTABLE_TIMEOUT_MS 120000     // 120 seconds
#define SAML_SUBJECT_TOKEN_TYPE "urn:ietf:params:oauth:token-type:saml2"
#define GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES \
  "GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES"
#define GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES_ACCEPTED_VALUE "1"

bool isExpired(int64_t expiration_time) {
  return gpr_time_cmp(
             gpr_time_from_seconds(expiration_time, GPR_CLOCK_REALTIME),
             gpr_now(GPR_CLOCK_REALTIME)) <= 0;
}

bool run_executable(gpr_subprocess** gpr_subprocess, int argc, char** argv,
                    int envc, char** envp, std::string* output,
                    std::string* stderr_data, std::string* error) {
  *gpr_subprocess =
      gpr_subprocess_create_with_envp(argc, const_cast<const char**>(argv),
                                      envc, const_cast<const char**>(envp));
  std::string input;
  return gpr_subprocess_communicate(*gpr_subprocess, input, output, stderr_data,
                                    error);
}

namespace grpc_core {
struct SliceWrapper {
  ~SliceWrapper() { CSliceUnref(slice); }
  grpc_slice slice = grpc_empty_slice();
};

RefCountedPtr<PluggableAuthExternalAccountCredentials>
PluggableAuthExternalAccountCredentials::Create(Options options,
                                                std::vector<std::string> scopes,
                                                grpc_error_handle* error) {
  auto creds = MakeRefCounted<PluggableAuthExternalAccountCredentials>(
      std::move(options), std::move(scopes), error);
  if (error->ok()) return creds;
  return nullptr;
}

PluggableAuthExternalAccountCredentials::
    PluggableAuthExternalAccountCredentials(Options options,
                                            std::vector<std::string> scopes,
                                            grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes)) {
  std::vector<grpc_error_handle> error_list;
  const Json::Object* executable_json;
  if (!ParseJsonObjectField(options.credential_source.object(), "executable",
                            &executable_json, &error_list)) {
    *error = GRPC_ERROR_CREATE(
        "Invalid credential source for executable-sourced credentials: "
        "`executable` field must be present and must be an object.");
    return;
  }
  if (!ParseJsonObjectField(*executable_json, "command", &command_, &error_list,
                            true)) {
    *error = GRPC_ERROR_CREATE(
        "Invalid credential source for executable-sourced credentials: "
        "`command` field must be provided.");
    return;
  }
  executable_timeout_ms_ = DEFAULT_EXECUTABLE_TIMEOUT_MS;
  ParseJsonObjectField(*executable_json, "timeout_millis",
                       &executable_timeout_ms_, &error_list, false);
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE(
        "Invalid credential source for executable-sourced credentials: "
        "timeout_millis field must be a number.");
    return;
  }
  if (executable_timeout_ms_ > MAX_EXECUTABLE_TIMEOUT_MS ||
      executable_timeout_ms_ < MIN_EXECUTABLE_TIMEOUT_MS) {
    *error = GRPC_ERROR_CREATE(absl::StrFormat(
        "Invalid credential source for executable-sourced credentials: "
        "timeout_millis should be between %d and %d milliseconds.",
        MIN_EXECUTABLE_TIMEOUT_MS, MAX_EXECUTABLE_TIMEOUT_MS));
    return;
  }
  ParseJsonObjectField(*executable_json, "output_file", &output_file_path_,
                       &error_list, false);
}

PluggableAuthExternalAccountCredentials::ExecutableResponse*
PluggableAuthExternalAccountCredentials::ParseExecutableResponse(
    std::string executable_output_string, grpc_error_handle* error) {
  std::vector<grpc_error_handle> error_list;
  auto executable_output = JsonParse(executable_output_string);
  ExecutableResponse* executable_response =
      static_cast<ExecutableResponse*>(gpr_malloc(sizeof(ExecutableResponse)));
  if (!executable_output.ok()) {
    *error = GRPC_ERROR_CREATE(
        absl::StrFormat("The response from the executable contains an invalid "
                        "or malformed response: %s.",
                        executable_output_string));
    return nullptr;
  }
  auto executable_output_object = executable_output->object();
  if (!ParseJsonObjectField(executable_output_object, "version",
                            &executable_response->version, &error_list, true)) {
    *error = GRPC_ERROR_CREATE(
        "The executable response must contain the `version` field and should "
        "be a number.");
    return nullptr;
  }
  if (!ParseJsonObjectField(executable_output_object, "success",
                            &executable_response->success, &error_list, true)) {
    *error = GRPC_ERROR_CREATE(
        "The executable response must contain the `success` field and should "
        "be a boolean.");
    return nullptr;
  }
  if (executable_response->success) {
    if (!ParseJsonObjectField(executable_output_object, "token_type",
                              &executable_response->token_type, &error_list,
                              true)) {
      *error = GRPC_ERROR_CREATE(
          "The executable response must contain the `token_type` field.");
      return nullptr;
    }
    executable_response->expiration_time =
        gpr_time_to_millis(gpr_inf_future(GPR_CLOCK_REALTIME));
    if (!output_file_path_.empty() &&
        executable_output_object.find("expiration_time") ==
            executable_output_object.end()) {
      *error = GRPC_ERROR_CREATE(
          "The executable response must contain the `expiration_time` field "
          "for successful responses when an output_file has been specified in "
          "the configuration.");
      return nullptr;
    }
    ParseJsonObjectField(executable_output_object, "expiration_time",
                         &executable_response->expiration_time, &error_list,
                         false);
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE(
          "The executable response contains an invalid value for "
          "`expiration_time`.");
      return nullptr;
    }
    auto executable_output_it = executable_output_object.find("id_token");
    if (strcmp(executable_response->token_type, SAML_SUBJECT_TOKEN_TYPE) == 0) {
      executable_output_it = executable_output_object.find("saml_response");
    }
    if (executable_output_it == executable_output_object.end()) {
      *error = GRPC_ERROR_CREATE(
          "The executable response must contain a valid token.");
      return nullptr;
    }
    executable_response->subject_token =
        gpr_strdup(executable_output_it->second.string().c_str());
  } else {
    if (!ParseJsonObjectField(executable_output_object, "code",
                              &executable_response->error_code, &error_list,
                              true)) {
      *error = GRPC_ERROR_CREATE(
          "The executable response must contain the "
          "`code` field when unsuccessful.");
      return nullptr;
    }
    if (!ParseJsonObjectField(executable_output_object, "message",
                              &executable_response->error_message, &error_list,
                              true)) {
      *error = GRPC_ERROR_CREATE(
          "The executable response must contain the "
          "`message` field when unsuccessful.");
      return nullptr;
    }
  }
  return executable_response;
}

bool validate_allow_executables_env() {
  char* allow_exec_env_value =
      getenv(GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES);
  if (allow_exec_env_value == nullptr) return false;
  return strcmp(getenv(GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES),
                GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES_ACCEPTED_VALUE) == 0;
}

// Users can specify an output file path in the Pluggable Auth ADC
// configuration. This is the file's absolute path. Their executable will
// handle writing the 3P credentials to this file. If specified, we will first
// check if we have valid unexpired credentials stored in this location to
// avoid running the executable until they are expired.
bool PluggableAuthExternalAccountCredentials::
    RetrieveSubjectTokenFromCachedOutputFile() {
  grpc_error_handle error;
  if (!output_file_path_.empty()) {
    SliceWrapper content_slice;
    error = grpc_load_file(output_file_path_.c_str(), 0, &content_slice.slice);
    if (error.ok()) {
      std::string output_file_content =
          std::string(StringViewFromSlice(content_slice.slice));
      // If the output_file is not blank, try to get an ExecutableResponse
      // from the output file.
      if (!output_file_content.empty()) {
        executable_response_ =
            ParseExecutableResponse(std::string(output_file_content), &error);
        if (executable_response_ == nullptr) {
          FinishRetrieveSubjectToken("", error);
          return true;
        }
        // If the cached output file has an executable response
        // that was successful and un-expired, return the subject token.
        if (executable_response_->success &&
            !isExpired(executable_response_->expiration_time)) {
          FinishRetrieveSubjectToken(executable_response_->subject_token,
                                     absl::OkStatus());
          return true;
        }
      }
    }
  }
  return false;
}

void PluggableAuthExternalAccountCredentials::RetrieveSubjectToken(
    HTTPRequestContext* /*ctx*/, const Options& options,
    std::function<void(std::string, grpc_error_handle)> cb) {
  cb_ = cb;
  grpc_error_handle error;
  if (!validate_allow_executables_env()) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(
                "Pluggable Auth executables need to be explicitly allowed to "
                "run by setting the GOOGLE_EXTERNAL_ACCOUNT_ALLOW_EXECUTABLES "
                "environment variable to 1."));
    return;
  }
  if (RetrieveSubjectTokenFromCachedOutputFile()) return;
  // If the cached output_file does not contain a valid response, call the
  // executable.
  std::vector<std::string> envp_vector = {
      absl::StrFormat("GOOGLE_EXTERNAL_ACCOUNT_AUDIENCE=%s", options.audience),
      absl::StrFormat("GOOGLE_EXTERNAL_ACCOUNT_TOKEN_TYPE=%s",
                      options.subject_token_type),
      absl::StrFormat("GOOGLE_EXTERNAL_ACCOUNT_INTERACTIVE=%d", 0),
      absl::StrFormat(
          "GOOGLE_EXTERNAL_ACCOUNT_IMPERSONATED_EMAIL=%s",
          GetImpersonatedEmail(options.service_account_impersonation_url))};
  if (!output_file_path_.empty()) {
    envp_vector.push_back(absl::StrFormat(
        "GOOGLE_EXTERNAL_ACCOUNT_OUTPUT_FILE=%s", output_file_path_));
  }
  int environ_count = 0, envc = 0, argc = 0;
  while (environ[environ_count] != nullptr) environ_count += 1;
  char** envp = static_cast<char**>(
      gpr_malloc(sizeof(char*) * (environ_count + envp_vector.size())));
  for (; envc < environ_count; envc++) {
    envp[envc] = gpr_strdup(environ[envc]);
  }
  for (int j = 0; j < envp_vector.size(); envc++, j++) {
    envp[envc] = gpr_strdup(envp_vector[j].c_str());
  }
  std::vector<std::string> arg_vector = absl::StrSplit(command_, ' ');
  argc = arg_vector.size();
  char** argv = static_cast<char**>(malloc(sizeof(char*) * argc));
  for (int i = 0; i < argc; i++) {
    argv[i] = gpr_strdup(arg_vector[i].c_str());
  }
  std::packaged_task<bool(gpr_subprocess**, int, char**, int, char**,
                          std::string*, std::string*, std::string*)>
      run_executable_task(run_executable);
  std::future<bool> executable_output_future = run_executable_task.get_future();
  std::string output_string, stderr_data, error_string;
  std::thread thr(std::move(run_executable_task), &gpr_subprocess_, argc, argv,
                  envc, envp, &output_string, &stderr_data, &error_string);
  if (executable_output_future.wait_for(std::chrono::seconds(
          executable_timeout_ms_ / 1000)) != std::future_status::timeout) {
    thr.join();
    if (!executable_output_future.get()) {
      // An error must have occured in the executable.
      FinishRetrieveSubjectToken(
          "", GRPC_ERROR_CREATE(absl::StrFormat(
                  "Executable failed with error: %s.", error_string)));
      return;
    }
    // If the output file is specified, then check if the executable has stored
    // the response in the output file path.
    if (!output_file_path_.empty()) {
      SliceWrapper content_slice;
      error =
          grpc_load_file(output_file_path_.c_str(), 0, &content_slice.slice);
      if (error.ok()) {
        absl::string_view output_file_content =
            StringViewFromSlice(content_slice.slice);
        executable_response_ =
            ParseExecutableResponse(std::string(output_file_content), &error);
      }
    }
    // Get the result of the executable from stdout and create an
    // ExecutableResponse object from it.
    if (executable_response_ == nullptr && !output_string.empty()) {
      executable_response_ = ParseExecutableResponse(output_string, &error);
    }
    if (executable_response_ == nullptr && !stderr_data.empty()) {
      // Get the result of the executable from stderr and create an
      // ExecutableResponse object from it.
      executable_response_ = ParseExecutableResponse(stderr_data, &error);
    }
    if (executable_response_ == nullptr) {
      FinishRetrieveSubjectToken("", error);
      return;
    }
    if (!executable_response_->success) {
      FinishRetrieveSubjectToken(
          "", GRPC_ERROR_CREATE(
                  absl::StrFormat("Executable failed with error code: %s "
                                  "and error message: %s.",
                                  executable_response_->error_code,
                                  executable_response_->error_message)));
      return;
    }
    if (isExpired(executable_response_->expiration_time)) {
      FinishRetrieveSubjectToken(
          "", GRPC_ERROR_CREATE("Executable response is expired."));
      return;
    }
    // Subject token is valid and can be returned.
    FinishRetrieveSubjectToken(executable_response_->subject_token,
                               absl::OkStatus());
    return;
  } else {
    // Process has not terminated within the specified timeout.
    gpr_subprocess_destroy(gpr_subprocess_);
    FinishRetrieveSubjectToken("", GRPC_ERROR_CREATE(absl::StrFormat(
                                       "The executable failed to finish within "
                                       "the timeout of %d milliseconds.",
                                       executable_timeout_ms_)));
    return;
  }
}

void PluggableAuthExternalAccountCredentials::FinishRetrieveSubjectToken(
    std::string token, grpc_error_handle error) {
  auto cb = cb_;
  cb_ = nullptr;
  if (error.ok()) {
    cb(token, absl::OkStatus());
  } else {
    cb("", error);
  }
}

}  // namespace grpc_core
