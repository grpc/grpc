//
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
//

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {

extern const char kCredentialsTypeXds[];

class XdsCredentials final : public grpc_channel_credentials {
 public:
  explicit XdsCredentials(
      RefCountedPtr<grpc_channel_credentials> fallback_credentials)
      : grpc_channel_credentials(kCredentialsTypeXds),
        fallback_credentials_(std::move(fallback_credentials)) {}

  RefCountedPtr<grpc_channel_security_connector> create_security_connector(
      RefCountedPtr<grpc_call_credentials> call_creds, const char* target_name,
      const grpc_channel_args* args, grpc_channel_args** new_args) override;

 private:
  RefCountedPtr<grpc_channel_credentials> fallback_credentials_;
};

class XdsServerCredentials final : public grpc_server_credentials {
 public:
  explicit XdsServerCredentials(
      RefCountedPtr<grpc_server_credentials> fallback_credentials)
      : grpc_server_credentials(kCredentialsTypeXds),
        fallback_credentials_(std::move(fallback_credentials)) {}

  RefCountedPtr<grpc_server_security_connector> create_security_connector(
      const grpc_channel_args* /* args */) override;

 private:
  RefCountedPtr<grpc_server_credentials> fallback_credentials_;
};

bool TestOnlyXdsVerifySubjectAlternativeNames(
    const char* const* subject_alternative_names,
    size_t subject_alternative_names_size,
    const std::vector<StringMatcher>& matchers);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CREDENTIALS_H */
