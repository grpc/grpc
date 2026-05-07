// Copyright 2026 gRPC authors.
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

#include "src/core/credentials/call/utils/redact_utils.h"

#include <cstddef>
#include <string>
#include <utility>

#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kRedactedString = "<redacted>";

void RedactJsonValue(Json& json) {
  if (json.type() == Json::Type::kObject) {
    Json::Object object = json.object();
    bool changed = false;
    for (auto& pair : object) {
      const std::string& key = pair.first;
      if (key == "access_token" || key == "refresh_token" ||
          key == "client_secret") {
        if (pair.second.type() != Json::Type::kString ||
            pair.second.string() != kRedactedString) {
          pair.second = Json::FromString(std::string(kRedactedString));
          changed = true;
        }
      } else {
        Json value_before = pair.second;
        RedactJsonValue(pair.second);
        if (pair.second != value_before) {
          changed = true;
        }
      }
    }
    if (changed) {
      json = Json::FromObject(std::move(object));
    }
  } else if (json.type() == Json::Type::kArray) {
    Json::Array array = json.array();
    bool changed = false;
    for (size_t i = 0; i < array.size(); ++i) {
      Json value_before = array[i];
      RedactJsonValue(array[i]);
      if (array[i] != value_before) {
        changed = true;
      }
    }
    if (changed) {
      json = Json::FromArray(std::move(array));
    }
  }
}

}  // namespace

std::string RedactSensitiveJsonFields(absl::string_view json_string) {
  auto json = JsonParse(json_string);
  if (!json.ok()) {
    // If parsing fails, return a generic redacted message
    // to avoid logging the potentially sensitive invalid JSON.
    return "[unparseable JSON - potential secrets redacted]";
  }
  RedactJsonValue(*json);
  return JsonDump(*json);
}

}  // namespace grpc_core
