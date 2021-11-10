// Copyright 2021 The gRPC Authors
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

#ifdef GRPC_USE_EVENT_ENGINE
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine/closure.h"
#include "src/core/lib/iomgr/event_engine/endpoint.h"
#include "src/core/lib/iomgr/event_engine/pollset.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/transport/error_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::ResolvedAddressToURI;
using ::grpc_event_engine::experimental::SliceBuffer;

void endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool /* urgent */) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_CANCELLED);
    return;
  }
  SliceBuffer* read_buffer = new (&eeep->read_buffer) SliceBuffer(slices);
  eeep->endpoint->Read(
      [eeep, cb](absl::Status status) {
        auto* read_buffer = reinterpret_cast<SliceBuffer*>(&eeep->read_buffer);
        read_buffer->~SliceBuffer();
        grpc_core::ExecCtx exec_ctx;
        grpc_core::Closure::Run(DEBUG_LOCATION, cb,
                                absl_status_to_grpc_error(status));
        exec_ctx.Flush();
        grpc_pollset_ee_broadcast_event();
      },
      read_buffer);
}

void endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, void* arg) {
  // TODO(hork): adapt arg to some metrics collection mechanism.
  (void)arg;
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_CANCELLED);
    return;
  }
  SliceBuffer* write_buffer = new (&eeep->write_buffer) SliceBuffer(slices);
  eeep->endpoint->Write(
      [eeep, cb](absl::Status status) {
        auto* write_buffer =
            reinterpret_cast<SliceBuffer*>(&eeep->write_buffer);
        write_buffer->~SliceBuffer();
        grpc_core::ExecCtx exec_ctx;
        grpc_core::Closure::Run(DEBUG_LOCATION, cb,
                                absl_status_to_grpc_error(status));
        exec_ctx.Flush();
        grpc_pollset_ee_broadcast_event();
      },
      write_buffer);
}
void endpoint_add_to_pollset(grpc_endpoint* /* ep */,
                             grpc_pollset* /* pollset */) {}
void endpoint_add_to_pollset_set(grpc_endpoint* /* ep */,
                                 grpc_pollset_set* /* pollset */) {}
void endpoint_delete_from_pollset_set(grpc_endpoint* /* ep */,
                                      grpc_pollset_set* /* pollset */) {}
/// After shutdown, all endpoint operations except destroy are no-op,
/// and will return some kind of sane default (empty strings, nullptrs, etc). It
/// is the caller's responsibility to ensure that calls to endpoint_shutdown are
/// synchronized.
void endpoint_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    std::string str = grpc_error_std_string(why);
    gpr_log(GPR_INFO, "TCP Endpoint %p shutdown why=%s", eeep->endpoint.get(),
            str.c_str());
  }
  eeep->endpoint.reset();
}

void endpoint_destroy(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  delete eeep;
}

absl::string_view endpoint_get_peer(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    return "";
  }
  if (eeep->peer_address.empty()) {
    const EventEngine::ResolvedAddress& addr = eeep->endpoint->GetPeerAddress();
    eeep->peer_address = ResolvedAddressToURI(addr);
  }
  return eeep->peer_address;
}

absl::string_view endpoint_get_local_address(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    return "";
  }
  if (eeep->local_address.empty()) {
    const EventEngine::ResolvedAddress& addr =
        eeep->endpoint->GetLocalAddress();
    eeep->local_address = ResolvedAddressToURI(addr);
  }
  return eeep->local_address;
}

int endpoint_get_fd(grpc_endpoint* /* ep */) { return -1; }

bool endpoint_can_track_err(grpc_endpoint* /* ep */) { return false; }

grpc_endpoint_vtable grpc_event_engine_endpoint_vtable = {
    endpoint_read,
    endpoint_write,
    endpoint_add_to_pollset,
    endpoint_add_to_pollset_set,
    endpoint_delete_from_pollset_set,
    endpoint_shutdown,
    endpoint_destroy,
    endpoint_get_peer,
    endpoint_get_local_address,
    endpoint_get_fd,
    endpoint_can_track_err};

}  // namespace

grpc_event_engine_endpoint* grpc_tcp_server_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint) {
  auto endpoint = new grpc_event_engine_endpoint;
  endpoint->base.vtable = &grpc_event_engine_endpoint_vtable;
  endpoint->endpoint = std::move(ee_endpoint);
  return endpoint;
}

grpc_endpoint* grpc_tcp_create(const grpc_channel_args* /* channel_args */,
                               absl::string_view /* peer_address */) {
  auto endpoint = new grpc_event_engine_endpoint;
  endpoint->base.vtable = &grpc_event_engine_endpoint_vtable;
  return &endpoint->base;
}

#endif  // GRPC_USE_EVENT_ENGINE
