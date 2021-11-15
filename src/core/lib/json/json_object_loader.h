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

class LoaderInterface {
 public:
  virtual void LoadInto(const Json& json, void* dst,
                        ErrorList* errors) const = 0;
};

class LoadScalar : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override;

 private:
  virtual bool IsNumber() const = 0;

  virtual void LoadInto(const std::string& json, void* dst,
                        ErrorList* errors) const = 0;
};

class LoadNumber : public LoadScalar {
 private:
  bool IsNumber() const override;
};

template <typename T>
class LoadContainer : public LoaderInterface {
 protected:
  virtual void LoadOne(const Json& json, T* dst) = 0;
};

template <typename T>
class TypedLoadNumber : public LoadNumber {
 private:
  void LoadInto(const std::string& value, void* dst,
                ErrorList* errors) const override {
    if (!absl::SimpleAtoi(value, static_cast<T*>(dst))) {
      errors->AddError("failed to parse number.");
    }
  }
};

class LoadString : public LoadScalar {
 private:
  bool IsNumber() const override;
};

class LoadVector : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override;

 private:
  virtual void LoadOne(const Json& json, void* dst,
                       ErrorList* errors) const = 0;
};

class LoadMap : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override;

 private:
  virtual void LoadOne(const Json& json, const std::string& name, void* dst,
                       ErrorList* errors) const = 0;
};

template <typename T>
const LoaderInterface* LoaderForType();

template <typename T>
class AutoLoader final : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override {
    T::JsonLoader()->LoadInto(json, dst, errors);
  }
};

template <>
class AutoLoader<int32_t> final : public TypedLoadNumber<int32_t> {};
template <>
class AutoLoader<uint32_t> final : public TypedLoadNumber<uint32_t> {};
template <>
class AutoLoader<int64_t> final : public TypedLoadNumber<int32_t> {};
template <>
class AutoLoader<uint64_t> final : public TypedLoadNumber<uint32_t> {};
template <>
class AutoLoader<std::string> final : public LoadString {
 private:
  void LoadInto(const std::string& value, void* dst,
                ErrorList* errors) const override {
    *static_cast<std::string*>(dst) = value;
  }
};

template <typename T>
class AutoLoader<std::vector<T>> final : public LoadVector {
 private:
  void LoadOne(const Json& json, void* dst, ErrorList* errors) const final {
    auto* vec = static_cast<std::vector<T>*>(dst);
    T value;
    LoaderForType<T>()->LoadInto(json, &value, errors);
    vec->push_back(std::move(value));
  }
};

template <typename T>
class AutoLoader<std::map<std::string, T>> final : public LoadMap {
 private:
  void LoadOne(const Json& json, const std::string& name, void* dst,
               ErrorList* errors) const final {
    auto* map = static_cast<std::map<std::string, T>*>(dst);
    T value;
    LoaderForType<T>()->LoadInto(json, &value, errors);
    map->emplace(name, std::move(value));
  }
};

template <typename T>
const LoaderInterface* LoaderForType() {
  static const AutoLoader<T> loader;
  return &loader;
}

struct Element {
  Element() = default;
  template <typename A, typename B>
  Element(const char* name, bool optional, B A::*p,
          const LoaderInterface* loader)
      : loader(loader),
        member_offset(static_cast<uint16_t>(
            reinterpret_cast<uintptr_t>(&(static_cast<A*>(nullptr)->*p)))),
        optional(optional),
        name{} {
    strcpy(this->name, name);
  }
  const LoaderInterface* loader;
  uint16_t member_offset;
  bool optional;
  char name[13];
};

// Vec<T, kSize> provides a constant array type that can be appended to by
// copying. It's setup so that most compilers can optimize away all of its
// operations.
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

void LoadObject(const Json& json, const Element* elements, size_t num_elements,
                void* dst, ErrorList* errors);

template <typename T, size_t kElemCount>
class FinishedJsonObjectLoader final : public LoaderInterface {
 public:
  explicit FinishedJsonObjectLoader(const Vec<Element, kElemCount>& elements)
      : elements_(elements) {}

  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override {
    LoadObject(json, elements_.data(), elements_.size(), dst, errors);
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Vec<Element, kElemCount> elements_;
};

template <typename T, size_t kElemCount = 0>
class JsonObjectLoader final {
 public:
  JsonObjectLoader() {
    static_assert(kElemCount == 0,
                  "Only initial loader step can have kElemCount==0.");
  }

  FinishedJsonObjectLoader<T, kElemCount> Finish() const {
    return FinishedJsonObjectLoader<T, kElemCount>(elements_);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1> Field(const char* name, U T::*p) const {
    return Field(name, false, p);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1> OptionalField(const char* name,
                                                    U T::*p) const {
    return Field(name, true, p);
  }

  template <typename U>
  JsonObjectLoader<T, kElemCount + 1> Field(const char* name, bool optional,
                                            U T::*p) const {
    return JsonObjectLoader<T, kElemCount + 1>(
        elements_, Element(name, optional, p, LoaderForType<U>()));
  }

  JsonObjectLoader(const Vec<Element, kElemCount - 1>& elements,
                   Element new_element)
      : elements_(elements, new_element) {}

 private:
  GPR_NO_UNIQUE_ADDRESS Vec<Element, kElemCount> elements_;
};

}  // namespace json_detail

template <typename T>
using JsonObjectLoader = json_detail::JsonObjectLoader<T>;

using JsonLoaderInterface = json_detail::LoaderInterface;

template <typename T>
T LoadFromJson(const Json& json, ErrorList* error_list) {
  T result;
  json_detail::LoaderForType<T>()->LoadInto(json, &result, error_list);
  return result;
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_JSON_JSON_OBJECT_LOADER_H
