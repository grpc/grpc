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

#include "src/core/ext/xds/google_mesh_ca_certificate_provider.h"

#include <sstream>
#include <type_traits>

#include "absl/strings/str_cat.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_core {

namespace {

//
// Helper functions for extracting types from JSON
//
template <typename NumericType, typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     NumericType* output, ErrorVectorType* error_list) {
  static_assert(std::is_integral<NumericType>::value, "Integral required");
  if (json.type() != Json::Type::NUMBER) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be NUMBER")
            .c_str()));
    return false;
  }
  std::istringstream ss(json.string_value());
  ss >> *output;
  // The JSON parsing API should have dealt with parsing errors, but check
  // anyway
  if (GPR_UNLIKELY(ss.bad())) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:failed to parse.").c_str()));
    return false;
  }
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     bool* output, ErrorVectorType* error_list) {
  switch (json.type()) {
    case Json::Type::JSON_TRUE:
      *output = true;
      return true;
    case Json::Type::JSON_FALSE:
      *output = false;
      return true;
    default:
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", field_name, " error:type should be BOOLEAN")
              .c_str()));
      return false;
  }
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     std::string* output, ErrorVectorType* error_list) {
  if (json.type() != Json::Type::STRING) {
    *output = "";
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be STRING")
            .c_str()));
    return false;
  }
  *output = json.string_value();
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     const Json::Array** output, ErrorVectorType* error_list) {
  if (json.type() != Json::Type::ARRAY) {
    *output = nullptr;
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field ", field_name, " error:type should be ARRAY")
            .c_str()));
    return false;
  }
  *output = &json.array_value();
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     const Json::Object** output, ErrorVectorType* error_list) {
  if (json.type() != Json::Type::OBJECT) {
    *output = nullptr;
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be OBJECT")
            .c_str()));
    return false;
  }
  *output = &json.object_value();
  return true;
}

// Parses a JSON field of the form generated for a google.proto.Duration
// proto message, as per:
//   https://developers.google.com/protocol-buffers/docs/proto3#json
bool ParseDuration(const Json& field, grpc_millis* duration) {
  if (field.type() != Json::Type::STRING) return false;
  size_t len = field.string_value().size();
  if (field.string_value()[len - 1] != 's') return false;
  grpc_core::UniquePtr<char> buf(gpr_strdup(field.string_value().c_str()));
  *(buf.get() + len - 1) = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf.get(), '.');
  int nanos = 0;
  if (decimal_point != nullptr) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      return false;
    }
    int num_digits = static_cast<int>(strlen(decimal_point + 1));
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds =
      decimal_point == buf.get() ? 0 : gpr_parse_nonnegative_int(buf.get());
  if (seconds == -1) return false;
  *duration = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     grpc_millis* output, ErrorVectorType* error_list) {
  if (!ParseDuration(json, output)) {
    *output = GRPC_MILLIS_INF_PAST;
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name,
                     " error:type should be STRING of the form given by "
                     "google.proto.Duration.")
            .c_str()));
    return false;
  }
  return true;
}

template <typename T, typename ErrorVectorType>
bool ParseJsonObjectField(const Json::Object& object,
                          const std::string& field_name, T* output,
                          ErrorVectorType* error_list, bool optional = false) {
  auto it = object.find(field_name);
  if (it == object.end()) {
    if (!optional) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", field_name, " error:does not exist.")
              .c_str()));
    }
    return false;
  }
  auto& child_object_json = it->second;
  return ExtractJsonType(child_object_json, field_name, output, error_list);
}

}  // namespace

