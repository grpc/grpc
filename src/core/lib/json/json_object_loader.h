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

#ifndef GRPC_CORE_LIB_JSON_JSON_OBJECT_LOADER_H
#define GRPC_CORE_LIB_JSON_JSON_OBJECT_LOADER_H

#include <grpc/support/port_platform.h>

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include "src/core/lib/json/json.h"

namespace grpc_core {

class ErrorList {
 public:
  void PushField(absl::string_view ext) GPR_ATTRIBUTE_NOINLINE;
  void PopField() GPR_ATTRIBUTE_NOINLINE;
  void AddError(absl::string_view error) GPR_ATTRIBUTE_NOINLINE;

  const std::vector<std::string>& errors() const { return errors_; }

 private:
  std::vector<std::string> errors_;
  std::vector<std::string> fields_;
};

class ScopedField {
 public:
  ScopedField(ErrorList* error_list, absl::string_view field_name)
      : error_list_(error_list) {
    error_list_->PushField(field_name);
  }
  ~ScopedField() { error_list_->PopField(); }

 private:
  ErrorList* error_list_;
};

namespace json_detail {

struct Element {
  enum Type : uint8_t {
    kInt32,
    kUint32,
    kString,
    // Vector should be the first thing after scalar types.
    kVector,
    kMap,
  };
  uint16_t member_offset;
  Type type;
  bool optional;
  uint8_t type_data;
  char name[11];
};

struct TypeVtable {
  void* (*create)();
  void (*destroy)(void*);
  void (*push_to_vec)(void* ptr, void* vec);
  void (*insert_to_map)(std::string key, void* ptr, void* map);
};

template <typename T>
struct TypeVtableImpl {
  static TypeVtable* vtable() {
    static TypeVtable vtable{
        // create
        []() -> void* { return new T(); },
        // destroy
        [](void* ptr) { delete static_cast<T*>(ptr); },
        // push_to_vec
        [](void* ptr, void* vec) {
          auto* vec_ptr = static_cast<std::vector<T>*>(vec);
          auto* src_ptr = static_cast<T*>(ptr);
          vec_ptr->emplace_back(std::move(*src_ptr));
        },
        // insert_to_map
        [](std::string key, void* ptr, void* map) {
          auto* map_ptr = static_cast<std::map<std::string, T>*>(map);
          auto* src_ptr = static_cast<T*>(ptr);
          map_ptr->emplace(std::move(key), std::move(*src_ptr));
        },
    };
    return &vtable;
  }
};

struct TypeRef;
struct GetTypeRefFn {
  TypeRef (*fn)(const void* arg);
  const void* arg;
  TypeRef operator()() const;
};

struct Loader {
  const TypeVtable* vtable;
  std::function<void(const Json&, void* dest, ErrorList*)> load_fn;
};

struct TypeRef {
  const TypeVtable* vtable;
  const Element* elements;
  size_t count;
  const GetTypeRefFn* get_type_refs;

