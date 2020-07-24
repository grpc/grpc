/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_CEL_VALUE_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_CEL_VALUE_H

// CelValue is a holder, capable of storing all kinds of data
// supported by CEL.
// CelValue defines explicitly typed/named getters/setters.
// When storing pointers to objects, CelValue does not accept ownership
// to them and does not control their lifecycle. Instead objects are expected
// to be either external to expression evaluation, and controlled beyond the
// scope or to be allocated and associated with some allocation/ownership
// controller (Arena).
// Usage examples:
// (a) For primitive types:
//    CelValue value = CelValue::CreateInt64(1);
// (b) For string:
//    string* msg = google::protobuf::Arena::Create<string>(arena,"test");
//    CelValue value = CelValue::CreateString(msg);
// (c) For messages:
//    const MyMessage * msg =
//    google::protobuf::Arena::CreateMessage<MyMessage>(arena); CelValue value =
//    CelValue::CreateMessage(msg, &arena);

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <google/protobuf/message.h>

#include "src/core/lib/security/authorization/mock_cel_library/cel_value_internal.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

using CelError = absl::Status;

// Break cyclic depdendencies for container types.
class CelList;
class CelMap;
class UnknownSet;

class CelValue {
 public:
  // Default constructor.
  // Creates CelValue with null data type.
  CelValue() : CelValue(nullptr) {}

  // We will use factory methods instead of public constructors
  // The reason for this is the high risk of implicit type conversions
  // between bool/int/pointer types.
  // We rely on copy elision to avoid extra copying.
  static CelValue CreateNull() { return CelValue(nullptr); }

  static CelValue CreateBool(bool value) { return CreateNull(); }

  static CelValue CreateInt64(int64_t value) { return CreateNull(); }

  static CelValue CreateUint64(uint64_t value) { return CreateNull(); }

  static CelValue CreateDouble(double value) { return CreateNull(); }

  static CelValue CreateString(StringHolder holder) { return CreateNull(); }

  static CelValue CreateStringView(absl::string_view value) {
    return CreateNull();
  }

  static CelValue CreateString(const std::string* str) { return CreateNull(); }

  static CelValue CreateBytes(BytesHolder holder) { return CreateNull(); }

  static CelValue CreateBytesView(absl::string_view value) {
    return CreateNull();
  }

  static CelValue CreateBytes(const std::string* str) { return CreateNull(); }

  // CreateMessage creates CelValue from google::protobuf::Message.
  // As some of CEL basic types are subclassing google::protobuf::Message,
  // this method contains type checking and downcasts.
  static CelValue CreateMessage(const google::protobuf::Message* value,
                                google::protobuf::Arena* arena) {
    return CreateNull();
  }

  static CelValue CreateList(const CelList* value) { return CreateNull(); }

  static CelValue CreateMap(const CelMap* value) { return CreateNull(); }

  static CelValue CreateUnknownSet(const UnknownSet* value) {
    return CreateNull();
  }

  static CelValue CreateError(const CelError* value) { return CreateNull(); }

 private:
  // Constructs CelValue wrapping value supplied as argument.
  // Value type T should be supported by specification of ValueHolder.
  template <class T>
  explicit CelValue(T value) {}
};

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_CEL_VALUE_H