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

#include <grpc/event_engine/event_engine.h>
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
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chaotic_good/config.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/server/server.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

// Channel arg: integer number of data connections to specify
// Defaults to 1 if not set
#define GRPC_ARG_CHAOTIC_GOOD_DATA_CONNECTIONS \
  "grpc.chaotic_good.data_connections"

namespace grpc_core {
namespace chaotic_good {
class ChaoticGoodServerListener final : public Server::ListenerInterface {
 public:
  static absl::AnyInvocable<std::string()> DefaultConnectionIDGenerator() {
    return [bitgen = absl::BitGen()]() mutable {
      return absl::StrCat(absl::Hex(absl::Uniform<uint64_t>(bitgen)));
    };
  }

  ChaoticGoodServerListener(
      Server* server, const ChannelArgs& args,
      absl::AnyInvocable<std::string()> connection_id_generator =
          DefaultConnectionIDGenerator());
  ~ChaoticGoodServerListener() override;
  // Bind address to EventEngine listener.
  absl::StatusOr<int> Bind(
      grpc_event_engine::experimental::EventEngine::ResolvedAddress addr);
  absl::Status StartListening();
  const ChannelArgs& args() const { return args_; }
  void Orphan() override;

  class ActiveConnection : public InternallyRefCounted<ActiveConnection> {
   public:
    ActiveConnection(
        RefCountedPtr<ChaoticGoodServerListener> listener,
        std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
            endpoint);
    ~ActiveConnection() override;
    const ChannelArgs& args() const { return listener_->args(); }

    void Orphan() override;

    class HandshakingState : public RefCounted<HandshakingState> {
     public:
      explicit HandshakingState(RefCountedPtr<ActiveConnection> connection);
      ~HandshakingState() override {};
      void Start(std::unique_ptr<
                 grpc_event_engine::experimental::EventEngine::Endpoint>
                     endpoint);

      void Shutdown() {
        handshake_mgr_->Shutdown(absl::CancelledError("Shutdown"));
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

      void OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result);
      Timestamp GetConnectionDeadline();
      const RefCountedPtr<ActiveConnection> connection_;
      const RefCountedPtr<HandshakeManager> handshake_mgr_;
    };

   private:
    void Done();
    void NewConnectionIDs(size_t count);
    RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
    const RefCountedPtr<ChaoticGoodServerListener> listener_;
    RefCountedPtr<HandshakingState> handshaking_state_;
    Mutex mu_;
    ActivityPtr receive_settings_activity_ ABSL_GUARDED_BY(mu_);
    bool orphaned_ ABSL_GUARDED_BY(mu_) = false;
    PromiseEndpoint endpoint_;
    std::vector<std::string> connection_ids_;
    Config config_;
    int32_t data_alignment_;
    absl::BitGen bitgen_;
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
  Server* const server_;
  ChannelArgs args_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>
      ee_listener_;
  Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // Map of connection id to endpoints connectivity.
  absl::flat_hash_map<std::string,
                      std::shared_ptr<InterActivityLatch<PromiseEndpoint>>>
      connectivity_map_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_set<OrphanablePtr<ActiveConnection>> connection_list_
      ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<std::string()> connection_id_generator_
      ABSL_GUARDED_BY(mu_);
  grpc_closure* on_destroy_done_ ABSL_GUARDED_BY(mu_) = nullptr;
};

}  // namespace chaotic_good
}  // namespace grpc_core

int grpc_server_add_chaotic_good_port(grpc_server* server, const char* addr);

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H
