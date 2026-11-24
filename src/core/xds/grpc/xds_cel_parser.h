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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_PARSER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_PARSER_H

#include <optional>

#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_cel.h"
#include "xds/type/v3/cel.upb.h"

namespace grpc_core {

// Parses an xds.type.v3.CelExpression.  Only cel_expr_checked is
// supported.  Returns std::nullopt and adds to errors on failure.
std::optional<CelProgram> ParseCelExpression(
    const xds_type_v3_CelExpression* cel_expression,
    CelProgram::ResultType result_type, ValidationErrors* errors);

// Parses an xds.type.v3.CelExtractString.  Returns std::nullopt and adds to
// errors on failure.
std::optional<CelStringExtractor> ParseCelExtractString(
    const xds_type_v3_CelExtractString* cel_extract_string,
    ValidationErrors* errors);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_CEL_PARSER_H
