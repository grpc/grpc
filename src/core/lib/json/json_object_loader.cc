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

#include "absl/strings/str_cat.h"

namespace grpc_core {

void ErrorList::AddError(absl::string_view error) {
  std::string message = "field:";
  for (const auto& field : fields_) {
    message += field;
  }
  errors_.emplace_back(absl::StrCat(message, " error:", error));
}

void ErrorList::PushField(absl::string_view ext) {
  fields_.emplace_back(std::string(ext));
}

void ErrorList::PopField() { fields_.pop_back(); }

namespace json_detail {

void LoadScalar::LoadInto(const Json& json, void* dst,
                          ErrorList* errors) const {
  const Json::Type expected_type =
      IsNumber() ? Json::Type::NUMBER : Json::Type::STRING;
  if (json.type() != expected_type) {
    errors->AddError(
        absl::StrCat("is not a ", IsNumber() ? "number." : "string."));
    return;
  }
  return LoadInto(json.string_value(), dst, errors);
}

bool LoadNumber::IsNumber() const { return true; }

bool LoadString::IsNumber() const { return false; }

void LoadVector::LoadInto(const Json& json, void* dst,
                          ErrorList* errors) const {
  if (json.type() != Json::Type::ARRAY) {
    errors->AddError("is not an array.");
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
    errors->AddError("is not an object.");
    return;
  }
  for (const auto& pair : json.object_value()) {
    ScopedField field(errors, absl::StrCat(".", pair.first));
    LoadOne(pair.second, pair.first, dst, errors);
  }
}

void LoadObject(const Json& json, const Element* elements, size_t num_elements,
                void* dst, ErrorList* errors) {
  if (json.type() != Json::Type::OBJECT) {
    errors->AddError("is not an object.");
    return;
  }
  for (size_t i = 0; i < num_elements; ++i) {
    const Element& element = elements[i];
    ScopedField field(errors, absl::StrCat(".", element.name));
    const auto& it = json.object_value().find(element.name);
    if (it == json.object_value().end()) {
      if (element.optional) continue;
      errors->AddError("does not exist.");
      continue;
    }
    char* field_dst = static_cast<char*>(dst) + element.member_offset;
    element.loader->LoadInto(it->second, field_dst, errors);
  }
}

}  // namespace json_detail
}  // namespace grpc_core
