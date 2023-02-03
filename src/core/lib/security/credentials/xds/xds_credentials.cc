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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/xds/xds_credentials.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"

namespace grpc_core {

namespace {

bool XdsVerifySubjectAlternativeNames(
    const char* const* subject_alternative_names,
    size_t subject_alternative_names_size,
    const std::vector<StringMatcher>& matchers) {
  if (matchers.empty()) return true;
  for (size_t i = 0; i < subject_alternative_names_size; ++i) {
    for (const auto& matcher : matchers) {
      if (matcher.type() == StringMatcher::Type::kExact) {
        // For Exact match, use DNS rules for verifying SANs
        // TODO(zhenlian): Right now, the SSL layer does not save the type of
        // the SAN, so we are doing a DNS style verification for all SANs when
        // the type is EXACT. When we expose the SAN type, change this to only
        // do this verification when the SAN type is DNS and match type is
        // kExact. For all other cases, we should use matcher.Match().
        if (VerifySubjectAlternativeName(subject_alternative_names[i],
                                         matcher.string_matcher())) {
          return true;
        }
      } else {
        if (matcher.Match(subject_alternative_names[i])) {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace

//
// XdsCertificateVerifier
//

XdsCertificateVerifier::XdsCertificateVerifier(
    RefCountedPtr<XdsCertificateProvider> xds_certificate_provider,
    std::string cluster_name)
    : xds_certificate_provider_(std::move(xds_certificate_provider)),
      cluster_name_(std::move(cluster_name)) {}

bool XdsCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void(absl::Status)>, absl::Status* sync_status) {
  GPR_ASSERT(request != nullptr);
  if (!XdsVerifySubjectAlternativeNames(
          request->peer_info.san_names.uri_names,
          request->peer_info.san_names.uri_names_size,
          xds_certificate_provider_->GetSanMatchers(cluster_name_)) &&
      !XdsVerifySubjectAlternativeNames(
          request->peer_info.san_names.ip_names,
          request->peer_info.san_names.ip_names_size,
          xds_certificate_provider_->GetSanMatchers(cluster_name_)) &&
      !XdsVerifySubjectAlternativeNames(
          request->peer_info.san_names.dns_names,
          request->peer_info.san_names.dns_names_size,
          xds_certificate_provider_->GetSanMatchers(cluster_name_))) {
    *sync_status = absl::Status(
        absl::StatusCode::kUnauthenticated,
        "SANs from certificate did not match SANs from xDS control plane");
  }
  return true;  // synchronous check
}

void XdsCertificateVerifier::Cancel(
    grpc_tls_custom_verification_check_request*) {}

int XdsCertificateVerifier::CompareImpl(
    const grpc_tls_certificate_verifier* other) const {
  auto* o = static_cast<const XdsCertificateVerifier*>(other);
  int r = QsortCompare(xds_certificate_provider_, o->xds_certificate_provider_);
  if (r != 0) return r;
  return cluster_name_.compare(o->cluster_name_);
}

UniqueTypeName XdsCertificateVerifier::type() const {
  static UniqueTypeName::Factory kFactory("Xds");
  return kFactory.Create();
}

bool TestOnlyXdsVerifySubjectAlternativeNames(
    const char* const* subject_alternative_names,
    size_t subject_alternative_names_size,
    const std::vector<StringMatcher>& matchers) {
  return XdsVerifySubjectAlternativeNames(
      subject_alternative_names, subject_alternative_names_size, matchers);
}

//
// XdsCredentials
//

RefCountedPtr<grpc_channel_security_connector>
XdsCredentials::create_security_connector(
    RefCountedPtr<grpc_call_credentials> call_creds, const char* target_name,
    ChannelArgs* args) {
  // TODO(yashykt): This arg will no longer need to be added after b/173119596
  // is fixed.
  *args = args->SetIfUnset(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, target_name);
  RefCountedPtr<grpc_channel_security_connector> security_connector;
  auto xds_certificate_provider = args->GetObjectRef<XdsCertificateProvider>();
  if (xds_certificate_provider != nullptr) {
    std::string cluster_name(
        args->GetString(GRPC_ARG_XDS_CLUSTER_NAME).value());
    const bool watch_root =
        xds_certificate_provider->ProvidesRootCerts(cluster_name);
    const bool watch_identity =
        xds_certificate_provider->ProvidesIdentityCerts(cluster_name);
    if (watch_root || watch_identity) {
      auto tls_credentials_options =
          MakeRefCounted<grpc_tls_credentials_options>();
      tls_credentials_options->set_certificate_provider(
          xds_certificate_provider);
      if (watch_root) {
        tls_credentials_options->set_watch_root_cert(true);
        tls_credentials_options->set_root_cert_name(cluster_name);
      }
      if (watch_identity) {
        tls_credentials_options->set_watch_identity_pair(true);
        tls_credentials_options->set_identity_cert_name(cluster_name);
      }
      tls_credentials_options->set_verify_server_cert(true);
      tls_credentials_options->set_certificate_verifier(
          MakeRefCounted<XdsCertificateVerifier>(xds_certificate_provider,
                                                 std::move(cluster_name)));
      tls_credentials_options->set_check_call_host(false);
      auto tls_credentials =
          MakeRefCounted<TlsCredentials>(std::move(tls_credentials_options));
      return tls_credentials->create_security_connector(std::move(call_creds),
                                                        target_name, args);
    }
  }
  GPR_ASSERT(fallback_credentials_ != nullptr);
  return fallback_credentials_->create_security_connector(std::move(call_creds),
                                                          target_name, args);
}

UniqueTypeName XdsCredentials::Type() {
  static UniqueTypeName::Factory kFactory("Xds");
  return kFactory.Create();
}

//
// XdsServerCredentials
//

RefCountedPtr<grpc_server_security_connector>
XdsServerCredentials::create_security_connector(const ChannelArgs& args) {
  auto xds_certificate_provider = args.GetObjectRef<XdsCertificateProvider>();
  // Identity certs are a must for TLS.
  if (xds_certificate_provider != nullptr &&
      xds_certificate_provider->ProvidesIdentityCerts("")) {
    auto tls_credentials_options =
        MakeRefCounted<grpc_tls_credentials_options>();
    tls_credentials_options->set_watch_identity_pair(true);
    tls_credentials_options->set_certificate_provider(xds_certificate_provider);
    if (xds_certificate_provider->ProvidesRootCerts("")) {
      tls_credentials_options->set_watch_root_cert(true);
      if (xds_certificate_provider->GetRequireClientCertificate("")) {
        tls_credentials_options->set_cert_request_type(
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
      } else {
        tls_credentials_options->set_cert_request_type(
            GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
      }
    } else {
      // Do not request client certificate if there is no way to verify.
      tls_credentials_options->set_cert_request_type(
          GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
    }
    auto tls_credentials = MakeRefCounted<TlsServerCredentials>(
        std::move(tls_credentials_options));
    return tls_credentials->create_security_connector(args);
  }
  return fallback_credentials_->create_security_connector(args);
}

UniqueTypeName XdsServerCredentials::Type() {
  static UniqueTypeName::Factory kFactory("Xds");
  return kFactory.Create();
}

}  // namespace grpc_core

grpc_channel_credentials* grpc_xds_credentials_create(
    grpc_channel_credentials* fallback_credentials) {
  GPR_ASSERT(fallback_credentials != nullptr);
  return new grpc_core::XdsCredentials(fallback_credentials->Ref());
}

grpc_server_credentials* grpc_xds_server_credentials_create(
    grpc_server_credentials* fallback_credentials) {
  GPR_ASSERT(fallback_credentials != nullptr);
  return new grpc_core::XdsServerCredentials(fallback_credentials->Ref());
}
