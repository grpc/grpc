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

#include <string>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/match.h"
#include "src/core/util/string.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

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

//
// HeaderMutationRules
//

bool HeaderMutationRules::IsMutationAllowed(
    const std::string& header_name) const {
  // Regardless of the mutation rules, we never allow certain headers.
  if (absl::StartsWith(header_name, ":") ||
      absl::StartsWith(header_name, "grpc-") || header_name == "host") {
    return false;
  }
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
  std::string result = "{";
  bool is_first = true;
  if (disallow_all) {
    StrAppend(result, "disallow_all=true");
    is_first = false;
  }
  if (disallow_is_error) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "disallow_is_error=true");
    is_first = false;
  }
  if (allow_expression != nullptr) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "allow_expression=");
    StrAppend(result, allow_expression->pattern());
    is_first = false;
  }
  if (disallow_expression != nullptr) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "disallow_expression=");
    StrAppend(result, disallow_expression->pattern());
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsHeaderValueOption
//

namespace {

void ApplyHeaderValueOptionMutation(const XdsHeaderValueOption& header,
                                    grpc_metadata_batch& md) {
  auto& [header_key, header_value] = header.header;
  std::string buffer;
  auto existing_value = md.GetStringValue(header_key, &buffer);
  switch (header.append_action) {
    case XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd: {
      if (!existing_value.has_value()) {
        md.Append(header_key, Slice::FromCopiedString(header_value),
                  [](absl::string_view, const Slice&) {});
      } else if (!header_value.empty()) {
        std::string concatenated_val =
            absl::StrCat(*existing_value, ",", header_value);
        md.Remove(absl::string_view(header_key));
        md.Append(header_key,
                  Slice::FromCopiedString(std::move(concatenated_val)),
                  [](absl::string_view, const Slice&) {});
      }
      break;
    }
    case XdsHeaderValueOption::AppendAction::kAddIfAbsent: {
      if (!existing_value.has_value()) {
        md.Append(header_key, Slice::FromCopiedString(header_value),
                  [](absl::string_view, const Slice&) {});
      }
      break;
    }
    case XdsHeaderValueOption::AppendAction::kOverwriteIfExists: {
      if (existing_value.has_value()) {
        md.Remove(absl::string_view(header_key));
        md.Append(header_key, Slice::FromCopiedString(header_value),
                  [](absl::string_view, const Slice&) {});
      }
      break;
    }
    case XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd: {
      md.Remove(absl::string_view(header_key));
      md.Append(header_key, Slice::FromCopiedString(header_value),
                [](absl::string_view, const Slice&) {});
      break;
    }
  }
}

}  // namespace

absl::Status ApplyXdsHeaderMutationsRemoval(absl::string_view remove_header,
                                            const HeaderMutationRules* rules,
                                            grpc_metadata_batch& md) {
  bool allowed = true;
  bool disallow_is_error = false;
  if (rules != nullptr) {
    allowed = rules->IsMutationAllowed(std::string(remove_header));
    disallow_is_error = rules->disallow_is_error;
  }
  if (allowed) {
    md.Remove(absl::string_view(remove_header));
  } else if (disallow_is_error) {
    return absl::InternalError(
        absl::StrCat("Forbidden header removal: ", remove_header));
  }
  return absl::OkStatus();
}

absl::Status ApplyXdsHeaderMutationsAddition(
    const XdsHeaderValueOption& set_header, const HeaderMutationRules* rules,
    grpc_metadata_batch& md) {
  bool allowed = true;
  bool disallow_is_error = false;
  if (rules != nullptr) {
    allowed = rules->IsMutationAllowed(std::string(set_header.header.first));
    disallow_is_error = rules->disallow_is_error;
  }
  if (allowed) {
    ApplyHeaderValueOptionMutation(set_header, md);
  } else if (disallow_is_error) {
    return absl::InternalError(
        absl::StrCat("Forbidden header mutation: ", set_header.header.first));
  }
  return absl::OkStatus();
}

}  // namespace grpc_core
