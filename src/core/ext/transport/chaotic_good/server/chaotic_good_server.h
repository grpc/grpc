// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/handshaker_registry.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace chaotic_good {
class ChaoticGoodServerListener
    : public Server::ListenerInterface,
      public std::enable_shared_from_this<ChaoticGoodServerListener> {
 public:
  ChaoticGoodServerListener(Server* server, const ChannelArgs& args);
  ~ChaoticGoodServerListener() override;
  // Bind address to EventEngine listener.
  absl::StatusOr<int> Bind(const char* addr);
  absl::Status StartListening();
  const ChannelArgs& args() const { return args_; }
  void Orphan() override{};

  class ActiveConnection
      : public std::enable_shared_from_this<ActiveConnection> {
   public:
    explicit ActiveConnection(
        std::shared_ptr<ChaoticGoodServerListener> listener);
    ~ActiveConnection();
    void Start(
        std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
            endpoint);
    const ChannelArgs& args() const { return listener_->args(); }

    class HandshakingState
        : public std::enable_shared_from_this<HandshakingState> {
     public:
      explicit HandshakingState(std::shared_ptr<ActiveConnection> connection);
      ~HandshakingState(){};
      void Start(std::unique_ptr<
                 grpc_event_engine::experimental::EventEngine::Endpoint>
                     endpoint);

     private:
      static auto EndpointReadSettingsFrame(
          std::shared_ptr<HandshakingState> self);
      static auto EndpointWriteSettingsFrame(
          std::shared_ptr<HandshakingState> self, bool is_control_endpoint);
      static auto WaitForDataEndpointSetup(
          std::shared_ptr<HandshakingState> self);
      static auto ControlEndpointWriteSettingsFrame(
          std::shared_ptr<HandshakingState> self);
      static auto DataEndpointWriteSettingsFrame(
          std::shared_ptr<HandshakingState> self);

      static void OnHandshakeDone(void* arg, grpc_error_handle error);
      Timestamp GetConnectionDeadline();
      std::shared_ptr<ActiveConnection> connection_;
      std::shared_ptr<HandshakeManager> handshake_mgr_;
    };

   private:
    std::string GenerateConnectionIDLocked();
    void NewConnectionID();
    std::shared_ptr<ChaoticGoodServerListener> listener_;
    const size_t kInitialArenaSize = 1024;
    const Duration kConnectionDeadline = Duration::Seconds(5);
    std::shared_ptr<HandshakingState> handshaking_state_;
    ActivityPtr receive_settings_activity_;
    std::shared_ptr<PromiseEndpoint> endpoint_;
    HPackCompressor hpack_compressor_;
    HPackParser hpack_parser_;
    absl::BitGen bitgen_;
    Slice connection_id_;
    int32_t data_alignment_;
  };

  // Overridden to initialize listener but not actually used.
  void Start(Server*, const std::vector<grpc_pollset*>*) override{};
  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return nullptr;
  }
  void SetOnDestroyDone(grpc_closure*) override{};

 private:
  Server* server_;
  ChannelArgs args_;
  grpc_event_engine::experimental::ChannelArgsEndpointConfig config_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>
      ee_listener_;
  Mutex mu_;
  // Map of connection id to endpoints connectivity.
  absl::flat_hash_map<std::string,
                      std::shared_ptr<Latch<std::shared_ptr<PromiseEndpoint>>>>
      connectivity_map_ ABSL_GUARDED_BY(mu_);
  std::vector<std::shared_ptr<ActiveConnection>> connection_list_
      ABSL_GUARDED_BY(mu_);
};
}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H