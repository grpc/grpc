/*
 *
 * Copyright 2021 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#if defined(GRPC_EVENT_ENGINE_TEST)

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"
#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine/util.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resource_quota.h"

static void endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                          grpc_closure* cb, bool urgent) {
  (void)ep;
  (void)slices;
  (void)cb;
  (void)urgent;
}
static void endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                           grpc_closure* cb, void* arg) {
  (void)ep;
  (void)slices;
  (void)cb;
  (void)arg;
}
static void endpoint_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  (void)ep;
  (void)pollset;
}
static void endpoint_add_to_pollset_set(grpc_endpoint* ep,
                                        grpc_pollset_set* pollset) {
  (void)ep;
  (void)pollset;
}
static void endpoint_delete_from_pollset_set(grpc_endpoint* ep,
                                             grpc_pollset_set* pollset) {
  (void)ep;
  (void)pollset;
}
static void endpoint_shutdown(grpc_endpoint* ep, grpc_error* why) {
  (void)ep;
  (void)why;
}
static void endpoint_destroy(grpc_endpoint* ep) { (void)ep; }
static grpc_resource_user* endpoint_get_resource_user(grpc_endpoint* ep) {
  (void)ep;
  return nullptr;
}
static absl::string_view endpoint_get_peer(grpc_endpoint* ep) {
  (void)ep;
  return "TEMP";
}
static absl::string_view endpoint_get_local_address(grpc_endpoint* ep) {
  (void)ep;
  return "TEMP";
}
static int endpoint_get_fd(grpc_endpoint* ep) {
  (void)ep;
  return -1;
}
static bool endpoint_can_track_err(grpc_endpoint* ep) {
  (void)ep;
  return false;
}

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

#endif
