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

#include "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.h"

#include <algorithm>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json_util.h"

namespace grpc_core {

namespace {

const char* kMeshCaPlugin = "meshCA";

}  // namespace

//
// GoogleMeshCaCertificateProviderFactory::Config
//

const char* GoogleMeshCaCertificateProviderFactory::Config::name() const {
  return kMeshCaPlugin;
}

std::string GoogleMeshCaCertificateProviderFactory::Config::ToString() const {
  // TODO(yashykt): To be filled
  return "{}";
}

std::vector<grpc_error_handle>
GoogleMeshCaCertificateProviderFactory::Config::ParseJsonObjectStsService(
    const Json::Object& sts_service) {
  std::vector<grpc_error_handle> error_list_sts_service;
  if (!ParseJsonObjectField(sts_service, "token_exchange_service_uri",
                            &sts_config_.token_exchange_service_uri,
                            &error_list_sts_service, false)) {
    sts_config_.token_exchange_service_uri =
        "securetoken.googleapis.com";  // default
  }
  ParseJsonObjectField(sts_service, "resource", &sts_config_.resource,
                       &error_list_sts_service, false);
  ParseJsonObjectField(sts_service, "audience", &sts_config_.audience,
                       &error_list_sts_service, false);
  if (!ParseJsonObjectField(sts_service, "scope", &sts_config_.scope,
                            &error_list_sts_service, false)) {
    sts_config_.scope =
        "https://www.googleapis.com/auth/cloud-platform";  // default
  }
  ParseJsonObjectField(sts_service, "requested_token_type",
                       &sts_config_.requested_token_type,
                       &error_list_sts_service, false);
  ParseJsonObjectField(sts_service, "subject_token_path",
                       &sts_config_.subject_token_path,
                       &error_list_sts_service);
  ParseJsonObjectField(sts_service, "subject_token_type",
                       &sts_config_.subject_token_type,
                       &error_list_sts_service);
  ParseJsonObjectField(sts_service, "actor_token_path",
                       &sts_config_.actor_token_path, &error_list_sts_service,
                       false);
  ParseJsonObjectField(sts_service, "actor_token_type",
                       &sts_config_.actor_token_type, &error_list_sts_service,
                       false);
  return error_list_sts_service;
}

std::vector<grpc_error_handle>
GoogleMeshCaCertificateProviderFactory::Config::ParseJsonObjectCallCredentials(
    const Json::Object& call_credentials) {
  std::vector<grpc_error_handle> error_list_call_credentials;
  const Json::Object* sts_service = nullptr;
  if (ParseJsonObjectField(call_credentials, "sts_service", &sts_service,
                           &error_list_call_credentials)) {
    std::vector<grpc_error_handle> error_list_sts_service =
        ParseJsonObjectStsService(*sts_service);
    if (!error_list_sts_service.empty()) {
      error_list_call_credentials.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "field:sts_service", &error_list_sts_service));
    }
  }
  return error_list_call_credentials;
}

std::vector<grpc_error_handle>
GoogleMeshCaCertificateProviderFactory::Config::ParseJsonObjectGoogleGrpc(
    const Json::Object& google_grpc) {
  std::vector<grpc_error_handle> error_list_google_grpc;
  if (!ParseJsonObjectField(google_grpc, "target_uri", &endpoint_,
                            &error_list_google_grpc, false)) {
    endpoint_ = "meshca.googleapis.com";  // Default target
  }
  const Json::Array* call_credentials_array = nullptr;
  if (ParseJsonObjectField(google_grpc, "call_credentials",
                           &call_credentials_array, &error_list_google_grpc)) {
    if (call_credentials_array->size() != 1) {
      error_list_google_grpc.push_back(GRPC_ERROR_CREATE(
          "field:call_credentials error:Need exactly one entry."));
    } else {
      const Json::Object* call_credentials = nullptr;
      if (ExtractJsonType((*call_credentials_array)[0], "call_credentials[0]",
                          &call_credentials, &error_list_google_grpc)) {
        std::vector<grpc_error_handle> error_list_call_credentials =
            ParseJsonObjectCallCredentials(*call_credentials);
        if (!error_list_call_credentials.empty()) {
          error_list_google_grpc.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
              "field:call_credentials", &error_list_call_credentials));
        }
      }
    }
  }

  return error_list_google_grpc;
}

