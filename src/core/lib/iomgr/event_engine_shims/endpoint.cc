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

#include <grpc/impl/codegen/slice.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/resolved_address.h"
#include "src/core/lib/transport/error_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_event_engine {
namespace experimental {

namespace {

constexpr int64_t kShutdownBit = (static_cast<int64_t>(1) << 32);

class EventEngineEndpointWrapper : std::enable_shared_from_this<EventEngine> {
 public:
  struct grpc_event_engine_endpoint {
    grpc_endpoint base;
    EventEngineEndpointWrapper* endpoint_wrapper;
    std::string peer_address;
    std::string local_address;
    std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
        read_buffer;
    std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
        write_buffer;
  };

  explicit EventEngineEndpointWrapper(
      std::unique_ptr<EventEngine::Endpoint> endpoint);

  void Ref() { refs_.fetch_add(1, std::memory_order_acq_rel); }
  void Unref() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  grpc_endpoint* GetGrpcEndpoint() { return &eeep_->base; }

  EventEngine::Endpoint* GetEEEndpoint() { return endpoint_.get(); }

  bool ShutdownRef() {
    int64_t curr = shutdown_ref_.load(std::memory_order_acquire);
    while (true) {
      if (curr & kShutdownBit) {
        return false;
      }
      if (shutdown_ref_.compare_exchange_strong(curr, curr + 1,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  void ShutdownUnref() {
    if (shutdown_ref_.fetch_sub(1, std::memory_order_acq_rel) ==
        kShutdownBit + 1) {
      endpoint_.reset();
      // For the Ref taken in TriggerShutdown
      Unref();
    }
  }

  void TriggerShutdown() {
    int64_t curr = shutdown_ref_.load(std::memory_order_acquire);
    while (true) {
      if (curr & kShutdownBit) {
        return;
      }
      if (shutdown_ref_.compare_exchange_strong(curr, curr | kShutdownBit,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        Ref();
        if (shutdown_ref_.fetch_sub(1, std::memory_order_acq_rel) ==
            kShutdownBit + 1) {
          endpoint_.reset();
          Unref();
        }
        return;
      }
    }
  }

 private:
  std::atomic<int64_t> refs_{1};
  std::atomic<int64_t> shutdown_ref_{1};
  std::unique_ptr<EventEngine::Endpoint> endpoint_;
  std::unique_ptr<grpc_event_engine_endpoint> eeep_;
};

void EndpointRead(grpc_endpoint* ep, grpc_slice_buffer* slices,
                  grpc_closure* cb, bool /* urgent */, int min_progress_size) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (!eeep->endpoint_wrapper->ShutdownRef()) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError());
    return;
  }

  eeep->endpoint_wrapper->Ref();
  EventEngine::Endpoint::ReadArgs read_args = {min_progress_size};
  SliceBuffer* read_buffer = new (&eeep->read_buffer) SliceBuffer(slices);
  eeep->endpoint_wrapper->GetEEEndpoint()->Read(
      [eeep, cb, slices](absl::Status status) {
        auto* read_buffer = reinterpret_cast<SliceBuffer*>(&eeep->read_buffer);
        grpc_slice_buffer_swap(slices, read_buffer->c_slice_buffer());
        read_buffer->~SliceBuffer();
        {
          grpc_core::ExecCtx exec_ctx;
          grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                                  absl_status_to_grpc_error(status));
        }
        // For the ref taken in EndpointRead
        eeep->endpoint_wrapper->Unref();
      },
      read_buffer, &read_args);

  eeep->endpoint_wrapper->ShutdownUnref();
}

void EndpointWrite(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, void* arg, int max_frame_size) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (!eeep->endpoint_wrapper->ShutdownRef()) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError());
    return;
  }

  eeep->endpoint_wrapper->Ref();
  EventEngine::Endpoint::WriteArgs write_args = {arg, max_frame_size};
  SliceBuffer* write_buffer = new (&eeep->write_buffer) SliceBuffer(slices);
  eeep->endpoint_wrapper->GetEEEndpoint()->Write(
      [eeep, cb](absl::Status status) {
        auto* write_buffer =
            reinterpret_cast<SliceBuffer*>(&eeep->write_buffer);
        write_buffer->~SliceBuffer();
        {
          grpc_core::ExecCtx exec_ctx;
          grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                                  absl_status_to_grpc_error(status));
        }
        // For the ref taken in EndpointWrite
        eeep->endpoint_wrapper->Unref();
      },
      write_buffer, &write_args);

  eeep->endpoint_wrapper->ShutdownUnref();
}

void EndpointAddToPollset(grpc_endpoint* /* ep */,
                          grpc_pollset* /* pollset */) {}
void EndpointAddToPollsetSet(grpc_endpoint* /* ep */,
                             grpc_pollset_set* /* pollset */) {}
void EndpointDeleteFromPollsetSet(grpc_endpoint* /* ep */,
                                  grpc_pollset_set* /* pollset */) {}
/// After shutdown, all endpoint operations except destroy are no-op,
/// and will return some kind of sane default (empty strings, nullptrs, etc). It
/// is the caller's responsibility to ensure that calls to EndpointShutdown are
/// synchronized.
void EndpointShutdown(grpc_endpoint* ep, grpc_error_handle why) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP Endpoint %p shutdown why=%s",
            eeep->endpoint_wrapper->GetEEEndpoint(), why.ToString().c_str());
  }
  eeep->endpoint_wrapper->TriggerShutdown();
}

void EndpointDestroy(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  eeep->endpoint_wrapper->Unref();
}

absl::string_view EndpointGetPeerAddress(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (!eeep->endpoint_wrapper->ShutdownRef()) {
    return "";
  }
  eeep->endpoint_wrapper->ShutdownUnref();
  return eeep->peer_address;
}

absl::string_view EndpointGetLocalAddress(grpc_endpoint* ep) {
  auto* eeep =
      reinterpret_cast<EventEngineEndpointWrapper::grpc_event_engine_endpoint*>(
          ep);
  if (!eeep->endpoint_wrapper->ShutdownRef()) {
    return "";
  }
  eeep->endpoint_wrapper->ShutdownUnref();
  return eeep->local_address;
}

int EndpointGetFd(grpc_endpoint* /* ep */) { return -1; }

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
  eeep_->endpoint_wrapper = this;
  auto local_addr = ResolvedAddressToURI(endpoint_->GetLocalAddress());
  if (local_addr.ok()) {
    eeep_->local_address = local_addr.value();
  }
  auto peer_addr = ResolvedAddressToURI(endpoint_->GetPeerAddress());
  if (peer_addr.ok()) {
    eeep_->peer_address = peer_addr.value();
  }
}

}  // namespace

grpc_endpoint* grpc_event_engine_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint) {
  GPR_DEBUG_ASSERT(ee_endpoint != nullptr);
  auto endpoint_wrapper =
      new EventEngineEndpointWrapper(std::move(ee_endpoint));
  return endpoint_wrapper->GetGrpcEndpoint();
}

}  // namespace experimental
}  // namespace grpc_event_engine
