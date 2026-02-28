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

#include "src/core/xds/grpc/xds_common_types.h"

#include "src/core/util/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

//
// CommonTlsContext::CertificateProviderPluginInstance
//

std::string CommonTlsContext::CertificateProviderPluginInstance::ToString()
    const {
  std::vector<std::string> contents;
  if (!instance_name.empty()) {
    contents.push_back(absl::StrFormat("instance_name=%s", instance_name));
  }
  if (!certificate_name.empty()) {
    contents.push_back(
        absl::StrFormat("certificate_name=%s", certificate_name));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool CommonTlsContext::CertificateProviderPluginInstance::Empty() const {
  return instance_name.empty() && certificate_name.empty();
}

//
// CommonTlsContext::CertificateValidationContext
//

std::string CommonTlsContext::CertificateValidationContext::ToString() const {
  std::vector<std::string> contents;
  Match(
      ca_certs, [](const std::monostate&) {},
      [&](const CertificateProviderPluginInstance& cert_provider) {
        contents.push_back(
            absl::StrCat("ca_certs=cert_provider", cert_provider.ToString()));
      },
      [&](const SystemRootCerts&) {
        contents.push_back("ca_certs=system_root_certs{}");
      });
  if (!match_subject_alt_names.empty()) {
    std::vector<std::string> san_matchers;
    san_matchers.reserve(match_subject_alt_names.size());
    for (const auto& match : match_subject_alt_names) {
      san_matchers.push_back(match.ToString());
    }
    contents.push_back(absl::StrCat("match_subject_alt_names=[",
                                    absl::StrJoin(san_matchers, ", "), "]"));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool CommonTlsContext::CertificateValidationContext::Empty() const {
  return std::holds_alternative<std::monostate>(ca_certs) &&
         match_subject_alt_names.empty();
}

//
// CommonTlsContext
//

std::string CommonTlsContext::ToString() const {
  std::vector<std::string> contents;
  if (!tls_certificate_provider_instance.Empty()) {
    contents.push_back(
        absl::StrFormat("tls_certificate_provider_instance=%s",
                        tls_certificate_provider_instance.ToString()));
  }
  if (!certificate_validation_context.Empty()) {
    contents.push_back(
        absl::StrFormat("certificate_validation_context=%s",
                        certificate_validation_context.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool CommonTlsContext::Empty() const {
  return tls_certificate_provider_instance.Empty() &&
         certificate_validation_context.Empty();
}

//
// XdsGrpcService
//

std::string XdsGrpcService::ToString() const {
  std::vector<std::string> parts;
  if (server_target != nullptr) {
    parts.push_back(absl::StrCat("server_target=", server_target->Key()));
  }
  if (timeout != Duration::Zero()) {
    parts.push_back(absl::StrCat("timeout=", timeout.ToString()));
  }
  if (!initial_metadata.empty()) {
    std::vector<std::string> headers;
    for (const auto& [key, value] : initial_metadata) {
      headers.push_back(absl::StrCat(key, "=", value));
    }
    parts.push_back(
        absl::StrCat("initial_metadata=[", absl::StrJoin(headers, ", "), "]"));
  }
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

//
// HeaderMutationRules
//

bool HeaderMutationRules::IsMutationAllowed(
    const std::string& header_name) const {
  // If true, all header mutations are disallowed, regardless of any other
  // setting.
  if (disallow_all) {
    return false;
  }
  // If a header name matches this regex, then it will be disallowed
  if (disallow_expression != nullptr &&
      RE2::FullMatch(header_name, *disallow_expression)) {
    return false;
  }
  // If a header name matches this regex and does not match disallow_expression,
  // it will be allowed. If unset, then all headers not matching
  // disallow_expression are allowed
  if (allow_expression == nullptr ||
      RE2::FullMatch(header_name, *allow_expression)) {
    return true;
  }
  return false;
}

std::string HeaderMutationRules::ToString() const {
  std::vector<std::string> contents;
  if (disallow_all) {
    contents.push_back("disallow_all=true");
  }
  if (disallow_is_error) {
    contents.push_back("disallow_is_error=true");
  }
  if (allow_expression != nullptr) {
    contents.push_back(
        absl::StrCat("allow_expression=", allow_expression->pattern()));
  }
  if (disallow_expression != nullptr) {
    contents.push_back(
        absl::StrCat("disallow_expression=", disallow_expression->pattern()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

}  // namespace grpc_core
