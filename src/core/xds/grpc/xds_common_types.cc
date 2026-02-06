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

#include <cstddef>
#include <memory>
#include <string>

#include "src/core/util/json/json_reader.h"
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
// XdsGrpcService
//

namespace {

struct InitialMetadata {
  std::string key;
  std::string value;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader = JsonObjectLoader<InitialMetadata>()
                                    .Field("key", &InitialMetadata::key)
                                    .Field("value", &InitialMetadata::value)
                                    .Finish();
    return loader;
  }
};

}  // namespace

std::unique_ptr<GrpcXdsServerTarget> ParseGrpcXdsServerTarget(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  auto server_target_itr = json.object().find("server_target");
  if (server_target_itr != json.object().end()) {
    ValidationErrors::ScopedField field(errors, ".server_target");
    if (server_target_itr->second.type() != Json::Type::kObject) {
      errors->AddError("is not an object");
    } else {
      const Json& target_json = server_target_itr->second;
      std::string server_uri =
          LoadJsonObjectField<std::string>(target_json.object(), args,
                                           "server_uri", errors)
              .value_or("");
      auto channel_creds_config =
          ParseXdsBootstrapChannelCreds(target_json, args, errors);
      auto call_creds_configs =
          ParseXdsBootstrapCallCreds(target_json, args, errors);
      return std::make_unique<GrpcXdsServerTarget>(
          std::move(server_uri), std::move(channel_creds_config),
          std::move(call_creds_configs));
    }
  }
  return nullptr;
}

std::vector<std::pair<std::string, std::string>> ParseInitialMetadata(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  std::vector<std::pair<std::string, std::string>> initial_metadata;
  auto md = LoadJsonObjectField<std::vector<InitialMetadata>>(
      json.object(), args, "initial_metadata", errors);
  if (md.has_value()) {
    ValidationErrors::ScopedField field(errors, ".initial_metadata");
    for (const auto& metadata : md.value()) {
      initial_metadata.emplace_back(metadata.key, metadata.value);
    }
  }
  return initial_metadata;
}

const JsonLoaderInterface* XdsGrpcService::JsonLoader(const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<XdsGrpcService>()
                                  .Field("timeout", &XdsGrpcService::timeout)
                                  .Finish();
  return loader;
}

void XdsGrpcService::JsonPostLoad(const Json& json, const JsonArgs& args,
                                  ValidationErrors* errors) {
  // parse server_target
  server_target = ParseGrpcXdsServerTarget(json, args, errors);
  // parse initial_medata
  initial_metadata = ParseInitialMetadata(json, args, errors);
}

std::string XdsGrpcService::ToJsonString() const {
  Json::Object root;
  if (server_target != nullptr) {
    auto target_json = JsonParse(server_target->ToJsonString());
    if (target_json.ok()) {
      root["server_target"] = *target_json;
    }
  }
  root["timeout"] = Json::FromString(timeout.ToJsonString());
  if (!initial_metadata.empty()) {
    Json::Array metadata_array;
    for (const auto& [key, value] : initial_metadata) {
      metadata_array.push_back(Json::FromObject({
          {"key", Json::FromString(key)},
          {"value", Json::FromString(value)},
      }));
    }
    root["initial_metadata"] = Json::FromArray(std::move(metadata_array));
  }
  return JsonDump(Json::FromObject(root));
}

}  // namespace grpc_core
