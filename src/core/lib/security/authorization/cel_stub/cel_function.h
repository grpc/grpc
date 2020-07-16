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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_STUB_CEL_FUNCTION_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_STUB_CEL_FUNCTION_H

#include <grpc/support/port_platform.h>
#include <vector>

#include "absl/types/span.h"
#include "src/core/lib/security/authorization/cel_stub/cel_value.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

// Type that describes CelFunction.
// This complex structure is needed for overloads support.
class CelFunctionDescriptor {
 public:
  CelFunctionDescriptor(const std::string& name, bool receiver_style,
                        const std::vector<CelValue::Type> types)
      : name_(name), receiver_style_(receiver_style), types_(types) {}

  // Function name.
  const std::string& name() const { return name_; }

  // Whether function is receiver style i.e. true means arg0.name(args[1:]...).
  bool receiver_style() const { return receiver_style_; }

  // The argmument types the function accepts.
  const std::vector<CelValue::Type>& types() const { return types_; }

  // Helper for matching a descriptor. This tests that the shape is the same --
  // |other| accepts the same number and types of arguments and is the same call
  // style).
  bool ShapeMatches(const CelFunctionDescriptor& other) const { return true; }
  bool ShapeMatches(bool receiver_style,
                    const std::vector<CelValue::Type>& types) const {
    return true;
  }

 private:
  std::string name_;
  bool receiver_style_;
  std::vector<CelValue::Type> types_;
};

// CelFunction is a handler that represents single
// CEL function.
// CelFunction provides Evaluate() method, that performs
//   evaluation of the function. CelFunction instances provide
// descriptors that contain function information:
// - name
// - is function receiver style (e.f(g) vs f(e,g))
// - amount of arguments and their types.
// Function overloads are resolved based on their arguments and
// receiver style.
class CelFunction {
 public:
  explicit CelFunction(const CelFunctionDescriptor& descriptor)
      : descriptor_(descriptor) {}

  // Non-copyable
  CelFunction(const CelFunction& other) = delete;
  CelFunction& operator=(const CelFunction& other) = delete;

  virtual ~CelFunction() {}

  virtual absl::Status Evaluate(absl::Span<const CelValue> arguments,
                                CelValue* result,
                                google::protobuf::Arena* arena) const = 0;

  // Determines whether instance of CelFunction is applicable to arguments
  // supplied. This implementation is a stub that always returns false.
  bool MatchArguments(absl::Span<const CelValue> arguments) const {
    return false;
  }

  const CelFunctionDescriptor& descriptor() const { return descriptor_; }

 private:
  CelFunctionDescriptor descriptor_;
};

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_STUB_CEL_FUNCTION_H