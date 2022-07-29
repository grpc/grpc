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

#include <grpc/support/port_platform.h>

#include "src/core/lib/json/json_object_loader.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"

namespace grpc_core {

void ErrorList::PushField(absl::string_view ext) {
  // Skip leading '.' for top-level field names.
  if (fields_.empty()) absl::ConsumePrefix(&ext, ".");
  fields_.emplace_back(std::string(ext));
}

void ErrorList::PopField() { fields_.pop_back(); }

void ErrorList::AddError(absl::string_view error) {
  field_errors_[absl::StrJoin(fields_, "")].emplace_back(error);
}

bool ErrorList::FieldHasErrors() const {
  return field_errors_.find(absl::StrJoin(fields_, "")) != field_errors_.end();
}

absl::Status ErrorList::status() const {
  if (field_errors_.empty()) return absl::OkStatus();
  std::vector<std::string> errors;
  for (const auto& p : field_errors_) {
    if (p.second.size() > 1) {
      errors.emplace_back(absl::StrCat("field:", p.first, " errors:[",
                                       absl::StrJoin(p.second, "; "), "]"));
    } else {
      errors.emplace_back(
          absl::StrCat("field:", p.first, " error:", p.second[0]));
    }
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "errors validating JSON: [", absl::StrJoin(errors, "; "), "]"));
}

namespace json_detail {

void LoadScalar::LoadInto(const Json& json, void* dst,
                          ErrorList* errors) const {
  // We accept either STRING or NUMBER for numeric values, as per
  // https://developers.google.com/protocol-buffers/docs/proto3#json.
  if (json.type() != Json::Type::STRING &&
      (!IsNumber() || json.type() != Json::Type::NUMBER)) {
    errors->AddError(
        absl::StrCat("is not a ", IsNumber() ? "number" : "string"));
    return;
  }
  return LoadInto(json.string_value(), dst, errors);
}

bool LoadString::IsNumber() const { return false; }

void LoadString::LoadInto(const std::string& value, void* dst,
                          ErrorList*) const {
  *static_cast<std::string*>(dst) = value;
}

bool LoadDuration::IsNumber() const { return false; }

void LoadDuration::LoadInto(const std::string& value, void* dst,
                            ErrorList* errors) const {
  absl::string_view buf(value);
  if (!absl::ConsumeSuffix(&buf, "s")) {
    errors->AddError("Not a duration (no s suffix)");
    return;
  }
  buf = absl::StripAsciiWhitespace(buf);
  auto decimal_point = buf.find('.');
  int nanos = 0;
  if (decimal_point != absl::string_view::npos) {
    absl::string_view after_decimal = buf.substr(decimal_point + 1);
    buf = buf.substr(0, decimal_point);
    if (!absl::SimpleAtoi(after_decimal, &nanos)) {
      errors->AddError("Not a duration (not a number of nanoseconds)");
      return;
    }
    if (after_decimal.length() > 9) {
      // We don't accept greater precision than nanos.
      errors->AddError("Not a duration (too many digits after decimal)");
      return;
    }
    for (size_t i = 0; i < (9 - after_decimal.length()); ++i) {
      nanos *= 10;
    }
  }
  int seconds;
  if (!absl::SimpleAtoi(buf, &seconds)) {
    errors->AddError("Not a duration (not a number of seconds)");
    return;
  }
  *static_cast<Duration*>(dst) =
      Duration::FromSecondsAndNanoseconds(seconds, nanos);
}

bool LoadNumber::IsNumber() const { return true; }

void LoadBool::LoadInto(const Json& json, void* dst, ErrorList* errors) const {
  if (json.type() == Json::Type::JSON_TRUE) {
    *static_cast<bool*>(dst) = true;
  } else if (json.type() == Json::Type::JSON_FALSE) {
    *static_cast<bool*>(dst) = false;
  } else {
    errors->AddError("is not a boolean");
  }
}

void LoadUnprocessedJsonObject::LoadInto(const Json& json, void* dst,
                                         ErrorList* errors) const {
  if (json.type() != Json::Type::OBJECT) {
    errors->AddError("is not an object");
    return;
  }
  *static_cast<Json::Object*>(dst) = json.object_value();
}

void LoadVector::LoadInto(const Json& json, void* dst,
                          ErrorList* errors) const {
  if (json.type() != Json::Type::ARRAY) {
    errors->AddError("is not an array");
    return;
  }
  const auto& array = json.array_value();
  for (size_t i = 0; i < array.size(); ++i) {
    ScopedField field(errors, absl::StrCat("[", i, "]"));
    LoadOne(array[i], dst, errors);
  }
}

void LoadMap::LoadInto(const Json& json, void* dst, ErrorList* errors) const {
  if (json.type() != Json::Type::OBJECT) {
    errors->AddError("is not an object");
    return;
  }
  for (const auto& pair : json.object_value()) {
    ScopedField field(errors, absl::StrCat("[\"", pair.first, "\"]"));
    LoadOne(pair.second, pair.first, dst, errors);
  }
}

bool LoadObject(const Json& json, const Element* elements, size_t num_elements,
                void* dst, ErrorList* errors) {
  if (json.type() != Json::Type::OBJECT) {
    errors->AddError("is not an object");
    return false;
  }
  for (size_t i = 0; i < num_elements; ++i) {
    const Element& element = elements[i];
    ScopedField field(errors, absl::StrCat(".", element.name));
    const auto& it = json.object_value().find(element.name);
    if (it == json.object_value().end()) {
      if (element.optional) continue;
      errors->AddError("field not present");
      continue;
    }
    char* field_dst = static_cast<char*>(dst) + element.member_offset;
    element.loader->LoadInto(it->second, field_dst, errors);
  }
  return true;
}

}  // namespace json_detail
}  // namespace grpc_core
