//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_H

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cel-c/activation.h"
#include "cel-c/program.h"
#include "cel/expr/checked.upb.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// A compiled CEL expression (gRFC A106).
//
// Instances are immutable and may be evaluated concurrently from
// multiple threads.
class CelProgram {
 public:
  // The type that the expression must evaluate to.
  enum class ResultType { kBool, kString };

  // Validates and compiles a type-checked CEL expression.  Validation
  // enforces the restrictions in gRFC A106:
  // - the result type is result_type (or dyn, for kString);
  // - the only variable is `request`;
  // - no comprehensions (e.g. exists(), all(), cel.bind());
  // - no string() conversions and no string or list concatenation;
  // - matches() patterns are constant, valid RE2 regexes with a program
  //   size of at most 100.
  static absl::StatusOr<std::unique_ptr<CelProgram>> Create(
      const cel_expr_CheckedExpr* checked_expr, ResultType result_type);

  // Evaluates the expression, resolving variables via resolver.
  // Returns std::nullopt if evaluation fails or the result is not of the
  // requested type.
  std::optional<bool> EvaluateBool(const cel_VariableResolver& resolver) const;
  std::optional<std::string> EvaluateString(
      const cel_VariableResolver& resolver) const;

  bool operator==(const CelProgram& other) const {
    return serialized_expr_ == other.serialized_expr_;
  }
  bool operator!=(const CelProgram& other) const { return !(*this == other); }

  std::string ToString() const;

 private:
  struct ProgramDeleter {
    void operator()(cel_Program* program) const { cel_Program_Delete(program); }
  };
  using ProgramPtr = std::unique_ptr<cel_Program, ProgramDeleter>;

  CelProgram(std::string serialized_expr, ProgramPtr program)
      : serialized_expr_(std::move(serialized_expr)),
        program_(std::move(program)) {}

  // Deterministically serialized CheckedExpr, used for equality.
  std::string serialized_expr_;
  ProgramPtr program_;
};

// Extracts a string from a request using a CEL expression
// (xds.type.v3.CelExtractString in gRFC A106).
class CelStringExtractor {
 public:
  CelStringExtractor(std::unique_ptr<CelProgram> program,
                     std::optional<std::string> default_value)
      : program_(std::move(program)),
        default_value_(std::move(default_value)) {}

  // Returns the result of evaluating the expression.  If evaluation fails
  // or does not produce a string, returns the default value, if any.
  std::optional<std::string> Extract(
      const cel_VariableResolver& resolver) const {
    auto result = program_->EvaluateString(resolver);
    if (result.has_value()) return result;
    return default_value_;
  }

  bool operator==(const CelStringExtractor& other) const {
    return *program_ == *other.program_ &&
           default_value_ == other.default_value_;
  }
  bool operator!=(const CelStringExtractor& other) const {
    return !(*this == other);
  }

  std::string ToString() const;

 private:
  std::unique_ptr<CelProgram> program_;
  std::optional<std::string> default_value_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_H
