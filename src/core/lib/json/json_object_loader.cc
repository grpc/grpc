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

template <typename T>
GPR_ATTRIBUTE_NOINLINE void ConvertNumber(const std::string& value, void* dest,
                                          ErrorList* error_list) {
  if (!absl::SimpleAtoi(value, static_cast<int32_t*>(dest))) {
    error_list->AddError("failed to parse.");
  }
}

template <typename T>
GPR_ATTRIBUTE_NOINLINE void ConvertString(const std::string& value, void* dest,
                                          ErrorList*) {
  *static_cast<T*>(dest) = value;
}

void TypeRef::Load(const Json::Object& json, void* dest,
                   ErrorList* errors) const {
  char* dest_chr = static_cast<char*>(dest);
  for (size_t i = 0; i < count; i++) {
    ScopedField field(errors, absl::StrCat(".", elements[i].name));
    auto it = json.find(elements[i].name);
    if (it == json.end()) {
      if (!elements[i].optional) errors->AddError("does not exist.");
      continue;
    }
    void* member_ptr = dest_chr + elements[i].member_offset;
    switch (elements[i].type) {
      case Element::kInt32:
      case Element::kUint32:
      case Element::kString:
        LoaderForTypeData(elements[i].type)
            .load_fn(it->second, member_ptr, errors);
        break;
      case Element::kVector: {
        if (it->second.type() != Json::Type::ARRAY) {
          errors->AddError("is not an array.");
          continue;
        }
        const Json::Array& array = it->second.array_value();
        auto loader = LoaderForTypeData(elements[i].type_data);
        for (size_t i = 0; i < array.size(); i++) {
          ScopedField array_elem(errors, absl::StrCat("[", i, "]"));
          const Json& elem_json = array[i];
          void* p = loader.vtable->create();
          loader.load_fn(elem_json, p, errors);
          loader.vtable->push_to_vec(p, member_ptr);
          loader.vtable->destroy(p);
        }
      } break;
      case Element::kMap: {
        if (it->second.type() != Json::Type::OBJECT) {
          errors->AddError("is not an object.");
          continue;
        }
        const Json::Object& object = it->second.object_value();
        auto loader = LoaderForTypeData(elements[i].type_data);
        for (const auto& pair : object) {
          ScopedField map_elem(errors, absl::StrCat(".", pair.first));
          const Json& elem_json = pair.second;
          void* p = loader.vtable->create();
          loader.load_fn(elem_json, p, errors);
          loader.vtable->insert_to_map(pair.first, p, member_ptr);
          loader.vtable->destroy(p);
        }
      } break;
    }
  }
}

Loader TypeRef::LoaderForTypeData(uint8_t tag) const {
  if (tag < Element::kVector) {
    void (*convert_number)(const std::string& value, void* dest,
                           ErrorList* error_list) = nullptr;
    void (*convert_string)(const std::string& value, void* dest,
                           ErrorList* error_list) = nullptr;
    const TypeVtable* vtable = nullptr;
    switch (static_cast<Element::Type>(tag)) {
      case Element::kVector:
      case Element::kMap:
        abort();  // not reachable
      case Element::kInt32:
        vtable = TypeVtableImpl<int32_t>::vtable();
        convert_number = ConvertNumber<int32_t>;
        break;
      case Element::kUint32:
        vtable = TypeVtableImpl<uint32_t>::vtable();
        convert_number = ConvertNumber<uint32_t>;
        break;
      case Element::kString:
        vtable = TypeVtableImpl<std::string>::vtable();
        convert_string = ConvertString<std::string>;
        break;
    }
    if (convert_number != nullptr) {
      return Loader{vtable, [convert_number](const Json& json, void* dest,
                                             ErrorList* errors) {
                      if (json.type() != Json::Type::NUMBER) {
                        errors->AddError("is not a number.");
                        return;
                      }
                      convert_number(json.string_value(), dest, errors);
                    }};
    }
    if (convert_string != nullptr) {
      return Loader{vtable, [convert_string](const Json& json, void* dest,
                                             ErrorList* errors) {
                      if (json.type() != Json::Type::STRING) {
                        errors->AddError("is not a string.");
                        return;
                      }
                      convert_string(json.string_value(), dest, errors);
                    }};
    }
    abort();  // not reachable
  }
  TypeRef type_ref = get_type_refs[tag - Element::kVector]();
  return Loader{type_ref.vtable,
                [type_ref](const Json& json, void* dest, ErrorList* errors) {
                  if (json.type() != Json::Type::OBJECT) {
                    errors->AddError("is not an object.");
                    return;
                  }
                  type_ref.Load(json.object_value(), dest, errors);
                }};
}

}  // namespace json_detail
}  // namespace grpc_core
