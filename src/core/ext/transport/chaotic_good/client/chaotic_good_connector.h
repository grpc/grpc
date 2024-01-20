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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_CHAOTIC_GOOD_CONNECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_CHAOTIC_GOOD_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/random/random.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/wait_for_callback.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {
class ChaoticGoodConnector
    : public SubchannelConnector,
      public std::enable_shared_from_this<ChaoticGoodConnector> {
 public:
  ChaoticGoodConnector();
  ~ChaoticGoodConnector() override;
  void Connect(const Args& args, Result* result, grpc_closure* notify) override;
  void Shutdown(grpc_error_handle error) override {
    MutexLock lock(&mu_);
    is_shutdown_ = true;
    if (handshake_mgr_ != nullptr) {
      handshake_mgr_->Shutdown(error);
    }
    if (connect_activity_ != nullptr) {
      connect_activity_.reset();
    }
    if (timer_handle_.has_value()) {
      timer_handle_.reset();
    }
  };

 private:
  static auto DataEndpointReadSettingsFrame(
      std::shared_ptr<ChaoticGoodConnector> self);
  static auto DataEndpointWriteSettingsFrame(
      std::shared_ptr<ChaoticGoodConnector> self);
  static auto ControlEndpointReadSettingsFrame(
      std::shared_ptr<ChaoticGoodConnector> self);
  static auto ControlEndpointWriteSettingsFrame(
      std::shared_ptr<ChaoticGoodConnector> self);
  static auto WaitForDataEndpointSetup(
      std::shared_ptr<ChaoticGoodConnector> self);
  static void OnHandshakeDone(void* arg, grpc_error_handle error);
  void OnTimeout();

  Mutex mu_;
  Args args_;
  Result* result_ ABSL_GUARDED_BY(mu_);
  grpc_closure* notify_;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  ChannelArgs channel_args_;
  const size_t kInitialArenaSize = 1024;
  absl::StatusOr<grpc_event_engine::experimental::EventEngine::ResolvedAddress>
      resolved_addr_;
  grpc_event_engine::experimental::MemoryAllocator memory_allocator_;
  std::shared_ptr<PromiseEndpoint> control_endpoint_;
  std::shared_ptr<PromiseEndpoint> data_endpoint_;
  ActivityPtr connect_activity_ ABSL_GUARDED_BY(mu_);
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  std::shared_ptr<HandshakeManager> handshake_mgr_;
  HPackCompressor hpack_compressor_;
  HPackParser hpack_parser_;
  absl::BitGen bitgen_;
  std::shared_ptr<Latch<std::shared_ptr<PromiseEndpoint>>> data_endpoint_latch_;
  std::shared_ptr<WaitForCallback> wait_for_data_endpoint_callback_;
  Slice connection_id_;
  const int32_t kDataAlignmentBytes = 64;
  absl::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      timer_handle_ ABSL_GUARDED_BY(mu_);
};
}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_CHAOTIC_GOOD_CONNECTOR_H
