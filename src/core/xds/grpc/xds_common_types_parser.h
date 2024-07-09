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

#include "absl/types/optional.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_resource_type.h"

namespace grpc_core {

Duration ParseDuration(const google_protobuf_Duration* proto_duration,
                       ValidationErrors* errors);

inline bool ParseBoolValue(const google_protobuf_BoolValue* bool_value_proto,
                           bool default_value = false) {
  if (bool_value_proto == nullptr) return default_value;
  return google_protobuf_BoolValue_value(bool_value_proto);
}

CommonTlsContext CommonTlsContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    ValidationErrors* errors);

absl::optional<XdsExtension> ExtractXdsExtension(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Any* any, ValidationErrors* errors);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_PARSER_H