//
// GoogleMeshCaCertificateProviderFactory::Config
//
std::unique_ptr<GoogleMeshCaCertificateProviderFactory::Config>
GoogleMeshCaCertificateProviderFactory::Config::Parse(const Json& config_json,
                                                      grpc_error** error) {
  std::vector<grpc_error*> error_list;
  auto config =
      absl::make_unique<GoogleMeshCaCertificateProviderFactory::Config>();
  if (config_json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "error:config type should be OBJECT.");
    return nullptr;
  }
  const Json::Object* server = nullptr;
  if (ParseJsonObjectField(config_json.object_value(), "server", &server,
                           &error_list)) {
    std::string api_type;
    if (ParseJsonObjectField(*server, "api_type", &api_type, &error_list,
                             true)) {
      if (api_type != "GRPC") {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:api_type error:Only GRPC is supported"));
      }
    }
    const Json::Array* grpc_services = nullptr;
    if (ParseJsonObjectField(*server, "grpc_services", &grpc_services,
                             &error_list)) {
      if (grpc_services->size() != 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:grpc_services error:Need exactly one entry"));
      } else {
        const Json::Object* grpc_service = nullptr;
        if (ExtractJsonType((*grpc_services)[0], "grpc_services[0]",
                            &grpc_service, &error_list)) {
          const Json::Object* google_grpc = nullptr;
          if (ParseJsonObjectField(*grpc_service, "google_grpc", &google_grpc,
                                   &error_list)) {
            if (!ParseJsonObjectField(*google_grpc, "target_uri",
                                      &config->endpoint_, &error_list, true)) {
              config->endpoint_ = "meshca.googleapis.com";  // Default target
            }
            const Json::Array* call_credentials_array = nullptr;
            if (ParseJsonObjectField(*google_grpc, "call_credentials",
                                     &call_credentials_array, &error_list)) {
              if (call_credentials_array->size() != 1) {
                error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "field:call_credentials error:Need exactly one entry."));
              } else {
                const Json::Object* call_credentials = nullptr;
                if (ExtractJsonType((*call_credentials_array)[0],
                                    "call_credentials[0]", &call_credentials,
                                    &error_list)) {
                  const Json::Object* sts_service = nullptr;
                  if (ParseJsonObjectField(*call_credentials, "sts_service",
                                           &sts_service, &error_list)) {
                    if (!ParseJsonObjectField(
                            *sts_service, "token_exchange_service_uri",
                            &config->sts_config_.token_exchange_service_uri,
                            &error_list, true)) {
                      config->sts_config_.token_exchange_service_uri =
                          "securetoken.googleapis.com";  // default
                    }
                    ParseJsonObjectField(*sts_service, "resource",
                                         &config->sts_config_.resource,
                                         &error_list, true);
                    ParseJsonObjectField(*sts_service, "audience",
                                         &config->sts_config_.audience,
                                         &error_list, true);
                    if (!ParseJsonObjectField(*sts_service, "scope",
                                              &config->sts_config_.scope,
                                              &error_list, true)) {
                      config->sts_config_.scope =
                          "https://www.googleapis.com/auth/cloud-platform";  // default
                    }
                    ParseJsonObjectField(
                        *sts_service, "requested_token_type",
                        &config->sts_config_.requested_token_type, &error_list,
                        true);
                    ParseJsonObjectField(
                        *sts_service, "subject_token_path",
                        &config->sts_config_.subject_token_path, &error_list);
                    ParseJsonObjectField(
                        *sts_service, "subject_token_type",
                        &config->sts_config_.subject_token_type, &error_list);
                    ParseJsonObjectField(*sts_service, "actor_token_path",
                                         &config->sts_config_.actor_token_path,
                                         &error_list, true);
                    ParseJsonObjectField(*sts_service, "actor_token_type",
                                         &config->sts_config_.actor_token_type,
                                         &error_list, true);
                  }
                }
              }
            }
          }
          if (!ParseJsonObjectField(*grpc_service, "timeout", &config->timeout_,
                                    &error_list, true)) {
            config->timeout_ = 10 * 1000;  // 10sec default
          }
        }
      }
    }
  }
  if (!ParseJsonObjectField(config_json.object_value(), "certificate_lifetime",
                            &config->certificate_lifetime_, &error_list,
                            true)) {
    config->certificate_lifetime_ = 24 * 60 * 60 * 1000;  // 24hrs default
  }
  if (!ParseJsonObjectField(config_json.object_value(), "renewal_grace_period",
                            &config->renewal_grace_period_, &error_list,
                            true)) {
    config->renewal_grace_period_ = 12 * 60 * 60 * 1000;  // 12hrs default
  }
  std::string key_type;
  if (ParseJsonObjectField(config_json.object_value(), "key_type", &key_type,
                           &error_list, true)) {
    if (key_type != "RSA") {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:key_type error:Only RSA is supported."));
    }
  }
  if (!ParseJsonObjectField(config_json.object_value(), "key_size",
                            &config->key_size_, &error_list, true)) {
    config->key_size_ = 2048;  // default 2048 bit key size
  }
  if (!ParseJsonObjectField(config_json.object_value(), "location",
                            &config->location_, &error_list, true)) {
    // GCE/GKE Metadata server needs to be contacted to get the value.
  }
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_VECTOR(
        "Error parsing google Mesh CA config", &error_list);
    return nullptr;
  }
  return config;
}

//
// GoogleMeshCaCertificateProviderFactory
//

std::unique_ptr<CertificateProviderFactory::Config>
GoogleMeshCaCertificateProviderFactory::CreateCertificateProviderConfig(
    const Json& config_json, grpc_error** error) {
  return GoogleMeshCaCertificateProviderFactory::Config::Parse(config_json,
                                                               error);
}

}  // namespace grpc_core