  void Load(const Json::Object& json, void* dest,
            ErrorList* errors) const GPR_ATTRIBUTE_NOINLINE;
  Loader LoaderForTypeData(uint8_t tag) const GPR_ATTRIBUTE_NOINLINE;
};

inline TypeRef GetTypeRefFn::operator()() const { return fn(arg); }

template <typename T>
struct ElementTypeOf;
template <>
struct ElementTypeOf<int32_t> {
  static Element::Type type() { return Element::kInt32; }
};
template <>
struct ElementTypeOf<uint32_t> {
  static Element::Type type() { return Element::kUint32; }
};
template <>
struct ElementTypeOf<std::string> {
  static Element::Type type() { return Element::kString; }
};

}  // namespace json_detail

template <typename T, size_t kElemCount = 0, size_t kTypeCount = 0>
class JsonObjectLoader final {
 public:
  JsonObjectLoader() {
    static_assert(kElemCount == 0,
                  "Only initial loader step can have kElemCount==0.");
    static_assert(kTypeCount == 0,
                  "Only initial loader step can have kTypeCount==0.");
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(const char* name,
                                                        U T::*p) const {
    return Field(name, false, p);
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> Field(
      const char* name, std::vector<U> T::*p,
      const JsonObjectLoader<U, N, M>* u_loader) const {
    return Field(name, false, p, u_loader);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> OptionalField(
      const char* name, U T::*p) const {
    return Field(name, true, p);
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> OptionalField(
      const char* name, std::vector<U> T::*p,
      const JsonObjectLoader<U, N, M>* u_loader) const {
    return Field(name, true, p, u_loader);
  }

  GPR_ATTRIBUTE_NOINLINE T Load(const Json::Object& json,
                                ErrorList* errors) const {
    T t;
    get_type_ref_fn()().Load(json, &t, errors);
    return t;
  }

  json_detail::GetTypeRefFn get_type_ref_fn() const {
    return json_detail::GetTypeRefFn{
        [](const void* arg) -> json_detail::TypeRef {
          const auto* loader = static_cast<const JsonObjectLoader*>(arg);
          return json_detail::TypeRef{
              json_detail::TypeVtableImpl<T>::vtable(),
              loader->elements_,
              kElemCount,
              loader->get_type_ref_fns_,
          };
        },
        this};
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(const char* name,
                                                        bool optional,
                                                        U T::*p) const {
    json_detail::Element e;
    strcpy(e.name, name);
    e.member_offset = static_cast<uint16_t>(
        reinterpret_cast<uintptr_t>(&(static_cast<T*>(nullptr)->*p)));
    e.optional = optional;
    e.type = json_detail::ElementTypeOf<U>::type();
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount>(
        elements_, get_type_ref_fns_, e);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(
      const char* name, bool optional, std::vector<U> T::*p) const {
    json_detail::Element e;
    strcpy(e.name, name);
    e.member_offset = static_cast<uint16_t>(
        reinterpret_cast<uintptr_t>(&(static_cast<T*>(nullptr)->*p)));
    e.optional = optional;
    e.type = json_detail::Element::kVector;
    e.type_data = json_detail::ElementTypeOf<U>::type();
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount>(
        elements_, get_type_ref_fns_, e);
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> Field(
      const char* name, bool optional, std::vector<U> T::*p,
      const JsonObjectLoader<U, N, M>* u_loader) const {
    json_detail::Element e;
    strcpy(e.name, name);
    e.member_offset = static_cast<uint16_t>(
        reinterpret_cast<uintptr_t>(&(static_cast<T*>(nullptr)->*p)));
    e.optional = optional;
    e.type = json_detail::Element::kVector;
    e.type_data =
        kTypeCount + static_cast<size_t>(json_detail::Element::kVector);
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1>(
        elements_, get_type_ref_fns_, e, u_loader->get_type_ref_fn());
  }

  JsonObjectLoader(const json_detail::Element* elements,
                   const json_detail::GetTypeRefFn* get_type_ref_fns,
                   json_detail::Element new_element) {
    for (size_t i = 0; i < kElemCount - 1; i++) {
      elements_[i] = elements[i];
    }
    elements_[kElemCount - 1] = new_element;
    for (size_t i = 0; i < kTypeCount; i++) {
      get_type_ref_fns_[i] = get_type_ref_fns[i];
    }
  }

  JsonObjectLoader(const json_detail::Element* elements,
                   const json_detail::GetTypeRefFn* get_type_ref_fns,
                   json_detail::Element new_element,
                   json_detail::GetTypeRefFn new_get_type_ref_fn) {
    for (size_t i = 0; i < kElemCount - 1; i++) {
      elements_[i] = elements[i];
    }
    elements_[kElemCount - 1] = new_element;
    for (size_t i = 0; i < kTypeCount - 1; i++) {
      get_type_ref_fns_[i] = get_type_ref_fns[i];
    }
    get_type_ref_fns_[kTypeCount - 1] = new_get_type_ref_fn;
  }

 private:
  GPR_NO_UNIQUE_ADDRESS json_detail::Element elements_[kElemCount];
  GPR_NO_UNIQUE_ADDRESS json_detail::GetTypeRefFn get_type_ref_fns_[kTypeCount];
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_JSON_JSON_OBJECT_LOADER_H
