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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_URL_EXTERNAL_ACCOUNT_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_URL_EXTERNAL_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/external/external_account_credentials.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

class UrlExternalAccountCredentials final : public ExternalAccountCredentials {
 public:
  static RefCountedPtr<UrlExternalAccountCredentials> Create(
      Options options, std::vector<std::string> scopes,
      grpc_error_handle* error);

  UrlExternalAccountCredentials(Options options,
                                std::vector<std::string> scopes,
                                grpc_error_handle* error);

 private:
  void RetrieveSubjectToken(
      HTTPRequestContext* ctx, const Options& options,
      std::function<void(std::string, grpc_error_handle)> cb) override;

  static void OnRetrieveSubjectToken(void* arg, grpc_error_handle error);
  void OnRetrieveSubjectTokenInternal(grpc_error_handle error);

  void FinishRetrieveSubjectToken(std::string subject_token,
                                  grpc_error_handle error);

  // Fields of credential source
  URI url_;
  std::string url_full_path_;
  std::map<std::string, std::string> headers_;
  std::string format_type_;
  std::string format_subject_token_field_name_;

  OrphanablePtr<HttpRequest> http_request_;
  HTTPRequestContext* ctx_ = nullptr;
  std::function<void(std::string, grpc_error_handle)> cb_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_URL_EXTERNAL_ACCOUNT_CREDENTIALS_H
