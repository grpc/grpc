//
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
//

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_ECDS_PARSER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_ECDS_PARSER_H

#include "absl/strings/string_view.h"
#include "src/core/xds/grpc/xds_ecds.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_resource_type_impl.h"

namespace grpc_core {

class XdsEcdsResourceType final
    : public XdsResourceTypeImpl<XdsEcdsResourceType, XdsEcdsResource> {
 public:
  absl::string_view type_url() const override {
    return "envoy.config.core.v3.TypedExtensionConfig";
  }

  DecodeResult Decode(const XdsResourceType::DecodeContext& context,
                      absl::string_view serialized_resource) const override;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_ECDS_PARSER_H
