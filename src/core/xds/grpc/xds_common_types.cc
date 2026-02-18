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

#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_writer.h"
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
// SafeRegexMatch
//

const JsonLoaderInterface* SafeRegexMatch::JsonLoader(const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<SafeRegexMatch>()
                                  .Field("regex", &SafeRegexMatch::regex)
                                  .Finish();
  return loader;
}

//
// HeaderMutationRules
//

const JsonLoaderInterface* HeaderMutationRules::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<HeaderMutationRules>()
          .OptionalField("disallow_is_error",
                         &HeaderMutationRules::disallow_is_error)
          .OptionalField("disallow_all", &HeaderMutationRules::disallow_all)
          .Finish();
  return loader;
}

void HeaderMutationRules::JsonPostLoad(const Json& json, const JsonArgs& args,
                                       ValidationErrors* errors) {
  auto createMatcherFromField =
      [&](const std::string& field_name) -> StringMatcher {
    auto expression = LoadJsonObjectField<SafeRegexMatch>(json.object(), args,
                                                          field_name, errors,
                                                          /*required=*/false);
    if (!expression.has_value()) {
      errors->AddError("no valid matcher found");
      return StringMatcher();
    }
    auto matcher = StringMatcher::Create(StringMatcher::Type::kSafeRegex,
                                         expression->regex);

    if (matcher.ok()) {
      return *matcher;
    }
    errors->AddError(matcher.status().message());
    return StringMatcher();
  };
  disallow_expression = createMatcherFromField("disallow_expression");
  allow_expression = createMatcherFromField("allow_expression");
}

std::string HeaderMutationRules::ToJsonString() const {
  Json::Object obj;
  if (disallow_is_error) {
    obj["disallow_is_error"] = Json::FromBool(disallow_is_error);
  }
  if (disallow_all) {
    obj["disallow_all"] = Json::FromBool(disallow_all);
  }
  auto dump_matcher = [&](const std::string& name,
                          const StringMatcher& matcher) {
    if (matcher.type() == StringMatcher::Type::kSafeRegex) {
      Json::Object regex_match;
      regex_match["regex"] =
          Json::FromString(matcher.regex_matcher()->pattern());
      obj[name] = Json::FromObject(std::move(regex_match));
    }
  };
  dump_matcher("allow_expression", allow_expression);
  dump_matcher("disallow_expression", disallow_expression);
  return JsonDump(Json::FromObject(std::move(obj)));
}

}  // namespace grpc_core
