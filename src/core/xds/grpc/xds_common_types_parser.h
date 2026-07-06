//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_PARSER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_PARSER_H

#include <cstdint>
#include <optional>

#include "envoy/config/common/mutation_rules/v3/mutation_rules.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/grpc_service.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "xds/type/matcher/v3/string.upb.h"

namespace grpc_core {

Duration ParseDuration(const google_protobuf_Duration* proto_duration,
                       ValidationErrors* errors);

inline bool ParseBoolValue(const google_protobuf_BoolValue* bool_value_proto,
                           bool default_value = false) {
  if (bool_value_proto == nullptr) return default_value;
  return google_protobuf_BoolValue_value(bool_value_proto);
}

inline std::optional<uint64_t> ParseUInt64Value(
    const google_protobuf_UInt64Value* proto) {
  if (proto == nullptr) return std::nullopt;
  return google_protobuf_UInt64Value_value(proto);
}

inline std::optional<uint32_t> ParseUInt32Value(
    const google_protobuf_UInt32Value* proto) {
  if (proto == nullptr) return std::nullopt;
  return google_protobuf_UInt32Value_value(proto);
}

// Returns the number per million.
uint32_t ParseFractionalPercent(
    const envoy_type_v3_FractionalPercent* fractional_percent);

std::optional<grpc_resolved_address> ParseXdsAddress(
    const envoy_config_core_v3_Address* address, ValidationErrors* errors);

StringMatcher StringMatcherParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_type_matcher_v3_StringMatcher* matcher_proto,
    ValidationErrors* errors);
StringMatcher StringMatcherParse(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_StringMatcher* matcher_proto,
    ValidationErrors* errors);

CommonTlsContext CommonTlsContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    ValidationErrors* errors);

absl::StatusOr<Json> ParseProtobufStructToJson(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Struct* resource);

std::optional<XdsExtension> ExtractXdsExtension(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Any* any, ValidationErrors* errors);

GrpcXdsServerTarget ParseXdsGrpcService(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_GrpcService* grpc_service,
    ValidationErrors* errors);

HeaderMutationRules ParseHeaderMutationRules(
    const envoy_config_common_mutation_rules_v3_HeaderMutationRules*
        header_mutation_rules,
    ValidationErrors* errors);

XdsHeaderValueOption ParseXdsHeaderValueOption(
    const envoy_config_core_v3_HeaderValueOption* header_value_option_config,
    ValidationErrors* errors);

// TODO(roth): We would ideally like to use a Slice for the header value, but
// Slice isn't copyable, which makes it problematic to store here. The down-side
// of this approach is that we need to make a copy when we apply the header
// value to each RPC rather than just taking a new ref to the slice. Consider
// solving this problem by adding a new RefCountedSlice class to represent an
// immutable, ref-counted slice that can be turned into a Slice and thus added
// to RPC metadata without a copy.
std::pair<std::string, std::string> ParseXdsHeader(
    const envoy_config_core_v3_HeaderValue* header_value,
    ValidationErrors* errors);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_PARSER_H
