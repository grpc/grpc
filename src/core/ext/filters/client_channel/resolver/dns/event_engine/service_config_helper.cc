// Copyright 2023 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

namespace {

bool ValueInJsonArray(const Json::Array& array, const char* value) {
  for (const Json& entry : array) {
    if (entry.type() == Json::Type::STRING && entry.string_value() == value) {
      return true;
    }
  }
  return false;
}

}  // namespace

absl::StatusOr<std::string> ChooseServiceConfig(
    absl::string_view service_config_json) {
  auto json = Json::Parse(service_config_json);
  GRPC_RETURN_IF_ERROR(json.status());
  if (json->type() != Json::Type::ARRAY) {
    return absl::FailedPreconditionError(
        "Service Config Choices, error: should be of type array");
  }
  const Json* service_config = nullptr;
  ValidationErrors error_list;
  for (const Json& choice : json->array_value()) {
    if (choice.type() != Json::Type::OBJECT) {
      error_list.AddError(
          "Service Config Choice, error: should be of type object");
      continue;
    }
    // Check client language, if specified.
    auto it = choice.object_value().find("clientLanguage");
    if (it != choice.object_value().end()) {
      if (it->second.type() != Json::Type::ARRAY) {
        error_list.AddError(
            "field:clientLanguage error:should be of type array");
      } else if (!ValueInJsonArray(it->second.array_value(), "c++")) {
        continue;
      }
    }
    // Check client hostname, if specified.
    it = choice.object_value().find("clientHostname");
    if (it != choice.object_value().end()) {
      if (it->second.type() != Json::Type::ARRAY) {
        error_list.AddError(
            "field:clientHostname error:should be of type array");
      } else {
        // TODO(hork): replace with something non-iomgr
        char* hostname = grpc_gethostname();
        if (hostname == nullptr ||
            !ValueInJsonArray(it->second.array_value(), hostname)) {
          continue;
        }
      }
    }
    // Check percentage, if specified.
    it = choice.object_value().find("percentage");
    if (it != choice.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.AddError("field:percentage error:should be of type number");
      } else {
        int random_pct = rand() % 100;
        int percentage;
        if (sscanf(it->second.string_value().c_str(), "%d", &percentage) != 1) {
          error_list.AddError(
              "field:percentage error:should be of type integer");
        } else if (random_pct > percentage || percentage == 0) {
          continue;
        }
      }
    }
    // Found service config.
    it = choice.object_value().find("serviceConfig");
    if (it == choice.object_value().end()) {
      error_list.AddError("field:serviceConfig error:required field missing");
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.AddError("field:serviceConfig error:should be of type object");
    } else if (service_config == nullptr) {
      service_config = &it->second;
    }
  }
  if (error_list.FieldHasErrors()) {
    return error_list.status("Service Config Choices Parser",
                             absl::StatusCode::kFailedPrecondition);
  }
  if (service_config == nullptr) return "";
  return service_config->Dump();
}

}  // namespace grpc_core
