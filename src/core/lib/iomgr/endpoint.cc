//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/core/lib/iomgr/endpoint.h"

#include <grpc/support/port_platform.h>

void grpc_endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                        grpc_closure* cb, bool urgent, int min_progress_size) {
  ep->vtable->read(ep, slices, cb, urgent, min_progress_size);
}

void grpc_endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                         grpc_closure* cb, void* arg, int max_frame_size) {
  ep->vtable->write(ep, slices, cb, arg, max_frame_size);
}

void grpc_endpoint_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  ep->vtable->add_to_pollset(ep, pollset);
}

void grpc_endpoint_add_to_pollset_set(grpc_endpoint* ep,
                                      grpc_pollset_set* pollset_set) {
  ep->vtable->add_to_pollset_set(ep, pollset_set);
}

void grpc_endpoint_delete_from_pollset_set(grpc_endpoint* ep,
                                           grpc_pollset_set* pollset_set) {
  ep->vtable->delete_from_pollset_set(ep, pollset_set);
}

void grpc_endpoint_destroy(grpc_endpoint* ep) { ep->vtable->destroy(ep); }

absl::string_view grpc_endpoint_get_peer(grpc_endpoint* ep) {
  return ep->vtable->get_peer(ep);
}

absl::string_view grpc_endpoint_get_local_address(grpc_endpoint* ep) {
  return ep->vtable->get_local_address(ep);
}

int grpc_endpoint_get_fd(grpc_endpoint* ep) { return ep->vtable->get_fd(ep); }

bool grpc_endpoint_can_track_err(grpc_endpoint* ep) {
  return ep->vtable->can_track_err(ep);
}
