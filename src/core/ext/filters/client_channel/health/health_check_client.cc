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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/health/health_check_client.h"

#include <stdint.h>
#include <stdio.h>

#include "upb/upb.hpp"

#include <grpc/status.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/proto/grpc/health/v1/health.upb.h"

#define HEALTH_CHECK_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define HEALTH_CHECK_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define HEALTH_CHECK_RECONNECT_MAX_BACKOFF_SECONDS 120
#define HEALTH_CHECK_RECONNECT_JITTER 0.2

namespace grpc_core {

TraceFlag grpc_health_check_client_trace(false, "health_check_client");

namespace {

// Returns true if healthy.
absl::StatusOr<bool> DecodeResponse(char* message, size_t size) {
  // If message is empty, assume unhealthy.
  if (size == 0) {
    return absl::InvalidArgumentError("health check response was empty");
  }
  // Deserialize message.
  upb::Arena arena;
  auto* response_struct = grpc_health_v1_HealthCheckResponse_parse(
      reinterpret_cast<char*>(message), size, arena.ptr());
  if (response_struct == nullptr) {
    // Can't parse message; assume unhealthy.
    return absl::InvalidArgumentError("cannot parse health check response");
  }
  int32_t status = grpc_health_v1_HealthCheckResponse_status(response_struct);
  return status == grpc_health_v1_HealthCheckResponse_SERVING;
}

class HealthStreamEventHandler
    : public SubchannelStreamClient::CallEventHandler {
 public:
  HealthStreamEventHandler(
      std::string service_name,
      RefCountedPtr<channelz::SubchannelNode> channelz_node,
      RefCountedPtr<ConnectivityStateWatcherInterface> watcher)
      : service_name_(std::move(service_name)),
        channelz_node_(std::move(channelz_node)),
        watcher_(std::move(watcher)) {}

  Slice GetPathLocked() override {
    return Slice::FromStaticString("/grpc.health.v1.Health/Watch");
  }

  void OnCallStartLocked(SubchannelStreamClient* client) override {
    SetHealthStatusLocked(client, GRPC_CHANNEL_CONNECTING,
                          "starting health watch");
  }

  void OnRetryTimerStartLocked(SubchannelStreamClient* client) override {
    SetHealthStatusLocked(client, GRPC_CHANNEL_TRANSIENT_FAILURE,
                          "health check call failed; will retry after backoff");
  }

  grpc_slice EncodeSendMessageLocked() override {
    upb::Arena arena;
    grpc_health_v1_HealthCheckRequest* request_struct =
        grpc_health_v1_HealthCheckRequest_new(arena.ptr());
    grpc_health_v1_HealthCheckRequest_set_service(
        request_struct,
        upb_StringView_FromDataAndSize(service_name_.data(),
                                       service_name_.size()));
    size_t buf_length;
    char* buf = grpc_health_v1_HealthCheckRequest_serialize(
        request_struct, arena.ptr(), &buf_length);
    grpc_slice request_slice = GRPC_SLICE_MALLOC(buf_length);
    memcpy(GRPC_SLICE_START_PTR(request_slice), buf, buf_length);
    return request_slice;
  }

  void RecvMessageReadyLocked(SubchannelStreamClient* client, char* message,
                              size_t size) override {
    auto healthy = DecodeResponse(message, size);
    if (!healthy.ok()) {
      SetHealthStatusLocked(client, GRPC_CHANNEL_TRANSIENT_FAILURE,
                            healthy.status().ToString().c_str());
    } else if (!*healthy) {
      SetHealthStatusLocked(client, GRPC_CHANNEL_TRANSIENT_FAILURE,
                            "backend unhealthy");
    } else {
      SetHealthStatusLocked(client, GRPC_CHANNEL_READY, "OK");
    }
  }

  void RecvTrailingMetadataReadyLocked(SubchannelStreamClient* client,
                                       grpc_status_code status) override {
    if (status == GRPC_STATUS_UNIMPLEMENTED) {
      static const char kErrorMessage[] =
          "health checking Watch method returned UNIMPLEMENTED; "
          "disabling health checks but assuming server is healthy";
      gpr_log(GPR_ERROR, kErrorMessage);
      if (channelz_node_ != nullptr) {
        channelz_node_->AddTraceEvent(
            channelz::ChannelTrace::Error,
            grpc_slice_from_static_string(kErrorMessage));
      }
      SetHealthStatusLocked(client, GRPC_CHANNEL_READY, kErrorMessage);
    }
  }

 private:
  void SetHealthStatusLocked(SubchannelStreamClient* client,
                             grpc_connectivity_state state,
                             const char* reason) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
      gpr_log(GPR_INFO, "HealthCheckClient %p: setting state=%s reason=%s",
              client, ConnectivityStateName(state), reason);
    }
    watcher_->Notify(state,
                     state == GRPC_CHANNEL_TRANSIENT_FAILURE
                         ? absl::UnavailableError(reason)
                         : absl::Status());
  }

  std::string service_name_;
  RefCountedPtr<channelz::SubchannelNode> channelz_node_;
  RefCountedPtr<ConnectivityStateWatcherInterface> watcher_;
};

}  // namespace

OrphanablePtr<SubchannelStreamClient> MakeHealthCheckClient(
    std::string service_name,
    RefCountedPtr<ConnectedSubchannel> connected_subchannel,
    grpc_pollset_set* interested_parties,
    RefCountedPtr<channelz::SubchannelNode> channelz_node,
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  return MakeOrphanable<SubchannelStreamClient>(
      std::move(connected_subchannel), interested_parties,
      absl::make_unique<HealthStreamEventHandler>(
          std::move(service_name), std::move(channelz_node),
          std::move(watcher)),
      GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)
          ? "HealthCheckClient"
          : nullptr);
}

}  // namespace grpc_core
