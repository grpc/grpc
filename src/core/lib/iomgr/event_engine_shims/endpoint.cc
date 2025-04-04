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
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include <atomic>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/extensions/can_track_errors.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/construct_destruct.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine {
namespace experimental {
namespace {

constexpr int64_t kShutdownBit = static_cast<int64_t>(1) << 32;

// A wrapper class to manage Event Engine endpoint ref counting and
// asynchronous shutdown.
class EventEngineEndpointWrapper {
 public:
  struct grpc_event_engine_endpoint {
    grpc_endpoint base;
    EventEngineEndpointWrapper* wrapper;
    alignas(SliceBuffer) char read_buffer[sizeof(SliceBuffer)];
    alignas(SliceBuffer) char write_buffer[sizeof(SliceBuffer)];
  };

  explicit EventEngineEndpointWrapper(
      std::unique_ptr<EventEngine::Endpoint> endpoint);

  EventEngine::Endpoint* endpoint() { return endpoint_.get(); }

  std::unique_ptr<EventEngine::Endpoint> ReleaseEndpoint() {
    return std::move(endpoint_);
  }

  int Fd() {
    grpc_core::MutexLock lock(&mu_);
    return fd_;
  }

  absl::string_view PeerAddress() { return peer_address_; }

  absl::string_view LocalAddress() { return local_address_; }

  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  // Returns a managed grpc_endpoint object. It retains ownership of the
  // object.
  grpc_endpoint* GetGrpcEndpoint() { return &eeep_->base; }

  // Read using the underlying EventEngine endpoint object.
  bool Read(grpc_closure* read_cb, grpc_slice_buffer* pending_read_buffer,
            const EventEngine::Endpoint::ReadArgs* args) {
    Ref();
    pending_read_cb_ = read_cb;
    pending_read_buffer_ = pending_read_buffer;
    // TODO(vigneshbabu): Use SliceBufferCast<> here.
    grpc_core::Construct(reinterpret_cast<SliceBuffer*>(&eeep_->read_buffer),
                         SliceBuffer::TakeCSliceBuffer(*pending_read_buffer_));
    SliceBuffer* read_buffer =
        reinterpret_cast<SliceBuffer*>(&eeep_->read_buffer);
    read_buffer->Clear();
    return endpoint_->Read(
        [this](absl::Status status) { FinishPendingRead(status); }, read_buffer,
        args);
  }

  void FinishPendingRead(absl::Status status) {
    auto* read_buffer = reinterpret_cast<SliceBuffer*>(&eeep_->read_buffer);
    grpc_slice_buffer_move_into(read_buffer->c_slice_buffer(),
                                pending_read_buffer_);
    read_buffer->~SliceBuffer();
    if (GRPC_TRACE_FLAG_ENABLED(tcp)) {
      size_t i;
      LOG(INFO) << "TCP: " << eeep_->wrapper << " READ error=" << status;
      if (ABSL_VLOG_IS_ON(2)) {
        for (i = 0; i < pending_read_buffer_->count; i++) {
          char* dump = grpc_dump_slice(pending_read_buffer_->slices[i],
                                       GPR_DUMP_HEX | GPR_DUMP_ASCII);
          VLOG(2) << "READ DATA: " << dump;
          gpr_free(dump);
        }
      }
    }
    pending_read_buffer_ = nullptr;
    grpc_closure* cb = pending_read_cb_;
    pending_read_cb_ = nullptr;
    if (grpc_core::ExecCtx::Get() == nullptr) {
      grpc_core::ExecCtx exec_ctx;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, status);
    } else {
      grpc_core::Closure::Run(DEBUG_LOCATION, cb, status);
    }
    // For the ref taken in EventEngineEndpointWrapper::Read().
    Unref();
  }

  // Write using the underlying EventEngine endpoint object
  bool Write(grpc_closure* write_cb, grpc_slice_buffer* slices,
             const EventEngine::Endpoint::WriteArgs* args) {
    Ref();
    if (GRPC_TRACE_FLAG_ENABLED(tcp)) {
      size_t i;
      LOG(INFO) << "TCP: " << this << " WRITE (peer=" << PeerAddress() << ")";
      if (ABSL_VLOG_IS_ON(2)) {
        for (i = 0; i < slices->count; i++) {
          char* dump =
              grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
          VLOG(2) << "WRITE DATA: " << dump;
          gpr_free(dump);
        }
      }
    }
    // TODO(vigneshbabu): Use SliceBufferCast<> here.
    grpc_core::Construct(reinterpret_cast<SliceBuffer*>(&eeep_->write_buffer),
                         SliceBuffer::TakeCSliceBuffer(*slices));
    SliceBuffer* write_buffer =
        reinterpret_cast<SliceBuffer*>(&eeep_->write_buffer);
    pending_write_cb_ = write_cb;
    return endpoint_->Write(
        [this](absl::Status status) { FinishPendingWrite(status); },
        write_buffer, args);
  }

  void FinishPendingWrite(absl::Status status) {
    auto* write_buffer = reinterpret_cast<SliceBuffer*>(&eeep_->write_buffer);
    write_buffer->~SliceBuffer();
    GRPC_TRACE_LOG(tcp, INFO)
        << "TCP: " << this << " WRITE (peer=" << PeerAddress()
        << ") error=" << status;
    grpc_closure* cb = pending_write_cb_;
    pending_write_cb_ = nullptr;
    if (grpc_core::ExecCtx::Get() == nullptr) {
      grpc_core::ExecCtx exec_ctx;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, status);
    } else {
      grpc_core::Closure::Run(DEBUG_LOCATION, cb, status);
    }
    // For the ref taken in EventEngineEndpointWrapper::Write().
    Unref();
  }

  // Returns true if the endpoint is not yet shutdown. In that case, it also
  // acquires a shutdown ref. Otherwise it returns false and doesn't modify
  // the shutdown ref.
  bool ShutdownRef() {
    int64_t curr = shutdown_ref_.load(std::memory_order_acquire);
    while (true) {
      if (curr & kShutdownBit) {
        return false;
      }
      if (shutdown_ref_.compare_exchange_strong(curr, curr + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  // Decrement the shutdown ref. If this is the last shutdown ref, it also
  // deletes the underlying event engine endpoint. Deletion of the event
  // engine endpoint should trigger execution of any pending read/write
  // callbacks with NOT-OK status.
  void ShutdownUnref() {
    if (shutdown_ref_.fetch_sub(1, std::memory_order_acq_rel) ==
        kShutdownBit + 1) {
      auto* supports_fd =
          QueryExtension<EndpointSupportsFdExtension>(endpoint_.get());
      if (supports_fd != nullptr && fd_ > 0 && on_release_fd_) {
        supports_fd->Shutdown(std::move(on_release_fd_));
      }
      OnShutdownInternal();
    }
  }

  // If trigger shutdown is called the first time, it sets the shutdown bit
  // and decrements the shutdown ref. If trigger shutdown has been called
  // before or in parallel, only one of them would win the race. The other
  // invocation would simply return.
  void TriggerShutdown(
      absl::AnyInvocable<void(absl::StatusOr<int>)> on_release_fd) {
    auto* supports_fd =
        QueryExtension<EndpointSupportsFdExtension>(endpoint_.get());
    if (supports_fd != nullptr) {
      on_release_fd_ = std::move(on_release_fd);
    }
    int64_t curr = shutdown_ref_.load(std::memory_order_acquire);
    while (true) {
      if (curr & kShutdownBit) {
        return;
      }
      if (shutdown_ref_.compare_exchange_strong(curr, curr | kShutdownBit,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
        Ref();
        if (shutdown_ref_.fetch_sub(1, std::memory_order_acq_rel) ==
            kShutdownBit + 1) {
          if (supports_fd != nullptr && fd_ > 0 && on_release_fd_) {
            supports_fd->Shutdown(std::move(on_release_fd_));
          }
          OnShutdownInternal();
        }
        return;
      }
    }
  }

  bool CanTrackErrors() {
    auto* can_track_errors =
        QueryExtension<EndpointCanTrackErrorsExtension>(endpoint_.get());
    if (can_track_errors != nullptr) {
      return can_track_errors->CanTrackErrors();
    } else {
      return false;
    }
  }

 private:
  void OnShutdownInternal() {
    {
      grpc_core::MutexLock lock(&mu_);
      fd_ = -1;
    }
    endpoint_.reset();
    // For the Ref taken in TriggerShutdown
    Unref();
  }
  std::unique_ptr<EventEngine::Endpoint> endpoint_;
  std::unique_ptr<grpc_event_engine_endpoint> eeep_;
  std::atomic<int64_t> refs_{1};
  std::atomic<int64_t> shutdown_ref_{1};
  absl::AnyInvocable<void(absl::StatusOr<int>)> on_release_fd_;
  grpc_core::Mutex mu_;
  grpc_closure* pending_read_cb_;
  grpc_closure* pending_write_cb_;
  grpc_slice_buffer* pending_read_buffer_;
  const std::string peer_address_{
      ResolvedAddressToURI(endpoint_->GetPeerAddress()).value_or("")};
  const std::string local_address_{
      ResolvedAddressToURI(endpoint_->GetLocalAddress()).value_or("")};
  int fd_{-1};
};

// Read from the endpoint and place the data in slices slice buffer. The
// provided closure is also invoked asynchronously.
void EndpointRead(grpc_endpoint* ep, grpc_slice_buffer* slices,
                  grpc_closure* cb, bool /* urgent */, int min_progress_size) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (!eeep->wrapper->ShutdownRef()) {
    // Shutdown has already been triggered on the endpoint.
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError());
    return;
  }

  EventEngine::Endpoint::ReadArgs read_args = {min_progress_size};
  if (eeep->wrapper->Read(cb, slices, &read_args)) {
    // Read succeeded immediately. Run the callback inline.
    eeep->wrapper->FinishPendingRead(absl::OkStatus());
  }

  eeep->wrapper->ShutdownUnref();
}

// Write the data from slices and invoke the provided closure asynchronously
// after the write is complete.
void EndpointWrite(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, void* arg, int max_frame_size) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (!eeep->wrapper->ShutdownRef()) {
    // Shutdown has already been triggered on the endpoint.
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError());
    return;
  }

  EventEngine::Endpoint::WriteArgs write_args = {arg, max_frame_size};
  if (eeep->wrapper->Write(cb, slices, &write_args)) {
    // Write succeeded immediately. Run the callback inline.
    eeep->wrapper->FinishPendingWrite(absl::OkStatus());
  }
  eeep->wrapper->ShutdownUnref();
}

void EndpointAddToPollset(grpc_endpoint* /* ep */,
                          grpc_pollset* /* pollset */) {}
void EndpointAddToPollsetSet(grpc_endpoint* /* ep */,
                             grpc_pollset_set* /* pollset */) {}
void EndpointDeleteFromPollsetSet(grpc_endpoint* /* ep */,
                                  grpc_pollset_set* /* pollset */) {}

/// Attempts to free the underlying data structures.
/// After destruction, no new endpoint operations may be started.
/// It is the caller's responsibility to ensure that calls to EndpointDestroy
/// are synchronized.
void EndpointDestroy(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  GRPC_TRACE_LOG(event_engine, INFO)
      << "EventEngine::Endpoint::" << eeep->wrapper << " EndpointDestroy";
  eeep->wrapper->TriggerShutdown(nullptr);
  eeep->wrapper->Unref();
}

absl::string_view EndpointGetPeerAddress(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  return eeep->wrapper->PeerAddress();
}

absl::string_view EndpointGetLocalAddress(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  return eeep->wrapper->LocalAddress();
}

int EndpointGetFd(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  return eeep->wrapper->Fd();
}

bool EndpointCanTrackErr(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  return eeep->wrapper->CanTrackErrors();
}

grpc_endpoint_vtable grpc_event_engine_endpoint_vtable = {
    EndpointRead,
    EndpointWrite,
    EndpointAddToPollset,
    EndpointAddToPollsetSet,
    EndpointDeleteFromPollsetSet,
    EndpointDestroy,
    EndpointGetPeerAddress,
    EndpointGetLocalAddress,
    EndpointGetFd,
    EndpointCanTrackErr};

EventEngineEndpointWrapper::EventEngineEndpointWrapper(
    std::unique_ptr<EventEngine::Endpoint> endpoint)
    : endpoint_(std::move(endpoint)),
      eeep_(std::make_unique<grpc_event_engine_endpoint>()) {
  eeep_->base.vtable = &grpc_event_engine_endpoint_vtable;
  eeep_->wrapper = this;
  auto* supports_fd =
      QueryExtension<EndpointSupportsFdExtension>(endpoint_.get());
  if (supports_fd != nullptr) {
    fd_ = supports_fd->GetWrappedFd();
  } else {
    fd_ = -1;
  }
  GRPC_TRACE_LOG(event_engine, INFO)
      << "EventEngine::Endpoint " << eeep_->wrapper << " Create";
}

}  // namespace

grpc_endpoint* grpc_event_engine_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint) {
  DCHECK(ee_endpoint != nullptr);
  auto wrapper = new EventEngineEndpointWrapper(std::move(ee_endpoint));
  return wrapper->GetGrpcEndpoint();
}

