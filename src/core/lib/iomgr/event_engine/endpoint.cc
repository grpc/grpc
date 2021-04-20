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

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine/util.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resource_quota.h"

namespace {
using ::grpc_event_engine::experimental::EventEngine;

static void endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                          grpc_closure* cb, bool /* urgent */) {
  // TODO(nnoble): provide way to convert slices to SliceBuffer.
  // auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  // eeep->endpoint->Read(event_engine_closure_to_on_connect_callback(cb),
  // slices);
  (void)slices;
  (void)cb;
  (void)ep;
}
static void endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                           grpc_closure* cb, void* arg) {
  // TODO(nnoble): provide way to convert slices to SliceBuffer.
  // auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  // eeep->endpoint->Write(event_engine_closure_to_callback(cb, arg), slices);
  (void)ep;
  (void)slices;
  (void)cb;
  (void)arg;
}
static void endpoint_add_to_pollset(grpc_endpoint* /* ep */,
                                    grpc_pollset* /* pollset */) {}
static void endpoint_add_to_pollset_set(grpc_endpoint* /* ep */,
                                        grpc_pollset_set* /* pollset */) {}
static void endpoint_delete_from_pollset_set(grpc_endpoint* /* ep */,
                                             grpc_pollset_set* /* pollset */) {}
static void endpoint_shutdown(grpc_endpoint* ep, grpc_error* why) {
  (void)ep;
  (void)why;
  // TODO(hork): do we need Endpoint shutdown?
}

static void endpoint_destroy(grpc_endpoint* ep) {
  (void)ep;
  // TODO(hork): Look into if we need TCP_UNREF. shared_ptr work?
}

static grpc_resource_user* endpoint_get_resource_user(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  return eeep->ru;
}

static absl::string_view endpoint_get_peer(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  return eeep->peer_string;
}

static absl::string_view endpoint_get_local_address(grpc_endpoint* ep) {
  // TODO(hork): need to convert ResolvedAddress <-> String
  // auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  // return eeep->endpoint->GetLocalAddress();
  (void)ep;
  return "TEMP";
}

static int endpoint_get_fd(grpc_endpoint* /* ep */) { return -1; }

static bool endpoint_can_track_err(grpc_endpoint* /* ep */) { return false; }

}  // namespace

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

grpc_endpoint* grpc_tcp_create(const grpc_channel_args* channel_args,
                               absl::string_view peer_string) {
  auto endpoint = new grpc_event_engine_endpoint();
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
  // TODO(hork): what should the string be?
  endpoint->ru = grpc_resource_user_create(resource_quota, "UNIMPLEMENTED");
  return &endpoint->base;
}

#endif
