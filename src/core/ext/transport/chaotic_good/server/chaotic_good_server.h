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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {
class ChaoticGoodServerListener final
    : public Server::ListenerInterface,
      public RefCounted<ChaoticGoodServerListener> {
 public:
  ChaoticGoodServerListener(Server* server, const ChannelArgs& args);
  ~ChaoticGoodServerListener() override;
  // Bind address to EventEngine listener.
  absl::StatusOr<int> Bind(const char* addr);
  absl::Status StartListening();
  const ChannelArgs& args() const { return args_; }
  void Orphan() override {
    gpr_log(GPR_INFO, "ORPHAN");
    {
      std::vector<OrphanablePtr<ActiveConnection>> connection_list;
      MutexLock lock(&mu_);
      connection_list = std::move(connection_list_);
    }
    ee_listener_.reset();
    Unref();
    gpr_log(GPR_INFO, "~ORPHAN");
  };

  class ActiveConnection : public InternallyRefCounted<ActiveConnection> {
   public:
    explicit ActiveConnection(
        RefCountedPtr<ChaoticGoodServerListener> listener);
    ~ActiveConnection() override;
    void Start(
        std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
            endpoint);
    const ChannelArgs& args() const { return listener_->args(); }

    void Orphan() override {
      if (handshaking_state_ != nullptr) {
        handshaking_state_->Shutdown();
        handshaking_state_.reset();
      }
      listener_.reset();
      receive_settings_activity_.reset();
      Unref();
    }

    class HandshakingState : public RefCounted<HandshakingState> {
     public:
      explicit HandshakingState(RefCountedPtr<ActiveConnection> connection);
      ~HandshakingState() override{};
      void Start(std::unique_ptr<
                 grpc_event_engine::experimental::EventEngine::Endpoint>
                     endpoint);

      void Shutdown() {
        gpr_log(GPR_INFO, "Shutdown:%p", this);
        handshake_mgr_->Shutdown(absl::CancelledError("Shutdown"));
        connection_.reset();
      }

     private:
      static auto EndpointReadSettingsFrame(
          RefCountedPtr<HandshakingState> self);
      static auto EndpointWriteSettingsFrame(
          RefCountedPtr<HandshakingState> self, bool is_control_endpoint);
      static auto WaitForDataEndpointSetup(
          RefCountedPtr<HandshakingState> self);
      static auto ControlEndpointWriteSettingsFrame(
          RefCountedPtr<HandshakingState> self);
      static auto DataEndpointWriteSettingsFrame(
          RefCountedPtr<HandshakingState> self);

      static void OnHandshakeDone(void* arg, grpc_error_handle error);
      Timestamp GetConnectionDeadline();
      RefCountedPtr<ActiveConnection> connection_;
      const RefCountedPtr<HandshakeManager> handshake_mgr_;
    };

   private:
    std::string GenerateConnectionIDLocked();
    void NewConnectionID();
    RefCountedPtr<ChaoticGoodServerListener> listener_;
    const size_t kInitialArenaSize = 1024;
    const Duration kConnectionDeadline = Duration::Seconds(5);
    RefCountedPtr<HandshakingState> handshaking_state_;
    ActivityPtr receive_settings_activity_;
    std::shared_ptr<PromiseEndpoint> endpoint_;
    HPackCompressor hpack_compressor_;
    HPackParser hpack_parser_;
    absl::BitGen bitgen_;
    std::string connection_id_;
    int32_t data_alignment_;
  };

  void Start(Server*, const std::vector<grpc_pollset*>*) override {
    StartListening().IgnoreError();
  };
  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return nullptr;
  }
  void SetOnDestroyDone(grpc_closure* closure) override {
    MutexLock lock(&mu_);
    on_destroy_done_ = closure;
  };

 private:
  Server* server_;
  ChannelArgs args_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>
      ee_listener_;
  Mutex mu_;
  // Map of connection id to endpoints connectivity.
  absl::flat_hash_map<std::string,
                      std::shared_ptr<Latch<std::shared_ptr<PromiseEndpoint>>>>
      connectivity_map_ ABSL_GUARDED_BY(mu_);
  std::vector<OrphanablePtr<ActiveConnection>> connection_list_
      ABSL_GUARDED_BY(mu_);
  grpc_closure* on_destroy_done_ ABSL_GUARDED_BY(mu_) = nullptr;
  grpc_event_engine::experimental::MemoryAllocator memory_allocator_ =
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "server_connection");
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H