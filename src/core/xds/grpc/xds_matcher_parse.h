// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_PARSE_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_PARSE_H

#include <memory>

#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/type/matcher/v3/matcher.upb.h"

namespace grpc_core {

// Parses the xDS Matcher proto into an `XdsMatcher` object.
// This is the top-level function expected to be called for parsing the
// matcher.proto.
std::unique_ptr<XdsMatcher> ParseMatcher(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher, ValidationErrors* errors);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_PARSE_H