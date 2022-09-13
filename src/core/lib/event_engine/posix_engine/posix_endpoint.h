// Copyright 2022 gRPC Authors
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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENDPOINT_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <memory>

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/traced_buffer_list.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine {
namespace posix_engine {

#ifdef GRPC_POSIX_SOCKET_TCP

using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::SliceBuffer;

class TcpZerocopySendCtx;
class TcpZerocopySendRecord;

class PosixEndpointImpl {
 public:
  PosixEndpointImpl(EventHandle* handle, PosixEngineClosure* on_done,
                    std::shared_ptr<EventEngine> engine,
                    const PosixTcpOptions& options);
  ~PosixEndpointImpl();
  void Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const EventEngine::Endpoint::ReadArgs* args);
  void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const EventEngine::Endpoint::WriteArgs* args);
  const EventEngine::ResolvedAddress& GetPeerAddress() const {
    return peer_address_;
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const {
    return local_address_;
  }

  void MaybeShutdown(absl::Status why);

 private:
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }
  void UpdateRcvLowat() ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void HandleWrite(absl::Status status);
  void HandleError(absl::Status status);
  void HandleRead(absl::Status status);
  void MaybeMakeReadSlices() ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  bool TcpDoRead(absl::Status& status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void FinishEstimate();
  void AddToEstimate(size_t bytes);
  void MaybePostReclaimer() ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void PerformReclamation() ABSL_LOCKS_EXCLUDED(read_mu_);
  // Zero copy related helper methods.
  TcpZerocopySendRecord* TcpGetSendZerocopyRecord(SliceBuffer& buf);
  bool DoFlushZerocopy(TcpZerocopySendRecord* record, absl::Status& status);
  bool TcpFlushZerocopy(TcpZerocopySendRecord* record, absl::Status& status);
  bool TcpFlush(absl::Status& status);
  void TcpShutdownTracedBufferList();
  void UnrefMaybePutZerocopySendRecord(TcpZerocopySendRecord* record);
  void ZerocopyDisableAndWaitForRemaining();
  bool WriteWithTimestamps(struct msghdr* msg, size_t sending_length,
                           ssize_t* sent_length, int* saved_errno,
                           int additional_flags);
#ifdef GRPC_LINUX_ERRQUEUE
  // Reads \a cmsg to process zerocopy control messages.
  bool ProcessErrors();
  void ProcessZerocopy(struct cmsghdr* cmsg);
  // Reads \a cmsg to derive timestamps from the control messages.
  struct cmsghdr* ProcessTimestamp(msghdr* msg, struct cmsghdr* cmsg);
#endif
  absl::Mutex read_mu_;
  absl::Mutex traced_buffer_mu_;
  PosixSocketWrapper sock_;
  int fd_;
  bool is_first_read_ = true;
  bool has_posted_reclaimer_ ABSL_GUARDED_BY(read_mu_) = false;
  double target_length_;
  int min_read_chunk_size_;
  int max_read_chunk_size_;
  int set_rcvlowat_ = 0;
  double bytes_read_this_round_ = 0;
  std::atomic<int> ref_count_{1};

  // garbage after the last read.
  SliceBuffer last_read_buffer_;

  SliceBuffer* incoming_buffer_ ABSL_GUARDED_BY(read_mu_) = nullptr;
  // bytes pending on the socket from the last read.
  int inq_ = 1;
  // cache whether kernel supports inq.
  bool inq_capable_ = false;

  SliceBuffer* outgoing_buffer_ = nullptr;
  // byte within outgoing_buffer's slices[0] to write next.
  size_t outgoing_byte_idx_ = 0;

  PosixEngineClosure* on_read_ = nullptr;
  PosixEngineClosure* on_write_ = nullptr;
  PosixEngineClosure* on_error_ = nullptr;
  PosixEngineClosure* on_done_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> read_cb_;
  absl::AnyInvocable<void(absl::Status)> write_cb_;

  EventEngine::ResolvedAddress peer_address_;
  EventEngine::ResolvedAddress local_address_;

  grpc_core::MemoryOwner memory_owner_;
  grpc_core::MemoryAllocator::Reservation self_reservation_;

  void* outgoing_buffer_arg_ = nullptr;

  // A counter which starts at 0. It is initialized the first time the socket
  // options for collecting timestamps are set, and is incremented with each
  // byte sent.
  int bytes_counter_ = -1;
  // True if timestamping options are set on the socket.
  bool socket_ts_enabled_ = false;
  // Cache whether we can set timestamping options
  bool ts_capable_ = true;
  // Set to 1 if we do not want to be notified on errors anymore.
  std::atomic<bool> stop_error_notification_{false};
  std::unique_ptr<TcpZerocopySendCtx> tcp_zerocopy_send_ctx_;
  TcpZerocopySendRecord* current_zerocopy_send_ = nullptr;
  // If true, the size of buffers alloted for tcp reads will be based on the
  // specified min_progress_size values conveyed by the upper layers.
  bool frame_size_tuning_enabled_ = false;
  // A hint from upper layers specifying the minimum number of bytes that need
  // to be read to make meaningful progress.
  int min_progress_size_ = 1;
  TracedBufferList traced_buffers_;
  EventHandle* handle_;
  PosixEventPoller* poller_;
  std::shared_ptr<EventEngine> engine_;
};

class PosixEndpoint : public EventEngine::Endpoint {
 public:
  PosixEndpoint(EventHandle* handle, PosixEngineClosure* on_shutdown,
                std::shared_ptr<EventEngine> engine,
                const EndpointConfig& config)
      : impl_(new PosixEndpointImpl(handle, on_shutdown, std::move(engine),
                                    TcpOptionsFromEndpointConfig(config))) {}

  void Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const EventEngine::Endpoint::ReadArgs* args) override {
    impl_->Read(std::move(on_read), buffer, args);
  }

  void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data,
             const EventEngine::Endpoint::WriteArgs* args) override {
    impl_->Write(std::move(on_writable), data, args);
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return impl_->GetPeerAddress();
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return impl_->GetLocalAddress();
  }

  ~PosixEndpoint() override {
    impl_->MaybeShutdown(absl::InternalError("Endpoint closing"));
  }

 private:
  PosixEndpointImpl* impl_;
};

