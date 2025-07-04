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

#include "src/core/xds/grpc/xds_matcher_input.h"

#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/type/matcher/v3/http_inputs.upb.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "xds/core/v3/extension.upb.h"
#include "xds/type/matcher/v3/http_inputs.upb.h"

namespace grpc_core {
std::optional<absl::string_view> MetadataInput::GetValue(
    const XdsMatcher::MatchContext& context) const {
  CHECK_EQ(context.type(), RpcMatchContext::Type());
  return DownCast<const RpcMatchContext&>(context).GetHeaderValue(key_,
                                                                  &buffer_);
}

std::unique_ptr<XdsMatcher::InputValue<absl::string_view>>
MetadataInputFactory::ParseAndCreateInput(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_value, ValidationErrors* errors) const {
  auto http_header_input =
      envoy_type_matcher_v3_HttpRequestHeaderMatchInput_parse(
          serialized_value.data(), serialized_value.size(), context.arena);
  // extract header name (Key for metadata match)
  auto x = envoy_type_matcher_v3_HttpRequestHeaderMatchInput_header_name(
      http_header_input);
  auto header_name = UpbStringToStdString(x);
  return std::make_unique<MetadataInput>(header_name);
}
template <>
XdsMatcherInputRegistry<absl::string_view>::XdsMatcherInputRegistry() {
  // Add factories
  factories_.emplace(MetadataInputFactory::Type(),
                     std::make_unique<MetadataInputFactory>());
}

}  // namespace grpc_core
