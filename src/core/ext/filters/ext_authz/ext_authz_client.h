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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_CLIENT_H
#define GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_CLIENT_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/sync.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "upb/reflection/def.hpp"
namespace grpc_core {

class ExtAuthzClient : public DualRefCounted<ExtAuthzClient> {
 public:
  ExtAuthzClient(RefCountedPtr<XdsTransportFactory> transport_factory,
                 std::unique_ptr<const XdsBootstrap::XdsServerTarget> server);
  ~ExtAuthzClient() override;

  // Resets connection backoff state.
  void ResetBackoff();

  std::string server_uri() const;

  struct ExtAuthzResponse {
    struct OkResponse {
      std::vector<HeaderValueOption> headers;
      std::vector<std::string> headers_to_remove;
      std::vector<HeaderValueOption> response_headers_to_add;
    };

    struct DeniedResponse {
      grpc_status_code status;
      std::vector<HeaderValueOption> headers;
    };

    grpc_status_code status_code;
    OkResponse ok_response;
    DeniedResponse denied_response;
  };

  struct ExtAuthzRequestParams {
    bool is_client_call;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string path;
  };

  absl::StatusOr<ExtAuthzResponse> Check(const ExtAuthzRequestParams& params);

  std::string CreateExtAuthzRequest(const ExtAuthzRequestParams& params);

  absl::StatusOr<ExtAuthzResponse> ParseExtAuthzResponse(
      absl::string_view encoded_response);

  XdsTransportFactory* transport_factory() const {
    return transport_factory_.get();
  }

 private:
  void Orphaned() override;

  RefCountedPtr<XdsTransportFactory> transport_factory_;
  std::unique_ptr<const XdsBootstrap::XdsServerTarget> server_;
  RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;

  Mutex mu_;
  upb::DefPool def_pool_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_CLIENT_H