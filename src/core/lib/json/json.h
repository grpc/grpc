//
// Copyright 2015 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_LIB_JSON_JSON_H
#define GRPC_SRC_CORE_LIB_JSON_JSON_H

#include <grpc/support/port_platform.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/variant.h"

namespace grpc_core {

// A JSON value, which can be any one of object, array, string,
// number, true, false, or null.
class Json {
 public:
  // TODO(roth): Currently, numbers are stored internally as strings,
  // which makes the API a bit cumbersome to use. When we have time,
  // consider whether there's a better alternative (e.g., maybe storing
  // each numeric type as the native C++ type and automatically converting
  // to string as needed).
  enum class Type { kNull, kTrue, kFalse, kNumber, kString, kObject, kArray };

  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;

  Json() = default;

  // Copyable.
  Json(const Json& other) = default;
  Json& operator=(const Json& other) = default;

  // Moveable.
  Json(Json&& other) noexcept : value_(std::move(other.value_)) {
    other.value_ = absl::monostate();
  }
  Json& operator=(Json&& other) noexcept {
    value_ = std::move(other.value_);
    other.value_ = absl::monostate();
    return *this;
  }

  // Construct from copying a string.
  // If is_number is true, the type will be kNumber instead of kString.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const std::string& string, bool is_number = false)
      : value_(is_number ? Value(NumberValue{string}) : Value(string)) {}
  Json& operator=(const std::string& string) {
    value_ = string;
    return *this;
  }

  // Same thing for C-style strings, both const and mutable.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const char* string, bool is_number = false)
      : Json(std::string(string), is_number) {}
  Json& operator=(const char* string) {
    *this = std::string(string);
    return *this;
  }
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(char* string, bool is_number = false)
      : Json(std::string(string), is_number) {}
  Json& operator=(char* string) {
    *this = std::string(string);
    return *this;
  }

  // Construct by moving a string.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(std::string&& string) : value_(Value(std::move(string))) {}
  Json& operator=(std::string&& string) {
    value_ = Value(std::move(string));
    return *this;
  }

  // Construct from bool.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(bool b) : value_(b) {}
  Json& operator=(bool b) {
    value_ = b;
    return *this;
  }

  // Construct from any numeric type.
  template <typename NumericType>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(NumericType number) : value_(NumberValue{std::to_string(number)}) {}
  template <typename NumericType>
  Json& operator=(NumericType number) {
    value_ = NumberValue{std::to_string(number)};
    return *this;
  }

  // Construct by copying object.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const Object& object) : value_(object) {}
  Json& operator=(const Object& object) {
    value_ = object;
    return *this;
  }

  // Construct by moving object.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(Object&& object) : value_(std::move(object)) {}
  Json& operator=(Object&& object) {
    value_ = std::move(object);
    return *this;
  }

  // Construct by copying array.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const Array& array) : value_(array) {}
  Json& operator=(const Array& array) {
    value_ = array;
    return *this;
  }

  // Construct by moving array.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(Array&& array) : value_(std::move(array)) {}
  Json& operator=(Array&& array) {
    value_ = std::move(array);
    return *this;
  }

  // Returns the JSON type.
  Type type() const {
    struct ValueFunctor {
      Json::Type operator()(const absl::monostate&) { return Type::kNull; }
      Json::Type operator()(bool value) {
        return value ? Type::kTrue : Type::kFalse;
      }
      Json::Type operator()(const NumberValue&) { return Type::kNumber; }
      Json::Type operator()(const std::string&) { return Type::kString; }
      Json::Type operator()(const Object&) { return Type::kObject; }
      Json::Type operator()(const Array&) { return Type::kArray; }
    };
    return absl::visit(ValueFunctor(), value_);
  }

  // Accessor methods.
  const std::string& string() const {
    const NumberValue* num = absl::get_if<NumberValue>(&value_);
    if (num != nullptr) return num->value;
    return absl::get<std::string>(value_);
  }
  const Object& object() const { return absl::get<Object>(value_); }
  const Array& array() const { return absl::get<Array>(value_); }

  bool operator==(const Json& other) const { return value_ == other.value_; }
  bool operator!=(const Json& other) const { return !(*this == other); }

 private:
  struct NumberValue {
    std::string value;

    bool operator==(const NumberValue& other) const {
      return value == other.value;
    }
  };
  using Value = absl::variant<absl::monostate,  // kNull
                              bool,             // kTrue or kFalse
                              NumberValue,      // kNumber
                              std::string,      // kString
                              Object,           // kObject
                              Array>;           // kArray

  explicit Json(Value value) : value_(std::move(value)) {}

  Value value_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_JSON_JSON_H
