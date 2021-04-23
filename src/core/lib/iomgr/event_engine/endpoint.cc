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
#if defined(GRPC_EVENT_ENGINE_TEST)

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/event_engine/endpoint.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/time.h>
#include "absl/strings/string_view.h"

#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine/util.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resource_quota.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::ResolvedAddressToURI;

void endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool /* urgent */) {
  // TODO(nnoble): provide way to convert slices to SliceBuffer.
  // auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  // eeep->endpoint->Read(event_engine_closure_to_on_connect_callback(cb),
  // slices);
  (void)slices;
  (void)cb;
  (void)ep;
}
void endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, void* arg) {
  // TODO(nnoble): provide way to convert slices to SliceBuffer.
  // auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  // eeep->endpoint->Write(event_engine_closure_to_callback(cb, arg), slices);
  (void)ep;
  (void)slices;
  (void)cb;
  (void)arg;
}
void endpoint_add_to_pollset(grpc_endpoint* /* ep */,
                             grpc_pollset* /* pollset */) {}
void endpoint_add_to_pollset_set(grpc_endpoint* /* ep */,
                                 grpc_pollset_set* /* pollset */) {}
void endpoint_delete_from_pollset_set(grpc_endpoint* /* ep */,
                                      grpc_pollset_set* /* pollset */) {}
void endpoint_shutdown(grpc_endpoint* ep, grpc_error* why) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    const char* str = grpc_error_string(why);
    gpr_log(GPR_INFO, "TCP Endpoint %p shutdown why=%s", eeep->endpoint.get(),
            str);
  }
  grpc_resource_user_shutdown(eeep->ru);
}

void endpoint_destroy(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  grpc_resource_user_unref(eeep->ru);
  delete ep;
}

grpc_resource_user* endpoint_get_resource_user(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  return eeep->ru;
}

absl::string_view endpoint_get_peer(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  return eeep->peer_string;
}

absl::string_view endpoint_get_local_address(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  const EventEngine::ResolvedAddress* addr = eeep->endpoint->GetLocalAddress();
  GPR_ASSERT(addr != nullptr);
  return ResolvedAddressToURI(*addr);
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
    endpoint_get_resource_user,
    endpoint_get_peer,
    endpoint_get_local_address,
    endpoint_get_fd,
    endpoint_can_track_err};

}  // namespace

grpc_endpoint* grpc_tcp_create(const grpc_channel_args* channel_args,
                               absl::string_view peer_string) {
  auto endpoint = new grpc_event_engine_endpoint;
  endpoint->base.vtable = &grpc_event_engine_endpoint_vtable;
  endpoint->peer_string = std::string(peer_string);
  endpoint->local_address = "";
  grpc_resource_quota* resource_quota = grpc_resource_quota_create(nullptr);
  if (channel_args != nullptr) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(resource_quota);
        resource_quota =
            grpc_resource_quota_ref_internal(static_cast<grpc_resource_quota*>(
                channel_args->args[i].value.pointer.p));
      }
    }
  }
  endpoint->ru =
      grpc_resource_user_create(resource_quota, endpoint->peer_string.c_str());
  return &endpoint->base;
}

#endif
