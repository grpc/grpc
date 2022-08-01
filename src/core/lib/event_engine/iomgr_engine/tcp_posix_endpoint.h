// Copyright 2022 The gRPC Authors
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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_TCP_POSIX_ENDPOINT_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_TCP_POSIX_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include "grpc/event_engine/endpoint_config.h"
#include "grpc/event_engine/slice_buffer.h"
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/iomgr_engine/event_poller.h"
#include "src/core/lib/event_engine/iomgr_engine/iomgr_engine_closure.h"
#include "src/core/lib/event_engine/iomgr_engine/tcp_posix_socket_utils.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine {
namespace iomgr_engine {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::SliceBuffer;

class TcpZerocopySendCtx;
class TcpZerocopySendRecord;

class PosixEndpoint : public EventEngine::Endpoint {
 public:
  PosixEndpoint(EventHandle* handle, const PosixTcpOptions& options);
  ~PosixEndpoint() override;
  void Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override;
  void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override;
  const EventEngine::ResolvedAddress& GetPeerAddress() const override;
  const EventEngine::ResolvedAddress& GetLocalAddress() const override;
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

 private:
  int fd_;
  bool is_first_read_ = true;
  bool has_posted_reclaimer_ = false;
  double target_length_;
  int min_read_chunk_size_;
  int max_read_chunk_size_;
  double bytes_read_this_round_ = 0;
  std::atomic<int> ref_count_{1};

  // garbage after the last read.
  SliceBuffer last_read_buffer_;

  absl::Mutex read_mu_;
  SliceBuffer* incoming_buffer ABSL_GUARDED_BY(read_mu_) = nullptr;
  // bytes pending on the socket from the last read.
  int inq_ = 1;
  // cache whether kernel supports inq.
  bool inq_capable_ = false;

  SliceBuffer* outgoing_buffer_ = nullptr;
  // byte within outgoing_buffer's slices[0] to write next.
  size_t outgoing_byte_idx_ = 0;

  IomgrEngineClosure* on_read_ = nullptr;
  IomgrEngineClosure* on_write_ = nullptr;
  IomgrEngineClosure* on_error_ = nullptr;
  IomgrEngineClosure* release_fd_cb_ = nullptr;
  int* release_fd_ = nullptr;

  EventEngine::ResolvedAddress peer_address_;
  EventEngine::ResolvedAddress local_address_;

  grpc_core::MemoryOwner memory_owner_;
  grpc_core::MemoryAllocator::Reservation self_reservation_;

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
  EventHandle* handle_;
  EventPoller* poller_;
  PosixTcpOptions options_;
};

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_TCP_POSIX_ENDPOINT_H