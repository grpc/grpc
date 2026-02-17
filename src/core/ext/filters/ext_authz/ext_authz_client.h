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
#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "envoy/service/auth/v3/attribute_context.upb.h"
#include "ext_authz_service_config_parser.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "upb/reflection/def.hpp"

namespace grpc_core {

class ExtAuthzClient : public DualRefCounted<ExtAuthzClient> {
 public:
  ExtAuthzClient(
      std::shared_ptr<XdsBootstrap> bootstrap,
      RefCountedPtr<XdsTransportFactory> transport_factory,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine);
  ~ExtAuthzClient() override;

  // Resets connection backoff state.
  void ResetBackoff();

  XdsTransportFactory* transport_factory() const {
    return transport_factory_.get();
  }

  grpc_event_engine::experimental::EventEngine* engine() {
    return engine_.get();
  }

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

 private:
  class ExtAuthzChannel final : public DualRefCounted<ExtAuthzChannel> {
   public:
    template <typename T>
    class RetryableCall;

    class ExtAuthzCall;

    ExtAuthzChannel(
        WeakRefCountedPtr<ExtAuthzClient> ext_authz_client_,
        std::shared_ptr<const XdsBootstrap::XdsServerTarget> server);
    ~ExtAuthzChannel() override;

    ExtAuthzClient* ext_authz_client() const { return ext_authz_client_.get(); }
    absl::string_view server_uri() const { return server_->server_uri(); }
    void ResetBackoff();

   private:
    void Orphaned() override;

    void StopExtAuthzCallLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ExtAuthzClient::mu_);

    // The owning ExtAuthzClient.
    WeakRefCountedPtr<ExtAuthzClient> ext_authz_client_;

    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server_;

    RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;

    // The retryable ExtAuthz call.
    OrphanablePtr<RetryableCall<ExtAuthzCall>> ext_authz_call_;
  };

  std::string CreateExtAuthzRequest(
      bool is_client_call,
      std::vector<std::pair<std::string, std::string>> headers)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  absl::StatusOr<ExtAuthzResponse> ParseExtAuthzResponse(
      absl::string_view encoded_response) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  void RemoveExtAuthzChannel(const std::string& key)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);
  RefCountedPtr<ExtAuthzChannel> GetOrCreateExtAuthzChannelLocked(
      std::shared_ptr<const XdsBootstrap::XdsServerTarget> server,
      const char* reason) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_;
  std::shared_ptr<XdsBootstrap> bootstrap_;  // not required
  RefCountedPtr<XdsTransportFactory> transport_factory_;
  Mutex mu_;
  upb::DefPool def_pool_ ABSL_GUARDED_BY(mu_);
  std::map<std::string /*XdsServer key*/, ExtAuthzChannel*>
      ext_authz_channel_map_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_CLIENT_H
