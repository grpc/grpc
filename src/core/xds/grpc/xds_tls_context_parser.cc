//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/grpc/xds_tls_context_parser.h"

#include <utility>
#include <variant>

#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

namespace {

// CertificateProviderInstance is deprecated but we are still supporting it for
// backward compatibility reasons. Note that we still parse the data into the
// same CertificateProviderPluginInstance struct since the fields are the same.
// TODO(yashykt): Remove this once we stop supporting the old way of fetching
// certificate provider instances.
CommonTlsContext::CertificateProviderPluginInstance
CertificateProviderInstanceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance*
        certificate_provider_instance_proto,
    ValidationErrors* errors) {
  CommonTlsContext::CertificateProviderPluginInstance cert_provider;
  cert_provider.instance_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_instance_name(
          certificate_provider_instance_proto));
  const auto& bootstrap =
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap());
  if (bootstrap.certificate_providers().find(cert_provider.instance_name) ==
      bootstrap.certificate_providers().end()) {
    ValidationErrors::ScopedField field(errors, ".instance_name");
    errors->AddError(
        absl::StrCat("unrecognized certificate provider instance name: ",
                     cert_provider.instance_name));
  }
  cert_provider.certificate_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_certificate_name(
          certificate_provider_instance_proto));
  return cert_provider;
}

CommonTlsContext::CertificateProviderPluginInstance
CertificateProviderPluginInstanceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance*
        certificate_provider_plugin_instance_proto,
    ValidationErrors* errors) {
  CommonTlsContext::CertificateProviderPluginInstance cert_provider;
  cert_provider.instance_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_instance_name(
          certificate_provider_plugin_instance_proto));
  const auto& bootstrap =
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap());
  if (bootstrap.certificate_providers().find(cert_provider.instance_name) ==
      bootstrap.certificate_providers().end()) {
    ValidationErrors::ScopedField field(errors, ".instance_name");
    errors->AddError(
        absl::StrCat("unrecognized certificate provider instance name: ",
                     cert_provider.instance_name));
  }
  cert_provider.certificate_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_certificate_name(
          certificate_provider_plugin_instance_proto));
  return cert_provider;
}

CommonTlsContext::CertificateValidationContext
CertificateValidationContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext*
        certificate_validation_context_proto,
    ValidationErrors* errors) {
  CommonTlsContext::CertificateValidationContext certificate_validation_context;
  size_t len = 0;
  auto* subject_alt_names_matchers =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_match_subject_alt_names(
          certificate_validation_context_proto, &len);
  for (size_t i = 0; i < len; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".match_subject_alt_names[", i, "]"));
    auto string_matcher =
        StringMatcherParse(context, subject_alt_names_matchers[i], errors);
    certificate_validation_context.match_subject_alt_names.push_back(
        std::move(string_matcher));
  }
  auto* ca_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_ca_certificate_provider_instance(
          certificate_validation_context_proto);
  if (ca_certificate_provider_instance != nullptr) {
    ValidationErrors::ScopedField field(errors,
                                        ".ca_certificate_provider_instance");
    certificate_validation_context.ca_certs =
        CertificateProviderPluginInstanceParse(
            context, ca_certificate_provider_instance, errors);
  } else {
    auto* system_root_certs =
        envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_system_root_certs(
            certificate_validation_context_proto);
    if (system_root_certs != nullptr) {
      certificate_validation_context.ca_certs =
          CommonTlsContext::CertificateValidationContext::SystemRootCerts();
    }
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_verify_certificate_spki(
          certificate_validation_context_proto, nullptr) != nullptr) {
    ValidationErrors::ScopedField field(errors, ".verify_certificate_spki");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_verify_certificate_hash(
          certificate_validation_context_proto, nullptr) != nullptr) {
    ValidationErrors::ScopedField field(errors, ".verify_certificate_hash");
    errors->AddError("feature unsupported");
  }
  if (ParseBoolValue(
          envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_require_signed_certificate_timestamp(
              certificate_validation_context_proto))) {
    ValidationErrors::ScopedField field(
        errors, ".require_signed_certificate_timestamp");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_has_crl(
          certificate_validation_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".crl");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_has_custom_validator_config(
          certificate_validation_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".custom_validator_config");
    errors->AddError("feature unsupported");
  }
  return certificate_validation_context;
}

}  // namespace

