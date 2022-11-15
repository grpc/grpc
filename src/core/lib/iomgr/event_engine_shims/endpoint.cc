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

#include "grpc/impl/codegen/slice.h"
#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/resolved_address.h"
#include "src/core/lib/transport/error_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_event_engine {
namespace experimental {

namespace {

struct grpc_event_engine_endpoint {
  grpc_endpoint base;
  std::unique_ptr<EventEngine::Endpoint> endpoint;
  std::atomic<int> refcount;
  grpc_core::Mutex shutdown_mu;
  bool shutting_down;
  std::atomic<int> shutdown_count;
  std::string peer_address;
  std::string local_address;
  std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
      read_buffer;
  std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
      write_buffer;
};

void EndpointRef(grpc_event_engine_endpoint* ep) {
  ep->refcount.fetch_add(1, std::memory_order_acq_rel);
}

void EndpointUnref(grpc_event_engine_endpoint* ep) {
  if (ep->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete ep;
  }
}

void EndpointRead(grpc_endpoint* ep, grpc_slice_buffer* slices,
                  grpc_closure* cb, bool /* urgent */, int min_progress_size) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  bool is_active = true;
  {
    grpc_core::MutexLock lock(&eeep->shutdown_mu);
    is_active = !eeep->shutting_down &&
                (eeep->shutdown_count.fetch_add(1, std::memory_order_acq_rel));
  }

  if (!is_active) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError());
    return;
  }

  EndpointRef(eeep);
  EventEngine::Endpoint::ReadArgs read_args = {min_progress_size};
  SliceBuffer* read_buffer = new (&eeep->read_buffer) SliceBuffer(slices);
  eeep->endpoint->Read(
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
        EndpointUnref(eeep);
      },
      read_buffer, &read_args);

  if (eeep->shutdown_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    eeep->endpoint.reset();
    // For the ref taken in EndpointShutdown
    EndpointUnref(eeep);
  }
}

void EndpointWrite(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, void* arg, int max_frame_size) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  bool is_active = true;
  {
    grpc_core::MutexLock lock(&eeep->shutdown_mu);
    is_active = !eeep->shutting_down &&
                (eeep->shutdown_count.fetch_add(1, std::memory_order_acq_rel));
  }

  if (!is_active) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError());
    return;
  }
  EndpointRef(eeep);
  EventEngine::Endpoint::WriteArgs write_args = {arg, max_frame_size};
  SliceBuffer* write_buffer = new (&eeep->write_buffer) SliceBuffer(slices);
  eeep->endpoint->Write(
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
        EndpointUnref(eeep);
      },
      write_buffer, &write_args);

  if (eeep->shutdown_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    eeep->endpoint.reset();
    // For the ref taken in EndpointShutdown
    EndpointUnref(eeep);
  }
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
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP Endpoint %p shutdown why=%s", eeep->endpoint.get(),
            why.ToString().c_str());
  }
  bool done = false;
  {
    grpc_core::MutexLock lock(&eeep->shutdown_mu);
    if (!eeep->shutting_down) {
      eeep->shutting_down = true;
      if (eeep->shutdown_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        done = true;
      } else {
        EndpointRef(eeep);
      }
    }
  }
  if (done) {
    eeep->endpoint.reset();
  }
}

void EndpointDestroy(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  EndpointUnref(eeep);
}

absl::string_view EndpointGetPeerAddress(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    return "";
  }
  if (eeep->peer_address.empty()) {
    auto addr = ResolvedAddressToURI(eeep->endpoint->GetPeerAddress());
    if (addr.ok()) {
      eeep->peer_address = addr.value();
    }
  }
  return eeep->peer_address;
}

absl::string_view EndpointGetLocalAddress(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    return "";
  }
  if (eeep->local_address.empty()) {
    auto addr = ResolvedAddressToURI(eeep->endpoint->GetLocalAddress());
    if (addr.ok()) {
      eeep->local_address = addr.value();
    }
  }
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

}  // namespace

grpc_endpoint* grpc_event_engine_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint) {
  auto endpoint = new grpc_event_engine_endpoint;
  endpoint->base.vtable = &grpc_event_engine_endpoint_vtable;
  endpoint->endpoint = std::move(ee_endpoint);
  endpoint->refcount.store(1, std::memory_order_relaxed);
  endpoint->shutdown_count.store(1, std::memory_order_relaxed);
  endpoint->shutting_down = false;
  return &endpoint->base;
}

}  // namespace experimental
}  // namespace grpc_event_engine
