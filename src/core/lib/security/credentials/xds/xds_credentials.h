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

#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"

namespace grpc_core {

class XdsCertificateVerifier : public grpc_tls_certificate_verifier {
 public:
  XdsCertificateVerifier(
      RefCountedPtr<XdsCertificateProvider> xds_certificate_provider,
      std::string cluster_name);

  bool Verify(grpc_tls_custom_verification_check_request* request,
              std::function<void(absl::Status)>,
              absl::Status* sync_status) override;
  void Cancel(grpc_tls_custom_verification_check_request*) override;

  const char* type() const override;

 private:
  int CompareImpl(const grpc_tls_certificate_verifier* other) const override;

  RefCountedPtr<XdsCertificateProvider> xds_certificate_provider_;
  std::string cluster_name_;
};

class XdsCredentials final : public grpc_channel_credentials {
 public:
  explicit XdsCredentials(
      RefCountedPtr<grpc_channel_credentials> fallback_credentials)
      : fallback_credentials_(std::move(fallback_credentials)) {}

  RefCountedPtr<grpc_channel_security_connector> create_security_connector(
      RefCountedPtr<grpc_call_credentials> call_creds, const char* target_name,
      const grpc_channel_args* args, grpc_channel_args** new_args) override;

  static const char* Type();

  const char* type() const override { return Type(); }

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override {
    auto* o = static_cast<const XdsCredentials*>(other);
    return fallback_credentials_->cmp(o->fallback_credentials_.get());
  }

  RefCountedPtr<grpc_channel_credentials> fallback_credentials_;
};

class XdsServerCredentials final : public grpc_server_credentials {
 public:
  explicit XdsServerCredentials(
      RefCountedPtr<grpc_server_credentials> fallback_credentials)
      : fallback_credentials_(std::move(fallback_credentials)) {}

  RefCountedPtr<grpc_server_security_connector> create_security_connector(
      const grpc_channel_args* /* args */) override;

  static const char* Type();

  const char* type() const override { return Type(); }

 private:
  RefCountedPtr<grpc_server_credentials> fallback_credentials_;
};

bool TestOnlyXdsVerifySubjectAlternativeNames(
    const char* const* subject_alternative_names,
    size_t subject_alternative_names_size,
    const std::vector<StringMatcher>& matchers);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_XDS_XDS_CREDENTIALS_H */
