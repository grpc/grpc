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
using grpc_event_engine::experimental::EventEngine;

class ChaoticGoodServerListener
    : public Server::ListenerInterface,
      public InternallyRefCounted<ChaoticGoodServerListener> {
 public:
  ChaoticGoodServerListener(Server* server, const ChannelArgs& args);
  ~ChaoticGoodServerListener() override{};
  // Bind address to EventEngine listener.
  absl::StatusOr<int> Bind(const char* addr);
  // Overridden to initialize listener but not actually used.
  void Start(Server* server,
             const std::vector<grpc_pollset*>* pollsets) override{};
  absl::Status StartListening();
  // TODO(ladynana): add channelz support if needed.
  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return nullptr;
  }
  void SetOnDestroyDone(grpc_closure* on_destroy_done) override{};
  void Orphan() override{};
  const ChannelArgs& args() const { return args_; }
  class ActiveConnection : public InternallyRefCounted<ActiveConnection> {
   public:
    ActiveConnection(RefCountedPtr<ChaoticGoodServerListener> listener_);
    ~ActiveConnection() {
      listener_.reset();
      std::cout << "connection close"
                << "\n";
      fflush(stdout);
      if (receive_setting_frames_ != nullptr) receive_setting_frames_.reset();
    };
    void Orphan() override{};
    // Start handshake.
    void Start(std::unique_ptr<EventEngine::Endpoint> endpoint);
    class HandshakingState : public InternallyRefCounted<HandshakingState> {
     public:
      HandshakingState(RefCountedPtr<ActiveConnection> connection);
      ~HandshakingState() {
        std::cout << "handshaking state close"
                  << "\n";
        fflush(stdout);
      };
      void Orphan() override{};
      void Start(std::unique_ptr<EventEngine::Endpoint> endpoint);

     private:
      static void OnHandshakeDone(void* arg, grpc_error_handle error);
      static ActivityPtr OnReceive(std::shared_ptr<PromiseEndpoint> endpoint,
                                   HandshakingState* self);
      Timestamp GetConnectionDeadline(const ChannelArgs& args) {
        return Timestamp::Now() +
               std::max(Duration::Milliseconds(1),
                        args.GetDurationFromIntMillis(
                                GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
                            .value_or(Duration::Seconds(120)));
      }
      RefCountedPtr<ActiveConnection> connection_;
      std::shared_ptr<HandshakeManager> handshake_mgr_;
      ScopedArenaPtr arena_;
      promise_detail::Context<Arena> context_;
    };
    const ChannelArgs& args() const { return listener_->args(); }

   private:
    RefCountedPtr<ChaoticGoodServerListener> listener_;
    MemoryAllocator memory_allocator_;
    ScopedArenaPtr arena_;
    promise_detail::Context<Arena> context_;
    std::shared_ptr<HandshakingState> handshaking_state_;
    Mutex mu_ ABSL_ACQUIRED_AFTER(&listener_->mu_);
    ActivityPtr receive_setting_frames_;
    std::shared_ptr<PromiseEndpoint> endpoint_;
  };

 private:
  Server* server_;
  const ChannelArgs& args_;
  grpc_event_engine::experimental::ChannelArgsEndpointConfig config_;
  std::shared_ptr<EventEngine> event_engine_;
  std::unique_ptr<EventEngine::Listener> ee_listener_;
  Mutex mu_;
  // Map of connection id to endpoints connectivity.
  std::map<std::string,
           std::shared_ptr<Latch<std::shared_ptr<PromiseEndpoint>>>>
      connectivity_map_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<HPackCompressor> hpack_compressor_;
  std::unique_ptr<HPackParser> hpack_parser_;
};

// Adds a port to server.  Sets port_num to the port number.
int ChaoticGoodServerAddPort(Server* server, const char* addr,
                             const ChannelArgs& args);
}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H