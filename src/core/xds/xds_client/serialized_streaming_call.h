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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_SERIALIZED_STREAMING_CALL_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_SERIALIZED_STREAMING_CALL_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Shared state for lock-free coordination between producer (Send) and consumer
// (Write Loop)
struct WriteState {
  std::string payload;
  Mutex mu;
  bool done ABSL_GUARDED_BY(&mu) = false;
  absl::Status status ABSL_GUARDED_BY(&mu);
  Waker waker ABSL_GUARDED_BY(&mu);
};

// A decorator wrapper for XdsTransportFactory::XdsTransport::StreamingCall
// that serializes writes lock-free using Mpsc queue and thread-safe Wakers.
class SerializedStreamingCall
    : public XdsTransportFactory::XdsTransport::StreamingCall {
 public:
  SerializedStreamingCall(
      RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
      const char* method,
      std::unique_ptr<
          XdsTransportFactory::XdsTransport::StreamingCall::EventHandler>
          user_event_handler);

  ~SerializedStreamingCall() override;

  auto Send(std::string payload) {
    auto state = std::make_shared<WriteState>();
    state->payload = std::move(payload);
    // Push to cleanup list lock-free!
    CleanupNode* node = new CleanupNode{state, nullptr};
    CleanupNode* old_head = cleanup_list_.load(std::memory_order_relaxed);
    do {
      node->next = old_head;
    } while (!cleanup_list_.compare_exchange_weak(
        old_head, node, std::memory_order_release, std::memory_order_relaxed));
    auto send_result = mpsc_sender_.UnbufferedImmediateSend(
        std::shared_ptr<WriteState>(state), 1);
    bool send_ok = send_result.ok();
    return [state, send_ok]() -> Poll<absl::Status> {
      if (!send_ok) {
        return absl::CancelledError("Stream closed");
      }
      MutexLock lock(&state->mu);
      if (state->done) {
        return state->status;
      }
      state->waker = GetContext<Activity>()->MakeNonOwningWaker();
      if (state->done) {
        return state->status;
      }
      return Pending{};
    };
  }

  // Standard interface methods
  void SendMessage(std::string payload) override;
  void StartRecvMessage() override;
  void Orphan() override;

 private:
  class InternalEventHandler;

  struct CleanupNode {
    std::weak_ptr<WriteState> state;
    CleanupNode* next;
  };

  // Callback handlers called by InternalEventHandler
  void OnRequestSent(bool ok);
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  void DrainQueueAndFail(absl::Status status);
  void CleanupExpiredNodes();
  void OnUnderlyingCallDestroyed();

  const std::unique_ptr<
      XdsTransportFactory::XdsTransport::StreamingCall::EventHandler>
      user_event_handler_;
  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
      underlying_call_;

  MpscReceiver<std::shared_ptr<WriteState>> receiver_;
  MpscSender<std::shared_ptr<WriteState>> mpsc_sender_;
  RefCountedPtr<Party> party_;

  Mutex mu_;
  std::shared_ptr<WriteState> active_write_ ABSL_GUARDED_BY(&mu_);
  Waker write_waker_ ABSL_GUARDED_BY(&mu_);
  bool write_completed_ ABSL_GUARDED_BY(&mu_) = false;
  bool write_ok_ ABSL_GUARDED_BY(&mu_) = false;

  std::atomic<CleanupNode*> cleanup_list_{nullptr};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_SERIALIZED_STREAMING_CALL_H
