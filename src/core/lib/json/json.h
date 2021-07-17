/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_JSON_JSON_H
#define GRPC_CORE_LIB_JSON_JSON_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include "src/core/lib/iomgr/error.h"

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
  enum class Type {
    JSON_NULL,
    JSON_TRUE,
    JSON_FALSE,
    NUMBER,
    STRING,
    OBJECT,
    ARRAY
  };

  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;

  // Parses JSON string from json_str.  On error, sets *error.
  static Json Parse(absl::string_view json_str, grpc_error_handle* error);

  Json() = default;

  // Copyable.
  Json(const Json& other) { CopyFrom(other); }
  Json& operator=(const Json& other) {
    CopyFrom(other);
    return *this;
  }

  // Moveable.
  Json(Json&& other) noexcept { MoveFrom(std::move(other)); }
  Json& operator=(Json&& other) noexcept {
    MoveFrom(std::move(other));
    return *this;
  }

  // Construct from copying a string.
  // If is_number is true, the type will be NUMBER instead of STRING.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const std::string& string, bool is_number = false)
      : type_(is_number ? Type::NUMBER : Type::STRING), string_value_(string) {}
  Json& operator=(const std::string& string) {
    type_ = Type::STRING;
    string_value_ = string;
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
  Json(std::string&& string)
      : type_(Type::STRING), string_value_(std::move(string)) {}
  Json& operator=(std::string&& string) {
    type_ = Type::STRING;
    string_value_ = std::move(string);
    return *this;
  }

  // Construct from bool.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(bool b) : type_(b ? Type::JSON_TRUE : Type::JSON_FALSE) {}
  Json& operator=(bool b) {
    type_ = b ? Type::JSON_TRUE : Type::JSON_FALSE;
    return *this;
  }

  // Construct from any numeric type.
  template <typename NumericType>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(NumericType number)
      : type_(Type::NUMBER), string_value_(std::to_string(number)) {}
  template <typename NumericType>
  Json& operator=(NumericType number) {
    type_ = Type::NUMBER;
    string_value_ = std::to_string(number);
    return *this;
  }

  // Construct by copying object.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const Object& object) : type_(Type::OBJECT), object_value_(object) {}
  Json& operator=(const Object& object) {
    type_ = Type::OBJECT;
    object_value_ = object;
    return *this;
  }

  // Construct by moving object.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(Object&& object)
      : type_(Type::OBJECT), object_value_(std::move(object)) {}
  Json& operator=(Object&& object) {
    type_ = Type::OBJECT;
    object_value_ = std::move(object);
    return *this;
  }

  // Construct by copying array.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(const Array& array) : type_(Type::ARRAY), array_value_(array) {}
  Json& operator=(const Array& array) {
    type_ = Type::ARRAY;
    array_value_ = array;
    return *this;
  }

  // Construct by moving array.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Json(Array&& array) : type_(Type::ARRAY), array_value_(std::move(array)) {}
  Json& operator=(Array&& array) {
    type_ = Type::ARRAY;
    array_value_ = std::move(array);
    return *this;
  }

  // Dumps JSON from value to string form.
  std::string Dump(int indent = 0) const;

  // Accessor methods.
  Type type() const { return type_; }
  const std::string& string_value() const { return string_value_; }
  std::string* mutable_string_value() { return &string_value_; }
  const Object& object_value() const { return object_value_; }
  Object* mutable_object() { return &object_value_; }
  const Array& array_value() const { return array_value_; }
  Array* mutable_array() { return &array_value_; }

  bool operator==(const Json& other) const {
    if (type_ != other.type_) return false;
    switch (type_) {
      case Type::NUMBER:
      case Type::STRING:
        if (string_value_ != other.string_value_) return false;
        break;
      case Type::OBJECT:
        if (object_value_ != other.object_value_) return false;
        break;
      case Type::ARRAY:
        if (array_value_ != other.array_value_) return false;
        break;
      default:
        break;
    }
    return true;
  }

  bool operator!=(const Json& other) const { return !(*this == other); }

 private:
  void CopyFrom(const Json& other) {
    type_ = other.type_;
    switch (type_) {
      case Type::NUMBER:
      case Type::STRING:
        string_value_ = other.string_value_;
        break;
      case Type::OBJECT:
        object_value_ = other.object_value_;
        break;
      case Type::ARRAY:
        array_value_ = other.array_value_;
        break;
      default:
        break;
    }
  }

  void MoveFrom(Json&& other) {
    type_ = other.type_;
    other.type_ = Type::JSON_NULL;
    switch (type_) {
      case Type::NUMBER:
      case Type::STRING:
        string_value_ = std::move(other.string_value_);
        break;
      case Type::OBJECT:
        object_value_ = std::move(other.object_value_);
        break;
      case Type::ARRAY:
        array_value_ = std::move(other.array_value_);
        break;
      default:
        break;
    }
  }

  Type type_ = Type::JSON_NULL;
  std::string string_value_;
  Object object_value_;
  Array array_value_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_JSON_JSON_H */
