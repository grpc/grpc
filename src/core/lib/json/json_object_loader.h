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

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"

// Provides a means to load JSON objects into C++ objects, with the aim of
// minimizing object code size.
//
// Usage:
// Given struct Foo:
//   struct Foo {
//     int a;
//     int b;
//   };
// We add a member to Foo to declare how to load the object from JSON:
//   struct Foo {
//     int a;
//     int b;
//     static const JsonLoaderInterface* JsonLoader() {
//       static const auto loader = JsonObjectLoader<Foo>()
//           .Field("a", &Foo::a)
//           .Field("b", &Foo::b)
//           .Finish();
//       return &loader;
//     }
//   };
// Now we can load Foo objects from JSON:
//   ErrorList errors;
//   Foo foo = LoadFromJson<Foo>(json, &errors);
namespace grpc_core {

// A list of errors that occurred during JSON parsing.
// If a non-empty list occurs during parsing, the parsing failed.
class ErrorList {
 public:
  // Record that we're reading some field.
  void PushField(absl::string_view ext) GPR_ATTRIBUTE_NOINLINE;
  // Record that we've finished reading that field.
  void PopField() GPR_ATTRIBUTE_NOINLINE;
  // Record that we've encountered an error.
  void AddError(absl::string_view error) GPR_ATTRIBUTE_NOINLINE;

  // Return the list of errors.
  const std::vector<std::string>& errors() const { return errors_; }

  // Return true if there are no errors.
  bool ok() const { return errors_.empty(); }

 private:
  std::vector<std::string> errors_;
  std::vector<std::string> fields_;
};

// Note that we're reading a field, and remove it at the end of the scope.
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

// An un-typed JSON loader.
class LoaderInterface {
 public:
  // Convert json value to whatever type we're loading at dst.
  // If errors occur, add them to error_list.
  virtual void LoadInto(const Json& json, void* dst,
                        ErrorList* errors) const = 0;

 protected:
  ~LoaderInterface() {}
};

// Loads a scalar (string or number).
class LoadScalar : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override;

 protected:
  ~LoadScalar() {}

 private:
  // true if we're loading a number, false if we're loading a string.
  // We use a virtual function to store this decision in a vtable instead of
  // needing an instance variable.
  virtual bool IsNumber() const = 0;

  virtual void LoadInto(const std::string& json, void* dst,
                        ErrorList* errors) const = 0;
};

// Load a number.
class LoadNumber : public LoadScalar {
 private:
  bool IsNumber() const override;
};

// Load a duration
class LoadDuration : public LoadScalar {
 private:
  bool IsNumber() const override;
  void LoadInto(const std::string& json, void* dst,
                ErrorList* errors) const override;
};

// Load a number of type T.
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

// Load a string.
class LoadString : public LoadScalar {
 private:
  bool IsNumber() const override;
  void LoadInto(const std::string& value, void* dst,
                ErrorList* errors) const override;
};

// Load a vector of some type.
class LoadVector : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override;

 private:
  virtual void LoadOne(const Json& json, void* dst,
                       ErrorList* errors) const = 0;
};

// Load a map of string->some type.
class LoadMap : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override;

 private:
  virtual void LoadOne(const Json& json, const std::string& name, void* dst,
                       ErrorList* errors) const = 0;
};

// Fetch a LoaderInterface for some type.
template <typename T>
const LoaderInterface* LoaderForType();

// AutoLoader implements LoaderInterface for a type.
// The default asks the type for its LoaderInterface and then uses that.
// Classes that load from objects should provide a:
// static const JsonLoaderInterface* JsonLoader();
template <typename T>
class AutoLoader final : public LoaderInterface {
 public:
  void LoadInto(const Json& json, void* dst, ErrorList* errors) const override {
    T::JsonLoader()->LoadInto(json, dst, errors);
  }
};

// Specializations of AutoLoader for basic types.
template <>
class AutoLoader<int32_t> final : public TypedLoadNumber<int32_t> {};
template <>
class AutoLoader<uint32_t> final : public TypedLoadNumber<uint32_t> {};
template <>
class AutoLoader<int64_t> final : public TypedLoadNumber<int64_t> {};
template <>
class AutoLoader<uint64_t> final : public TypedLoadNumber<uint64_t> {};
template <>
class AutoLoader<Duration> final : public LoadDuration {};
template <>
class AutoLoader<std::string> final : public LoadString {};

// Specializations of AutoLoader for vectors.
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

// Specializations of AutoLoader for maps.
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

// Implementation of aforementioned LoaderForType.
// Simply keeps a static AutoLoader<T> and returns a pointer to that.
template <typename T>
const LoaderInterface* LoaderForType() {
  static const AutoLoader<T> loader;
  return &loader;
}

// Element describes one typed field to be loaded from a JSON object.
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
  // The loader for this field.
  const LoaderInterface* loader;
  // Offset into the destination object to store the field.
  uint16_t member_offset;
  // Is this field optional?
  bool optional;
  // The name of the field.
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

// Given a list of elements, and a destination object, load the elements into
// the object from some parsed JSON.
void LoadObject(const Json& json, const Element* elements, size_t num_elements,
                void* dst, ErrorList* errors);

// Adaptor type - takes a compile time computed list of elements and implements
// LoaderInterface by calling LoadObject.
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

// Builder type for JSON object loaders.
// Concatenate fields with Field, OptionalField, and then call Finish to obtain
// an object that implements LoaderInterface.
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

  JsonObjectLoader(const Vec<Element, kElemCount - 1>& elements,
                   Element new_element)
      : elements_(elements, new_element) {}

 private:
  template <typename U>
  JsonObjectLoader<T, kElemCount + 1> Field(const char* name, bool optional,
                                            U T::*p) const {
    return JsonObjectLoader<T, kElemCount + 1>(
        elements_, Element(name, optional, p, LoaderForType<U>()));
  }

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