#else  // GRPC_POSIX_SOCKET_TCP

class PosixEndpoint : public EventEngine::Endpoint {
 public:
  PosixEndpoint() = default;

  void Read(absl::AnyInvocable<void(absl::Status)> /*on_read*/,
            SliceBuffer* /*buffer*/,
            const EventEngine::Endpoint::ReadArgs* /*args*/) override {
    GPR_ASSERT(false && "PosixEndpoint::Read not supported on this platform");
  }

  void Write(absl::AnyInvocable<void(absl::Status)> /*on_writable*/,
             SliceBuffer* /*data*/,
             const EventEngine::Endpoint::WriteArgs* /*args*/) override {
    GPR_ASSERT(false && "PosixEndpoint::Write not supported on this platform");
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    GPR_ASSERT(false &&
               "PosixEndpoint::GetPeerAddress not supported on this platform");
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    GPR_ASSERT(false &&
               "PosixEndpoint::GetLocalAddress not supported on this platform");
  }

  ~PosixEndpoint() override = default;
};

#endif  // GRPC_POSIX_SOCKET_TCP

// Create a PosixEndpoint.
// A shared_ptr of the EventEngine is passed to the endpoint to ensure that
// the event engine is alive for the lifetime of the endpoint.
std::unique_ptr<PosixEndpoint> CreatePosixEndpoint(
    EventHandle* handle, PosixEngineClosure* on_shutdown,
    std::shared_ptr<EventEngine> engine, const EndpointConfig& config);

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENDPOINT_H