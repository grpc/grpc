/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/health/health_check_parser.h"

namespace grpc_core {
namespace {
size_t g_health_check_parser_index;
}

UniquePtr<ServiceConfig::ParsedConfig> HealthCheckParser::ParseGlobalParams(
    const grpc_json* json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  const char* service_name = nullptr;
  InlinedVector<grpc_error*, 2> error_list;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) {
      GPR_DEBUG_ASSERT(false);
      continue;
    }
    if (strcmp(field->key, "healthCheckConfig") == 0) {
      if (field->type != GRPC_JSON_OBJECT) {
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:healthCheckConfig error:should be of type object");
        return nullptr;
      }
      for (grpc_json* sub_field = field->child; sub_field != nullptr;
           sub_field = sub_field->next) {
        if (sub_field->key == nullptr) {
          GPR_DEBUG_ASSERT(false);
          continue;
        }
        if (strcmp(sub_field->key, "serviceName") == 0) {
          if (service_name != nullptr) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "field:serviceName error:Duplicate "
                "entry"));
            continue;
          }
          if (sub_field->type != GRPC_JSON_STRING) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "field:serviceName error:should be of type string"));
            continue;
          }
          service_name = sub_field->value;
        }
      }
    }
  }
  if (error_list.empty()) {
    if (service_name == nullptr) return nullptr;
    return UniquePtr<ServiceConfig::ParsedConfig>(
        New<HealthCheckParsedObject>(service_name));
  }
  *error = ServiceConfig::CreateErrorFromVector("field:healthCheckConfig",
                                                &error_list);
  return nullptr;
}

void HealthCheckParser::Register() {
  g_health_check_parser_index = ServiceConfig::RegisterParser(
      UniquePtr<ServiceConfig::Parser>(New<HealthCheckParser>()));
}

size_t HealthCheckParser::ParserIndex() { return g_health_check_parser_index; }

}  // namespace grpc_core