bool grpc_is_event_engine_endpoint(grpc_endpoint* ep) {
  return ep->vtable == &grpc_event_engine_endpoint_vtable;
}

EventEngine::Endpoint* grpc_get_wrapped_event_engine_endpoint(
    grpc_endpoint* ep) {
  if (!grpc_is_event_engine_endpoint(ep)) {
    return nullptr;
  }
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  return eeep->wrapper->endpoint();
}

std::unique_ptr<EventEngine::Endpoint> grpc_take_wrapped_event_engine_endpoint(
    grpc_endpoint* ep) {
  if (!grpc_is_event_engine_endpoint(ep)) {
    return nullptr;
  }
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  auto endpoint = eeep->wrapper->ReleaseEndpoint();
  eeep->wrapper->Unref();
  return endpoint;
}

void grpc_event_engine_endpoint_destroy_and_release_fd(
    grpc_endpoint* ep, int* fd, grpc_closure* on_release_fd) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (fd == nullptr || on_release_fd == nullptr) {
    if (fd != nullptr) {
      *fd = -1;
    }
    eeep->wrapper->TriggerShutdown(nullptr);
  } else {
    *fd = -1;
    eeep->wrapper->TriggerShutdown(
        [fd, on_release_fd](absl::StatusOr<int> release_fd) {
          if (release_fd.ok()) {
            *fd = *release_fd;
          }
          RunEventEngineClosure(on_release_fd,
                                absl_status_to_grpc_error(release_fd.status()));
        });
  }
  eeep->wrapper->Unref();
}

}  // namespace experimental
}  // namespace grpc_event_engine
