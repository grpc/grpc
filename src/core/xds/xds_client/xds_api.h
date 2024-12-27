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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_API_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_API_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "envoy/admin/v3/config_dump_shared.upb.h"
#include "envoy/service/status/v3/csds.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_locality.h"
#include "upb/mem/arena.h"
#include "upb/reflection/def.hpp"

namespace grpc_core {

class XdsClient;

// TODO(roth): When we have time, remove this class and move its
// functionality directly inside of XdsClient.
class XdsApi final {
 public:
  // Interface defined by caller and passed to ParseAdsResponse().
  class AdsResponseParserInterface {
   public:
    struct AdsResponseFields {
      std::string type_url;
      std::string version;
      std::string nonce;
      size_t num_resources;
    };

    virtual ~AdsResponseParserInterface() = default;

    // Called when the top-level ADS fields are parsed.
    // If this returns non-OK, parsing will stop, and the individual
    // resources will not be processed.
    virtual absl::Status ProcessAdsResponseFields(AdsResponseFields fields) = 0;

    // Called to parse each individual resource in the ADS response.
    // Note that resource_name is non-empty only when the resource was
    // wrapped in a Resource wrapper proto.
    virtual void ParseResource(upb_Arena* arena, size_t idx,
                               absl::string_view type_url,
                               absl::string_view resource_name,
                               absl::string_view serialized_resource) = 0;

    // Called when a resource is wrapped in a Resource wrapper proto but
    // we fail to parse the Resource wrapper.
    virtual void ResourceWrapperParsingFailed(size_t idx,
                                              absl::string_view message) = 0;
  };

  XdsApi(XdsClient* client, TraceFlag* tracer, const XdsBootstrap::Node* node,
         upb::DefPool* def_pool, std::string user_agent_name,
         std::string user_agent_version);

  // Returns non-OK when failing to deserialize response message.
  // Otherwise, all events are reported to the parser.
  absl::Status ParseAdsResponse(absl::string_view encoded_response,
                                AdsResponseParserInterface* parser);

 private:
  XdsClient* client_;
  TraceFlag* tracer_;
  const XdsBootstrap::Node* node_;  // Do not own.
  upb::DefPool* def_pool_;          // Do not own.
  const std::string user_agent_name_;
  const std::string user_agent_version_;
};

void PopulateXdsNode(const XdsBootstrap::Node* node,
                     absl::string_view user_agent_name,
                     absl::string_view user_agent_version,
                     envoy_config_core_v3_Node* node_msg, upb_Arena* arena);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_API_H
