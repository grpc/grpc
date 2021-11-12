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
  Element() = default;
  template <typename A, typename B> 
  Element(B A::* p, Type type, bool optional, uint8_t type_data,
          const char* name)
      : member_offset(static_cast<uint16_t>(
            reinterpret_cast<uintptr_t>(&(static_cast<A*>(nullptr)->*p)))),
        type(type),
        optional(optional),
        type_data(type_data),
        name{} {
    strcpy(this->name, name);
  }
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
  static const TypeVtable vtable;

  static void* Create() { return new T;  }
  static void Destroy(void* ptr) { delete static_cast<T*>(ptr); }
  static void PushToVec(void* ptr, void* vec) {
    auto* vec_ptr = static_cast<std::vector<T>*>(vec);
    auto* src_ptr = static_cast<T*>(ptr);
    vec_ptr->emplace_back(std::move(*src_ptr));
  }
  static void InsertToMap(std::string key, void* ptr, void* map) {
    auto* map_ptr = static_cast<std::map<std::string, T>*>(map);
    auto* src_ptr = static_cast<T*>(ptr);
    map_ptr->emplace(std::move(key), std::move(*src_ptr));
  }
};

template <typename T>
const TypeVtable TypeVtableImpl<T>::vtable{
    Create,
    Destroy,
    PushToVec,
    InsertToMap,
};

class TypeRefProvider;

struct TypeRef {
  const TypeVtable* vtable;
  const Element* elements;
  size_t count;
  const TypeRefProvider* const* type_ref_providers;

  void Load(const Json::Object& json, void* dest,
            ErrorList* errors) const GPR_ATTRIBUTE_NOINLINE;
  using LoadFn = absl::FunctionRef<void(const Json& json, void* dest_ptr)>;
  void WithLoaderForTypeData(
      uint8_t tag, ErrorList* errors,
      absl::FunctionRef<void(const TypeVtable* vtable, LoadFn load)>) const
      GPR_ATTRIBUTE_NOINLINE;
  void LoadScalar(const Json& json, Element::Type type, void* dest,
                  ErrorList* errors) const GPR_ATTRIBUTE_NOINLINE;
  void LoadVector(const Json& json, uint8_t type_data, void* dest,
                  ErrorList* errors) const GPR_ATTRIBUTE_NOINLINE;
  void LoadMap(const Json& json, uint8_t type_data, void* dest,
               ErrorList* errors) const GPR_ATTRIBUTE_NOINLINE;
};

class TypeRefProvider {
 public:
  virtual void WithTypeRef(absl::FunctionRef<void(const TypeRef&)>) const = 0;

 protected:
  ~TypeRefProvider() = default;
};

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

// Vec<T, kSize> provides a constant array type that can be appended to by copying.
// It's setup so that most compilers can optimize away all of its operations.
template <typename T, size_t kSize>
class Vec {
 public:
  Vec(const Vec<T, kSize - 1>& other, const T& new_value) {
    for (size_t i = 0; i < other.size(); i++) values_[i] = other.data()[i];
    values_[kSize - 1] = new_value;
  }

  const T* data() const { return values_; }
  size_t size() const { return kSize; }

 private:
  T values_[kSize];
};

template <typename T>
class Vec<T, 0> {
 public:
  const T* data() const { return nullptr; }
  size_t size() const { return 0; }
};

template <typename T, size_t kElemCount, size_t kTypeCount>
class FinishedJsonObjectLoader final : public TypeRefProvider {
 public:
  FinishedJsonObjectLoader(const Vec<Element, kElemCount>& elements,
      const Vec<const TypeRefProvider*, kTypeCount>& type_ref_providers)
      : elements_(elements), type_ref_providers_(type_ref_providers) {
  }

  using ResultType = T;

  GPR_ATTRIBUTE_NOINLINE T Load(const Json::Object& json,
                                ErrorList* errors) const {
    T t;
    WithTypeRef(
        [&](const TypeRef& type_ref) { type_ref.Load(json, &t, errors); });
    return t;
  }

