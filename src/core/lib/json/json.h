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

#include "src/core/lib/gprpp/string_view.h"
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
  static Json Parse(StringView json_str, grpc_error** error);

  Json() = default;

  // Copyable.
  Json(const Json& other) { CopyFrom(other); }
  Json& operator=(const Json& other) {
    CopyFrom(other);
    return *this;
  }

  // Moveable.
  Json(Json&& other) { MoveFrom(std::move(other)); }
  Json& operator=(Json&& other) {
    MoveFrom(std::move(other));
    return *this;
  }

  // Construct from copying a string.
  // If is_number is true, the type will be NUMBER instead of STRING.
  Json(const std::string& string, bool is_number = false)
      : type_(is_number ? Type::NUMBER : Type::STRING), string_value_(string) {}
  Json& operator=(const std::string& string) {
    type_ = Type::STRING;
    string_value_ = string;
    return *this;
  }

  // Same thing for C-style strings, both const and mutable.
  Json(const char* string, bool is_number = false)
      : Json(std::string(string), is_number) {}
  Json& operator=(const char* string) {
    *this = std::string(string);
    return *this;
  }
  Json(char* string, bool is_number = false)
      : Json(std::string(string), is_number) {}
  Json& operator=(char* string) {
    *this = std::string(string);
    return *this;
  }

  // Construct by moving a string.
  Json(std::string&& string)
      : type_(Type::STRING), string_value_(std::move(string)) {}
  Json& operator=(std::string&& string) {
    type_ = Type::STRING;
    string_value_ = std::move(string);
    return *this;
  }

  // Construct from bool.
  Json(bool b) : type_(b ? Type::JSON_TRUE : Type::JSON_FALSE) {}
  Json& operator=(bool b) {
    type_ = b ? Type::JSON_TRUE : Type::JSON_FALSE;
    return *this;
  }

  // Construct from any numeric type.
  template <typename NumericType>
  Json(NumericType number)
      : type_(Type::NUMBER), string_value_(std::to_string(number)) {}
  template <typename NumericType>
  Json& operator=(NumericType number) {
    type_ = Type::NUMBER;
    string_value_ = std::to_string(number);
    return *this;
  }

  // Construct by copying object.
  Json(const Object& object) : type_(Type::OBJECT), object_value_(object) {}
  Json& operator=(const Object& object) {
    type_ = Type::OBJECT;
    object_value_ = object;
    return *this;
  }

  // Construct by moving object.
  Json(Object&& object)
      : type_(Type::OBJECT), object_value_(std::move(object)) {}
  Json& operator=(Object&& object) {
    type_ = Type::OBJECT;
    object_value_ = std::move(object);
    return *this;
  }

  // Construct by copying array.
  Json(const Array& array) : type_(Type::ARRAY), array_value_(array) {}
  Json& operator=(const Array& array) {
    type_ = Type::ARRAY;
    array_value_ = array;
    return *this;
  }

  // Construct by moving array.
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

/* The various json types. */
typedef enum {
  GRPC_JSON_OBJECT,
  GRPC_JSON_ARRAY,
  GRPC_JSON_STRING,
  GRPC_JSON_NUMBER,
  GRPC_JSON_TRUE,
  GRPC_JSON_FALSE,
  GRPC_JSON_NULL,
  GRPC_JSON_TOP_LEVEL
} grpc_json_type;

/* A tree-like structure to hold json values. The key and value pointers
 * are not owned by it.
 */
typedef struct grpc_json {
  struct grpc_json* next;
  struct grpc_json* prev;
  struct grpc_json* child;
  struct grpc_json* parent;

  grpc_json_type type;
  const char* key;
  const char* value;

  /* if set, destructor will free value */
  bool owns_value;
} grpc_json;

/* The next two functions are going to parse the input string, and
 * modify it in the process, in order to use its space to store
 * all of the keys and values for the returned object tree.
 *
 * They assume UTF-8 input stream, and will output UTF-8 encoded
 * strings in the tree. The input stream's UTF-8 isn't validated,
 * as in, what you input is what you get as an output.
 *
 * All the keys and values in the grpc_json objects will be strings
 * pointing at your input buffer.
 *
 * Delete the allocated tree afterward using grpc_json_destroy().
 */
grpc_json* grpc_json_parse_string_with_len(char* input, size_t size);
grpc_json* grpc_json_parse_string(char* input);

/* This function will create a new string using gpr_realloc, and will
 * deserialize the grpc_json tree into it. It'll be zero-terminated,
 * but will be allocated in chunks of 256 bytes.
 *
 * The indent parameter controls the way the output is formatted.
 * If indent is 0, then newlines will be suppressed as well, and the
 * output will be condensed at its maximum.
 */
char* grpc_json_dump_to_string(const grpc_json* json, int indent);

/* Use these to create or delete a grpc_json object.
 * Deletion is recursive. We will not attempt to free any of the strings
 * in any of the objects of that tree, unless the boolean, owns_value,
 * is true.
 */
grpc_json* grpc_json_create(grpc_json_type type);
void grpc_json_destroy(grpc_json* json);

/* Links the child json object into the parent's json tree. If the parent
 * already has children, then passing in the most recently added child as the
 * sibling parameter is an optimization. For if sibling is NULL, this function
 * will manually traverse the tree in order to find the right most sibling.
 */
grpc_json* grpc_json_link_child(grpc_json* parent, grpc_json* child,
                                grpc_json* sibling);

/* Creates a child json object into the parent's json tree then links it in
 * as described above. */
grpc_json* grpc_json_create_child(grpc_json* sibling, grpc_json* parent,
                                  const char* key, const char* value,
                                  grpc_json_type type, bool owns_value);

/* Creates a child json string object from the integer num, then links the
   json object into the parent's json tree */
grpc_json* grpc_json_add_number_string_child(grpc_json* parent, grpc_json* it,
                                             const char* name, int64_t num);

#endif /* GRPC_CORE_LIB_JSON_JSON_H */