CommonTlsContext CommonTlsContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    ValidationErrors* errors) {
  CommonTlsContext common_tls_context;
  // The validation context is derived from the oneof in
  // 'validation_context_type'. 'validation_context_sds_secret_config' is not
  // supported.
  auto* combined_validation_context =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_combined_validation_context(
          common_tls_context_proto);
  if (combined_validation_context != nullptr) {
    ValidationErrors::ScopedField field(errors, ".combined_validation_context");
    auto* default_validation_context =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_default_validation_context(
            combined_validation_context);
    if (default_validation_context != nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".default_validation_context");
      common_tls_context.certificate_validation_context =
          CertificateValidationContextParse(context, default_validation_context,
                                            errors);
    }
    // If after parsing default_validation_context,
    // common_tls_context->certificate_validation_context.ca_certs does not
    // contain a cert provider, fall back onto
    // 'validation_context_certificate_provider_instance' inside
    // 'combined_validation_context'. Note that this way of fetching root
    // certificates is deprecated and will be removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    if (!std::holds_alternative<
            CommonTlsContext::CertificateProviderPluginInstance>(
            common_tls_context.certificate_validation_context.ca_certs)) {
      const auto* validation_context_certificate_provider_instance =
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_validation_context_certificate_provider_instance(
              combined_validation_context);
      if (validation_context_certificate_provider_instance != nullptr) {
        ValidationErrors::ScopedField field(
            errors, ".validation_context_certificate_provider_instance");
        common_tls_context.certificate_validation_context.ca_certs =
            CertificateProviderInstanceParse(
                context, validation_context_certificate_provider_instance,
                errors);
      }
    }
  } else if (
      auto* validation_context =
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_validation_context(
              common_tls_context_proto);
      validation_context != nullptr) {
    ValidationErrors::ScopedField field(errors, ".validation_context");
    common_tls_context.certificate_validation_context =
        CertificateValidationContextParse(context, validation_context, errors);
  } else if (
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_validation_context_sds_secret_config(
          common_tls_context_proto)) {
    ValidationErrors::ScopedField field(
        errors, ".validation_context_sds_secret_config");
    errors->AddError("feature unsupported");
  }
  auto* tls_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_provider_instance(
          common_tls_context_proto);
  if (tls_certificate_provider_instance != nullptr) {
    ValidationErrors::ScopedField field(errors,
                                        ".tls_certificate_provider_instance");
    common_tls_context.tls_certificate_provider_instance =
        CertificateProviderPluginInstanceParse(
            context, tls_certificate_provider_instance, errors);
  } else {
    // Fall back onto 'tls_certificate_certificate_provider_instance'. Note that
    // this way of fetching identity certificates is deprecated and will be
    // removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    auto* tls_certificate_certificate_provider_instance =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_certificate_provider_instance(
            common_tls_context_proto);
    if (tls_certificate_certificate_provider_instance != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".tls_certificate_certificate_provider_instance");
      common_tls_context.tls_certificate_provider_instance =
          CertificateProviderInstanceParse(
              context, tls_certificate_certificate_provider_instance, errors);
    } else {
      size_t size;
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificates(
          common_tls_context_proto, &size);
      if (size != 0) {
        ValidationErrors::ScopedField field(errors, ".tls_certificates");
        errors->AddError("feature unsupported");
      }
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_sds_secret_configs(
          common_tls_context_proto, &size);
      if (size != 0) {
        ValidationErrors::ScopedField field(
            errors, ".tls_certificate_sds_secret_configs");
        errors->AddError("feature unsupported");
      }
    }
  }
  if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_params(
          common_tls_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".tls_params");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_custom_handshaker(
          common_tls_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".custom_handshaker");
    errors->AddError("feature unsupported");
  }
  return common_tls_context;
}

}  // namespace grpc_core