  void WithTypeRef(absl::FunctionRef<void(const TypeRef&)> fn) const override {
    fn(TypeRef{&TypeVtableImpl<T>::vtable, elements_.data(), kElemCount,
               type_ref_providers_.data()});
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Vec<Element, kElemCount> elements_;
  GPR_NO_UNIQUE_ADDRESS Vec<const TypeRefProvider*, kTypeCount> type_ref_providers_;
};

template <typename T, size_t kElemCount = 0, size_t kTypeCount = 0>
class JsonObjectLoader final {
 public:
  JsonObjectLoader() {
    static_assert(kElemCount == 0,
                  "Only initial loader step can have kElemCount==0.");
    static_assert(kTypeCount == 0,
                  "Only initial loader step can have kTypeCount==0.");
  }

  FinishedJsonObjectLoader<T, kElemCount, kTypeCount> Finish()
      const {
    return FinishedJsonObjectLoader<T, kElemCount, kTypeCount>(
        elements_, type_ref_providers_);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(const char* name,
                                                        U T::*p) const {
    return Field(name, false, p);
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> Field(
      const char* name, std::vector<U> T::*p,
      const FinishedJsonObjectLoader<U, N, M>* u_loader) const {
    return Field(name, false, p, u_loader);
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> Field(
      const char* name, std::map<std::string, U> T::*p,
      const FinishedJsonObjectLoader<U, N, M>* u_loader) const {
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

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> OptionalField(
      const char* name, std::map<std::string, U> T::*p,
      const JsonObjectLoader<U, N, M>* u_loader) const {
    return Field(name, true, p, u_loader);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(const char* name,
                                                        bool optional,
                                                        U T::*p) const {
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount>(
        elements_, type_ref_providers_,
        Element(p, ElementTypeOf<U>::type(), optional, 0, name));
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(
      const char* name, bool optional, std::vector<U> T::*p) const {
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount>(
        elements_, type_ref_providers_,
        Element(p, Element::kVector,optional,
                ElementTypeOf<U>::type(), name));
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> Field(
      const char* name, bool optional, std::vector<U> T::*p,
      const FinishedJsonObjectLoader<U, N, M>* u_loader) const {
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1>(
        elements_, type_ref_providers_,
        Element(p, Element::kVector, optional,
                kTypeCount + static_cast<size_t>(Element::kVector),
                name),
        u_loader);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount> Field(
      const char* name, bool optional, std::map<std::string, U> T::*p) const {
    return JsonObjectLoader<T, kElemCount + 1, kTypeCount>(
        elements_, type_ref_providers_,
        Element(p, Element::kMap,optional,
                ElementTypeOf<U>::type(), name));
  }

  template <typename U, size_t N, size_t M>
  JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1> Field(
      const char* name, bool optional, std::map<std::string, U> T::*p,
      const FinishedJsonObjectLoader<U, N, M>* u_loader) const {
    return 
      JsonObjectLoader<T, kElemCount + 1, kTypeCount + 1>(
        elements_, type_ref_providers_,
      Element(
        p, Element::kMap, optional,
        kTypeCount + static_cast<size_t>(Element::kVector), name), u_loader);
  }

  JsonObjectLoader(
      const Vec<Element, kElemCount - 1>& elements,
      const Vec<const TypeRefProvider*, kTypeCount>&
          type_ref_providers,
      Element new_element)
      : elements_(elements, new_element),
        type_ref_providers_(type_ref_providers) {}

  JsonObjectLoader(
      const Vec<Element, kElemCount - 1>& elements,
      const Vec<const TypeRefProvider*,
                             kTypeCount - 1>& type_ref_providers,
      Element new_element,
      const TypeRefProvider* new_type_ref_provider)
      : elements_(elements, new_element),
        type_ref_providers_(type_ref_providers, new_type_ref_provider) {}

 private:
  GPR_NO_UNIQUE_ADDRESS Vec<Element, kElemCount>
      elements_;
  GPR_NO_UNIQUE_ADDRESS
      Vec<const TypeRefProvider*, kTypeCount>
          type_ref_providers_;
};

}  // namespace json_detail

template <typename T>
using JsonObjectLoader = json_detail::JsonObjectLoader<T>;

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_JSON_JSON_OBJECT_LOADER_H
