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

#include "src/core/xds/grpc/xds_cel_parser.h"

#include <optional>
#include <string>
#include <utility>

#include "cel/expr/checked.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/util/upb_utils.h"

namespace grpc_core {

std::optional<CelProgram> ParseCelExpression(
    const xds_type_v3_CelExpression* cel_expression,
    CelProgram::ResultType result_type, ValidationErrors* errors) {
  if (cel_expression == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  if (xds_type_v3_CelExpression_cel_expr_string(cel_expression).size != 0) {
    ValidationErrors::ScopedField field(errors, ".cel_expr_string");
    errors->AddError("not supported");
    return std::nullopt;
  }
  ValidationErrors::ScopedField field(errors, ".cel_expr_checked");
  const cel_expr_CheckedExpr* checked_expr =
      xds_type_v3_CelExpression_cel_expr_checked(cel_expression);
  if (checked_expr == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  auto program = CelProgram::Create(checked_expr, result_type);
  if (!program.ok()) {
    errors->AddError(program.status().message());
    return std::nullopt;
  }
  return std::move(*program);
}

std::optional<CelStringExtractor> ParseCelExtractString(
    const xds_type_v3_CelExtractString* cel_extract_string,
    ValidationErrors* errors) {
  if (cel_extract_string == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  std::optional<CelProgram> program;
  {
    ValidationErrors::ScopedField field(errors, ".expr_extract");
    program = ParseCelExpression(
        xds_type_v3_CelExtractString_expr_extract(cel_extract_string),
        CelProgram::ResultType::kString, errors);
  }
  if (!program.has_value()) return std::nullopt;
  std::optional<std::string> default_value;
  if (const auto* default_value_proto =
          xds_type_v3_CelExtractString_default_value(cel_extract_string);
      default_value_proto != nullptr) {
    default_value = UpbStringToStdString(
        google_protobuf_StringValue_value(default_value_proto));
  }
  return CelStringExtractor(std::move(*program), std::move(default_value));
}

}  // namespace grpc_core
