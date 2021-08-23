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

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
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

class ServerAuthCheck {
 public:
  ServerAuthCheck(
      RefCountedPtr<XdsCertificateProvider> xds_certificate_provider,
      std::string cluster_name)
      : xds_certificate_provider_(std::move(xds_certificate_provider)),
        cluster_name_(std::move(cluster_name)) {}

  static int Schedule(void* config_user_data,
                      grpc_tls_server_authorization_check_arg* arg) {
    return static_cast<ServerAuthCheck*>(config_user_data)->ScheduleImpl(arg);
  }

  static void Destroy(void* config_user_data) {
    delete static_cast<ServerAuthCheck*>(config_user_data);
  }

 private:
  int ScheduleImpl(grpc_tls_server_authorization_check_arg* arg) {
    if (XdsVerifySubjectAlternativeNames(
            arg->subject_alternative_names, arg->subject_alternative_names_size,
            xds_certificate_provider_->GetSanMatchers(cluster_name_))) {
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

  RefCountedPtr<XdsCertificateProvider> xds_certificate_provider_;
  std::string cluster_name_;
};

}  // namespace

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
    const grpc_channel_args* args, grpc_channel_args** new_args) {
  struct ChannelArgsDeleter {
    const grpc_channel_args* args;
    bool owned;
    ~ChannelArgsDeleter() {
      if (owned) grpc_channel_args_destroy(args);
    }
  };
  ChannelArgsDeleter temp_args{args, false};
  // TODO(yashykt): This arg will no longer need to be added after b/173119596
  // is fixed.
  grpc_arg override_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      const_cast<char*>(target_name));
  const char* override_arg_name = GRPC_SSL_TARGET_NAME_OVERRIDE_ARG;
  if (grpc_channel_args_find(args, override_arg_name) == nullptr) {
    temp_args.args = grpc_channel_args_copy_and_add_and_remove(
        args, &override_arg_name, 1, &override_arg, 1);
    temp_args.owned = true;
  }
  RefCountedPtr<grpc_channel_security_connector> security_connector;
  auto xds_certificate_provider =
      XdsCertificateProvider::GetFromChannelArgs(args);
  if (xds_certificate_provider != nullptr) {
    std::string cluster_name =
        grpc_channel_args_find_string(args, GRPC_ARG_XDS_CLUSTER_NAME);
    GPR_ASSERT(cluster_name.data() != nullptr);
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
      tls_credentials_options->set_server_verification_option(
          GRPC_TLS_SKIP_HOSTNAME_VERIFICATION);
      auto* server_auth_check = new ServerAuthCheck(xds_certificate_provider,
                                                    std::move(cluster_name));
      tls_credentials_options->set_server_authorization_check_config(
          MakeRefCounted<grpc_tls_server_authorization_check_config>(
              server_auth_check, ServerAuthCheck::Schedule, nullptr,
              ServerAuthCheck::Destroy));
      // TODO(yashkt): Creating a new TlsCreds object each time we create a
      // security connector means that the security connector's cmp() method
      // returns unequal for each instance, which means that every time an LB
      // policy updates, all the subchannels will be recreated.  This is
      // going to lead to a lot of connection churn.  Instead, we should
      // either (a) change the TLS security connector's cmp() method to be
      // smarter somehow, so that it compares unequal only when the
      // tls_credentials_options have changed, or (b) cache the TlsCreds
      // objects in the XdsCredentials object so that we can reuse the
      // same one when creating new security connectors, swapping out the
      // TlsCreds object only when the tls_credentials_options change.
      // Option (a) would probably be better, although it may require some
      // structural changes to the security connector API.
      auto tls_credentials =
          MakeRefCounted<TlsCredentials>(std::move(tls_credentials_options));
      return tls_credentials->create_security_connector(
          std::move(call_creds), target_name, temp_args.args, new_args);
    }
  }
  GPR_ASSERT(fallback_credentials_ != nullptr);
  return fallback_credentials_->create_security_connector(
      std::move(call_creds), target_name, temp_args.args, new_args);
}

//
// XdsServerCredentials
//

RefCountedPtr<grpc_server_security_connector>
XdsServerCredentials::create_security_connector(const grpc_channel_args* args) {
  auto xds_certificate_provider =
      XdsCertificateProvider::GetFromChannelArgs(args);
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
