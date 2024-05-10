// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_LB_CALL_TRACING_FILTER_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_LB_CALL_TRACING_FILTER_H

namespace grpc_core {

// A filter to handle updating with the call tracer and LB subchannel
// call tracker inside the LB call.
// FIXME: register only when call v3 experiment is enabled
class LbCallTracingFilter {
 public:
  static absl::StatusOr<LbCallTracingFilter> Create(const ChannelArgs&,
                                                    ChannelFilter::Args) {
    return LbCallTracingFilter();
  }

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& metadata);
    void OnServerInitialMetadata(ServerMetadata& metadata);

    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;

    void OnClientToServerHalfClose();
    void OnServerTrailingMetadata(ServerMetadata& metadata);
    void OnFinalize(const grpc_call_final_info*);

   private:
    // Interface for accessing backend metric data in the LB call tracker.
    class BackendMetricAccessor
        : public LoadBalancingPolicy::BackendMetricAccessor {
     public:
      explicit BackendMetricAccessor(
          grpc_metadata_batch* server_trailing_metadata)
          : server_trailing_metadata_(server_trailing_metadata) {}

      ~BackendMetricAccessor() override;
      const BackendMetricData* GetBackendMetricData() override;

     private:
      class BackendMetricAllocator : public BackendMetricAllocatorInterface {
       public:
        BackendMetricData* AllocateBackendMetricData() override {
          return GetContext<Arena>()->New<BackendMetricData>();
        }

        char* AllocateString(size_t size) override {
          return static_cast<char*>(GetContext<Arena>()->Alloc(size));
        }
      };

      grpc_metadata_batch* server_trailing_metadata_;
      const BackendMetricData* backend_metric_data_ = nullptr;
    };

    // FIXME: this isn't the right place to measure this from -- should be
    // doing it from before we do the LB pick
    gpr_cycle_counter lb_call_start_time_ = gpr_get_cycle_counter();
    Slice peer_string_;
  };
};

}  // namespace grpc_core

#endif
