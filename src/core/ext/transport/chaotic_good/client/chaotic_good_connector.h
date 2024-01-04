#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_CHAOTIC_GOOD_CONNECTOR_H_
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_CHAOTIC_GOOD_CONNECTOR_H_

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

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
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/wait_for_callback.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {
using grpc_event_engine::experimental::EventEngine;
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
  };

 private:
  static void OnHandshakeDone(void* arg, grpc_error_handle error);
  static ActivityPtr ReceiveSettingsFrame(ChaoticGoodConnector* self);
  Mutex mu_;
  Args args_;
  Result* result_ ABSL_GUARDED_BY(mu_);
  grpc_closure* notify_;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  ChannelArgs channel_args_;
  std::unique_ptr<MemoryQuota> memory_quota_;
  ResourceQuotaRefPtr resource_quota_;
  size_t initial_arena_size = 1024;
  MemoryAllocator memory_allocator_;
  ScopedArenaPtr arena_;
  absl::StatusOr<grpc_event_engine::experimental::EventEngine::ResolvedAddress>
      resolved_addr_;
  grpc_event_engine::experimental::ChannelArgsEndpointConfig ee_config_;
  EventEngine::Duration timeout_;
  std::shared_ptr<promise_detail::Context<Arena>> context_;
  std::shared_ptr<PromiseEndpoint> control_endpoint_;
  std::shared_ptr<PromiseEndpoint> data_endpoint_;
  ActivityPtr connect_activity_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  std::shared_ptr<HandshakeManager> handshake_mgr_;
  std::unique_ptr<HPackCompressor> hpack_compressor_;
  std::unique_ptr<HPackParser> hpack_parser_;
  std::shared_ptr<Latch<std::shared_ptr<PromiseEndpoint>>> data_endpoint_latch_;
  std::shared_ptr<WaitForCallback> wait_for_data_endpoint_callback_;
  Slice connection_id_;
};
}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_CHAOTIC_GOOD_CONNECTOR_H_
