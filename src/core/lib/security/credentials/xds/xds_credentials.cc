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

#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

const char kCredentialsTypeXds[] = "Xds";

namespace {

bool XdsVerifySubjectAlternativeNames(
    const char* const* subject_alternative_names,
    size_t subject_alternative_names_size,
    const std::vector<XdsApi::StringMatcher>& matchers) {
  if (matchers.empty()) return true;
  for (size_t i = 0; i < subject_alternative_names_size; ++i) {
    for (const auto& matcher : matchers) {
      if (matcher.type() == XdsApi::StringMatcher::StringMatcherType::EXACT) {
        // For EXACT match, use DNS rules for verifying SANs
        // TODO(zhenlian): Right now, the SSL layer does not save the type of
        // the SAN, so we are doing a DNS style verification for all SANs when
        // the type is EXACT. When we expose the SAN type, change this to only
        // do this verification when the SAN type is DNS and match type is
        // EXACT. For all other cases, we should use matcher.Match().
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

int ServerAuthCheckSchedule(void* config_user_data,
                            grpc_tls_server_authorization_check_arg* arg) {
  XdsCertificateProvider* xds_certificate_provider =
      static_cast<XdsCertificateProvider*>(config_user_data);
  if (XdsVerifySubjectAlternativeNames(
          arg->subject_alternative_names, arg->subject_alternative_names_size,
          xds_certificate_provider->subject_alternative_name_matchers())) {
    arg->success = 1;
    arg->status = GRPC_STATUS_OK;
  } else {
    arg->success = 0;
    arg->status = GRPC_STATUS_UNAUTHENTICATED;
    if (arg->error_details) {
      arg->error_details->set_error_details(
          "SANs from certificate did not match SANs from xDS control plane");
    }
  }

  return 0; /* synchronous check */
}

void ServerAuthCheckDestroy(void* config_user_data) {
  XdsCertificateProvider* xds_certificate_provider =
      static_cast<XdsCertificateProvider*>(config_user_data);
  xds_certificate_provider->Unref();
}

}  // namespace

bool TestOnlyXdsVerifySubjectAlternativeNames(
    const char* const* subject_alternative_names,
    size_t subject_alternative_names_size,
    const std::vector<XdsApi::StringMatcher>& matchers) {
  return XdsVerifySubjectAlternativeNames(
      subject_alternative_names, subject_alternative_names_size, matchers);
}

//
// XdsCredentials
//

RefCountedPtr<grpc_channel_security_connector>
XdsCredentials::create_security_connector(
    RefCountedPtr<grpc_call_credentials> call_creds, const char* target_name,
    const grpc_channel_args* args, grpc_channel_args** new_args) {
  auto xds_certificate_provider =
      XdsCertificateProvider::GetFromChannelArgs(args);
  // TODO(yashykt): This arg will no longer need to be added after b/173119596
  // is fixed.
  grpc_arg override_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      const_cast<char*>(target_name));
  const char* override_arg_name = GRPC_SSL_TARGET_NAME_OVERRIDE_ARG;
  const grpc_channel_args* temp_args = args;
  if (grpc_channel_args_find(args, override_arg_name) == nullptr) {
    temp_args = grpc_channel_args_copy_and_add_and_remove(
        args, &override_arg_name, 1, &override_arg, 1);
  }
  RefCountedPtr<grpc_channel_security_connector> security_connector;
  if (xds_certificate_provider != nullptr) {
    auto tls_credentials_options =
        MakeRefCounted<grpc_tls_credentials_options>();
    tls_credentials_options->set_certificate_provider(xds_certificate_provider);
    if (xds_certificate_provider->ProvidesRootCerts()) {
      tls_credentials_options->set_watch_root_cert(true);
    }
    if (xds_certificate_provider->ProvidesIdentityCerts()) {
      tls_credentials_options->set_watch_identity_pair(true);
    }
    tls_credentials_options->set_server_verification_option(
        GRPC_TLS_SKIP_HOSTNAME_VERIFICATION);
    tls_credentials_options->set_server_authorization_check_config(
        MakeRefCounted<grpc_tls_server_authorization_check_config>(
            xds_certificate_provider->Ref().release(), ServerAuthCheckSchedule,
            nullptr, ServerAuthCheckDestroy));
    auto tls_credentials =
        MakeRefCounted<TlsCredentials>(std::move(tls_credentials_options));
    security_connector = tls_credentials->create_security_connector(
        std::move(call_creds), target_name, temp_args, new_args);
  } else {
    GPR_ASSERT(fallback_credentials_ != nullptr);
    security_connector = fallback_credentials_->create_security_connector(
        std::move(call_creds), target_name, temp_args, new_args);
  }
  if (temp_args != args) {
    grpc_channel_args_destroy(temp_args);
  }
  return security_connector;
}

//
// XdsServerCredentials
//

RefCountedPtr<grpc_server_security_connector>
XdsServerCredentials::create_security_connector() {
  // TODO(yashkt): Fill this
  return fallback_credentials_->create_security_connector();
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
