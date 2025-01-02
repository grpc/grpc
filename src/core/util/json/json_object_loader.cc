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

#include "src/core/util/json/json_object_loader.h"

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

namespace grpc_core {
namespace json_detail {

void LoadScalar::LoadInto(const Json& json, const JsonArgs& /*args*/, void* dst,
                          ValidationErrors* errors) const {
  // We accept either kString or kNumber for numeric values, as per
  // https://developers.google.com/protocol-buffers/docs/proto3#json.
  if (json.type() != Json::Type::kString &&
      (!IsNumber() || json.type() != Json::Type::kNumber)) {
    errors->AddError(
        absl::StrCat("is not a ", IsNumber() ? "number" : "string"));
    return;
  }
  return LoadInto(json.string(), dst, errors);
}

bool LoadString::IsNumber() const { return false; }

void LoadString::LoadInto(const std::string& value, void* dst,
                          ValidationErrors*) const {
  *static_cast<std::string*>(dst) = value;
}

bool LoadDuration::IsNumber() const { return false; }

void LoadDuration::LoadInto(const std::string& value, void* dst,
                            ValidationErrors* errors) const {
  absl::string_view buf(value);
  if (!absl::ConsumeSuffix(&buf, "s")) {
    errors->AddError("Not a duration (no s suffix)");
    return;
  }
  buf = absl::StripAsciiWhitespace(buf);
  auto decimal_point = buf.find('.');
  int32_t nanos = 0;
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
  int64_t seconds;
  if (!absl::SimpleAtoi(buf, &seconds)) {
    errors->AddError("Not a duration (not a number of seconds)");
    return;
  }
  // Acceptable range for seconds documented at
  // https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#google.protobuf.Duration
  if (seconds < 0 || seconds > 315576000000) {
    errors->AddError("seconds must be in the range [0, 315576000000]");
  }
  *static_cast<Duration*>(dst) =
      Duration::FromSecondsAndNanoseconds(seconds, nanos);
}

bool LoadNumber::IsNumber() const { return true; }

void LoadBool::LoadInto(const Json& json, const JsonArgs&, void* dst,
                        ValidationErrors* errors) const {
  if (json.type() != Json::Type::kBoolean) {
    errors->AddError("is not a boolean");
    return;
  }
  *static_cast<bool*>(dst) = json.boolean();
}

void LoadUnprocessedJsonObject::LoadInto(const Json& json, const JsonArgs&,
                                         void* dst,
                                         ValidationErrors* errors) const {
  if (json.type() != Json::Type::kObject) {
    errors->AddError("is not an object");
    return;
  }
  *static_cast<Json::Object*>(dst) = json.object();
}

void LoadUnprocessedJsonArray::LoadInto(const Json& json, const JsonArgs&,
                                        void* dst,
                                        ValidationErrors* errors) const {
  if (json.type() != Json::Type::kArray) {
    errors->AddError("is not an array");
    return;
  }
  *static_cast<Json::Array*>(dst) = json.array();
}

void LoadVector::LoadInto(const Json& json, const JsonArgs& args, void* dst,
                          ValidationErrors* errors) const {
  if (json.type() != Json::Type::kArray) {
    errors->AddError("is not an array");
    return;
  }
  const auto& array = json.array();
  const LoaderInterface* element_loader = ElementLoader();
  for (size_t i = 0; i < array.size(); ++i) {
    ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
    void* element = EmplaceBack(dst);
    element_loader->LoadInto(array[i], args, element, errors);
  }
}

void AutoLoader<std::vector<bool>>::LoadInto(const Json& json,
                                             const JsonArgs& args, void* dst,
                                             ValidationErrors* errors) const {
  if (json.type() != Json::Type::kArray) {
    errors->AddError("is not an array");
    return;
  }
  const auto& array = json.array();
  const LoaderInterface* element_loader = LoaderForType<bool>();
  std::vector<bool>* vec = static_cast<std::vector<bool>*>(dst);
  for (size_t i = 0; i < array.size(); ++i) {
    ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
    bool elem = false;
    element_loader->LoadInto(array[i], args, &elem, errors);
    vec->push_back(elem);
  }
}

void LoadMap::LoadInto(const Json& json, const JsonArgs& args, void* dst,
                       ValidationErrors* errors) const {
  if (json.type() != Json::Type::kObject) {
    errors->AddError("is not an object");
    return;
  }
  const LoaderInterface* element_loader = ElementLoader();
  for (const auto& pair : json.object()) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat("[\"", pair.first, "\"]"));
    void* element = Insert(pair.first, dst);
    element_loader->LoadInto(pair.second, args, element, errors);
  }
}

void LoadWrapped::LoadInto(const Json& json, const JsonArgs& args, void* dst,
                           ValidationErrors* errors) const {
  void* element = Emplace(dst);
  size_t starting_error_size = errors->size();
  ElementLoader()->LoadInto(json, args, element, errors);
  if (errors->size() > starting_error_size) Reset(dst);
}

bool LoadObject(const Json& json, const JsonArgs& args, const Element* elements,
                size_t num_elements, void* dst, ValidationErrors* errors) {
  if (json.type() != Json::Type::kObject) {
    errors->AddError("is not an object");
    return false;
  }
  for (size_t i = 0; i < num_elements; ++i) {
    const Element& element = elements[i];
    if (element.enable_key != nullptr && !args.IsEnabled(element.enable_key)) {
      continue;
    }
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".", element.name));
    const auto& it = json.object().find(element.name);
    if (it == json.object().end() || it->second.type() == Json::Type::kNull) {
      if (element.optional) continue;
      errors->AddError("field not present");
      continue;
    }
    char* field_dst = static_cast<char*>(dst) + element.member_offset;
    element.loader->LoadInto(it->second, args, field_dst, errors);
  }
  return true;
}

const Json* GetJsonObjectField(const Json::Object& json,
                               absl::string_view field,
                               ValidationErrors* errors, bool required) {
  auto it = json.find(std::string(field));
  if (it == json.end()) {
    if (required) errors->AddError("field not present");
    return nullptr;
  }
  return &it->second;
}

}  // namespace json_detail
}  // namespace grpc_core