std::vector<grpc_error_handle>
GoogleMeshCaCertificateProviderFactory::Config::ParseJsonObjectGrpcServices(
    const Json::Object& grpc_service) {
  std::vector<grpc_error_handle> error_list_grpc_services;
  const Json::Object* google_grpc = nullptr;
  if (ParseJsonObjectField(grpc_service, "google_grpc", &google_grpc,
                           &error_list_grpc_services)) {
    std::vector<grpc_error_handle> error_list_google_grpc =
        ParseJsonObjectGoogleGrpc(*google_grpc);
    if (!error_list_google_grpc.empty()) {
      error_list_grpc_services.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "field:google_grpc", &error_list_google_grpc));
    }
  }
  if (!ParseJsonObjectFieldAsDuration(grpc_service, "timeout", &timeout_,
                                      &error_list_grpc_services, false)) {
    timeout_ = Duration::Seconds(10);  // 10sec default
  }
  return error_list_grpc_services;
}

std::vector<grpc_error_handle>
GoogleMeshCaCertificateProviderFactory::Config::ParseJsonObjectServer(
    const Json::Object& server) {
  std::vector<grpc_error_handle> error_list_server;
  std::string api_type;
  if (ParseJsonObjectField(server, "api_type", &api_type, &error_list_server,
                           false)) {
    if (api_type != "GRPC") {
      error_list_server.push_back(
          GRPC_ERROR_CREATE("field:api_type error:Only GRPC is supported"));
    }
  }
  const Json::Array* grpc_services = nullptr;
  if (ParseJsonObjectField(server, "grpc_services", &grpc_services,
                           &error_list_server)) {
    if (grpc_services->size() != 1) {
      error_list_server.push_back(GRPC_ERROR_CREATE(
          "field:grpc_services error:Need exactly one entry"));
    } else {
      const Json::Object* grpc_service = nullptr;
      if (ExtractJsonType((*grpc_services)[0], "grpc_services[0]",
                          &grpc_service, &error_list_server)) {
        std::vector<grpc_error_handle> error_list_grpc_services =
            ParseJsonObjectGrpcServices(*grpc_service);
        if (!error_list_grpc_services.empty()) {
          error_list_server.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
              "field:grpc_services", &error_list_grpc_services));
        }
      }
    }
  }
  return error_list_server;
}

RefCountedPtr<GoogleMeshCaCertificateProviderFactory::Config>
GoogleMeshCaCertificateProviderFactory::Config::Parse(
    const Json& config_json, grpc_error_handle* error) {
  auto config =
      MakeRefCounted<GoogleMeshCaCertificateProviderFactory::Config>();
  if (config_json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE("error:config type should be OBJECT.");
    return nullptr;
  }
  std::vector<grpc_error_handle> error_list;
  const Json::Object* server = nullptr;
  if (ParseJsonObjectField(config_json.object_value(), "server", &server,
                           &error_list)) {
    std::vector<grpc_error_handle> error_list_server =
        config->ParseJsonObjectServer(*server);
    if (!error_list_server.empty()) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("field:server", &error_list_server));
    }
  }
  if (!ParseJsonObjectFieldAsDuration(
          config_json.object_value(), "certificate_lifetime",
          &config->certificate_lifetime_, &error_list, false)) {
    config->certificate_lifetime_ = Duration::Hours(24);  // 24hrs default
  }
  if (!ParseJsonObjectFieldAsDuration(
          config_json.object_value(), "renewal_grace_period",
          &config->renewal_grace_period_, &error_list, false)) {
    config->renewal_grace_period_ = Duration::Hours(12);  // 12hrs default
  }
  std::string key_type;
  if (ParseJsonObjectField(config_json.object_value(), "key_type", &key_type,
                           &error_list, false)) {
    if (key_type != "RSA") {
      error_list.push_back(
          GRPC_ERROR_CREATE("field:key_type error:Only RSA is supported."));
    }
  }
  if (!ParseJsonObjectField(config_json.object_value(), "key_size",
                            &config->key_size_, &error_list, false)) {
    config->key_size_ = 2048;  // default 2048 bit key size
  }
  if (!ParseJsonObjectField(config_json.object_value(), "location",
                            &config->location_, &error_list, false)) {
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

const char* GoogleMeshCaCertificateProviderFactory::name() const {
  return kMeshCaPlugin;
}

RefCountedPtr<CertificateProviderFactory::Config>
GoogleMeshCaCertificateProviderFactory::CreateCertificateProviderConfig(
    const Json& config_json, grpc_error_handle* error) {
  return GoogleMeshCaCertificateProviderFactory::Config::Parse(config_json,
                                                               error);
}

}  // namespace grpc_core
