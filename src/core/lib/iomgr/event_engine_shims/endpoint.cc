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
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"

#include <atomic>
#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/error_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

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
    std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
        read_buffer;
    std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
        write_buffer;
  };

  explicit EventEngineEndpointWrapper(
      std::unique_ptr<EventEngine::Endpoint> endpoint);

  int Fd() {
    grpc_core::MutexLock lock(&mu_);
    return fd_;
  }

  absl::string_view PeerAddress() {
    grpc_core::MutexLock lock(&mu_);
    return peer_address_;
  }

  absl::string_view LocalAddress() {
    grpc_core::MutexLock lock(&mu_);
    return local_address_;
  }

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
  void Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const EventEngine::Endpoint::ReadArgs* args) {
    endpoint_->Read(std::move(on_read), buffer, args);
  }

  // Write using the underlying EventEngine endpoint object
  void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const EventEngine::Endpoint::WriteArgs* args) {
    endpoint_->Write(std::move(on_writable), data, args);
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
      OnShutdownInternal();
    }
  }

  // If trigger shutdown is called the first time, it sets the shutdown bit
  // and decrements the shutdown ref. If trigger shutdown has been called
  // before or in parallel, only one of them would win the race. The other
  // invocation would simply return.
  void TriggerShutdown() {
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
          OnShutdownInternal();
        }
        return;
      }
    }
  }

 private:
  void OnShutdownInternal() {
    {
      grpc_core::MutexLock lock(&mu_);
      fd_ = -1;
      local_address_ = "";
      peer_address_ = "";
    }
    endpoint_.reset();
    // For the Ref taken in TriggerShutdown
    Unref();
  }
  std::unique_ptr<EventEngine::Endpoint> endpoint_;
  std::unique_ptr<grpc_event_engine_endpoint> eeep_;
  std::atomic<int64_t> refs_{1};
  std::atomic<int64_t> shutdown_ref_{1};
  grpc_core::Mutex mu_;
  std::string peer_address_;
  std::string local_address_;
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

  eeep->wrapper->Ref();
  EventEngine::Endpoint::ReadArgs read_args = {min_progress_size};

  // TODO(vigneshbabu): Use SliceBufferCast<> here.
  SliceBuffer* read_buffer = new (&eeep->read_buffer)
      SliceBuffer(SliceBuffer::TakeCSliceBuffer(*slices));
  read_buffer->Clear();
  eeep->wrapper->Read(
      [eeep, cb, slices](absl::Status status) {
        auto* read_buffer = reinterpret_cast<SliceBuffer*>(&eeep->read_buffer);
        grpc_slice_buffer_move_into(read_buffer->c_slice_buffer(), slices);
        read_buffer->~SliceBuffer();
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          size_t i;
          gpr_log(GPR_INFO, "TCP: %p READ (peer=%s) error=%s", eeep->wrapper,
                  std::string(eeep->wrapper->PeerAddress()).c_str(),
                  status.ToString().c_str());
          if (gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
            for (i = 0; i < slices->count; i++) {
              char* dump = grpc_dump_slice(slices->slices[i],
                                           GPR_DUMP_HEX | GPR_DUMP_ASCII);
              gpr_log(GPR_DEBUG, "READ DATA: %s", dump);
              gpr_free(dump);
            }
          }
        }
        {
          grpc_core::ApplicationCallbackExecCtx app_ctx;
          grpc_core::ExecCtx exec_ctx;
          grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, status);
        }
        // For the ref taken in EndpointRead
        eeep->wrapper->Unref();
      },
      read_buffer, &read_args);

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

  eeep->wrapper->Ref();
  EventEngine::Endpoint::WriteArgs write_args = {arg, max_frame_size};
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    size_t i;
    gpr_log(GPR_INFO, "TCP: %p WRITE (peer=%s)", eeep->wrapper,
            std::string(eeep->wrapper->PeerAddress()).c_str());
    if (gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
      for (i = 0; i < slices->count; i++) {
        char* dump =
            grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
        gpr_log(GPR_DEBUG, "WRITE DATA: %s", dump);
        gpr_free(dump);
      }
    }
  }

  // TODO(vigneshbabu): Use SliceBufferCast<> here.
  SliceBuffer* write_buffer = new (&eeep->write_buffer)
      SliceBuffer(SliceBuffer::TakeCSliceBuffer(*slices));
  eeep->wrapper->Write(
      [eeep, cb](absl::Status status) {
        auto* write_buffer =
            reinterpret_cast<SliceBuffer*>(&eeep->write_buffer);
        write_buffer->~SliceBuffer();
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO, "TCP: %p WRITE (peer=%s) error=%s", eeep->wrapper,
                  std::string(eeep->wrapper->PeerAddress()).c_str(),
                  status.ToString().c_str());
        }
        {
          grpc_core::ApplicationCallbackExecCtx app_ctx;
          grpc_core::ExecCtx exec_ctx;
          grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, status);
        }
        // For the ref taken in EndpointWrite
        eeep->wrapper->Unref();
      },
      write_buffer, &write_args);

  eeep->wrapper->ShutdownUnref();
}

