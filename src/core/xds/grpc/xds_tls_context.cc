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

#include "src/core/xds/grpc/xds_tls_context.h"

#include <string>
#include <variant>

#include "src/core/util/match.h"
#include "src/core/util/string.h"

namespace grpc_core {

//
// CommonTlsContext::CertificateProviderPluginInstance
//

std::string CommonTlsContext::CertificateProviderPluginInstance::ToString()
    const {
  std::string result = "{";
  bool is_first = true;
  if (!instance_name.empty()) {
    StrAppend(result, "instance_name=");
    StrAppend(result, instance_name);
    is_first = false;
  }
  if (!certificate_name.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "certificate_name=");
    StrAppend(result, certificate_name);
  }
  StrAppend(result, "}");
  return result;
}

bool CommonTlsContext::CertificateProviderPluginInstance::Empty() const {
  return instance_name.empty() && certificate_name.empty();
}

//
// CommonTlsContext::CertificateValidationContext
//

std::string CommonTlsContext::CertificateValidationContext::ToString() const {
  std::string result = "{";
  bool is_first = true;
  Match(
      ca_certs, [](const std::monostate&) {},
      [&](const CertificateProviderPluginInstance& cert_provider) {
        StrAppend(result, "ca_certs=cert_provider");
        StrAppend(result, cert_provider.ToString());
        is_first = false;
      },
      [&](const SystemRootCerts&) {
        StrAppend(result, "ca_certs=system_root_certs{}");
        is_first = false;
      });
  if (!match_subject_alt_names.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "match_subject_alt_names=[");
    bool is_first_san = true;
    for (const auto& match : match_subject_alt_names) {
      if (!is_first_san) StrAppend(result, ", ");
      StrAppend(result, match.ToString());
      is_first_san = false;
    }
    StrAppend(result, "]");
  }
  StrAppend(result, "}");
  return result;
}

bool CommonTlsContext::CertificateValidationContext::Empty() const {
  return std::holds_alternative<std::monostate>(ca_certs) &&
         match_subject_alt_names.empty();
}

//
// CommonTlsContext
//

std::string CommonTlsContext::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (!tls_certificate_provider_instance.Empty()) {
    StrAppend(result, "tls_certificate_provider_instance=");
    StrAppend(result, tls_certificate_provider_instance.ToString());
    is_first = false;
  }
  if (!certificate_validation_context.Empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "certificate_validation_context=");
    StrAppend(result, certificate_validation_context.ToString());
  }
  StrAppend(result, "}");
  return result;
}

bool CommonTlsContext::Empty() const {
  return tls_certificate_provider_instance.Empty() &&
         certificate_validation_context.Empty();
}

}  // namespace grpc_core