void EndpointAddToPollset(grpc_endpoint* /* ep */,
                          grpc_pollset* /* pollset */) {}
void EndpointAddToPollsetSet(grpc_endpoint* /* ep */,
                             grpc_pollset_set* /* pollset */) {}
void EndpointDeleteFromPollsetSet(grpc_endpoint* /* ep */,
                                  grpc_pollset_set* /* pollset */) {}
/// After shutdown, all endpoint operations except destroy are no-op,
/// and will return some kind of sane default (empty strings, nullptrs, etc).
/// It is the caller's responsibility to ensure that calls to EndpointShutdown
/// are synchronized.
void EndpointShutdown(grpc_endpoint* ep, grpc_error_handle why) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP Endpoint %p shutdown why=%s", eeep->wrapper,
            why.ToString().c_str());
  }
  GRPC_EVENT_ENGINE_TRACE("EventEngine::Endpoint %p Shutdown:%s", eeep->wrapper,
                          why.ToString().c_str());
  eeep->wrapper->TriggerShutdown();
}

// Attempts to free the underlying data structures.
void EndpointDestroy(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  eeep->wrapper->Unref();
  GRPC_EVENT_ENGINE_TRACE("EventEngine::Endpoint %p Destroy", eeep->wrapper);
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

bool EndpointCanTrackErr(grpc_endpoint* /* ep */) { return false; }

grpc_endpoint_vtable grpc_event_engine_endpoint_vtable = {
    EndpointRead,
    EndpointWrite,
    EndpointAddToPollset,
    EndpointAddToPollsetSet,
    EndpointDeleteFromPollsetSet,
    EndpointShutdown,
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
  auto local_addr = ResolvedAddressToURI(endpoint_->GetLocalAddress());
  if (local_addr.ok()) {
    local_address_ = *local_addr;
  }
  auto peer_addr = ResolvedAddressToURI(endpoint_->GetPeerAddress());
  if (peer_addr.ok()) {
    peer_address_ = *peer_addr;
  }
#ifdef GRPC_POSIX_SOCKET_TCP
  fd_ = reinterpret_cast<PosixEndpointWithFdSupport*>(endpoint_.get())
            ->GetWrappedFd();
#else   // GRPC_POSIX_SOCKET_TCP
  fd_ = -1;
#endif  // GRPC_POSIX_SOCKET_TCP
  GRPC_EVENT_ENGINE_TRACE("EventEngine::Endpoint %p Create", eeep_->wrapper);
}

}  // namespace

grpc_endpoint* grpc_event_engine_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint) {
  GPR_DEBUG_ASSERT(ee_endpoint != nullptr);
  auto wrapper = new EventEngineEndpointWrapper(std::move(ee_endpoint));
  return wrapper->GetGrpcEndpoint();
}

}  // namespace experimental
}  // namespace grpc_event_engine
