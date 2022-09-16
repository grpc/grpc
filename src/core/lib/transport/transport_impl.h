/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_TRANSPORT_TRANSPORT_IMPL_H
#define GRPC_CORE_LIB_TRANSPORT_TRANSPORT_IMPL_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "absl/strings/string_view.h"

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_fwd.h"

typedef struct grpc_transport_vtable {
  /* Memory required for a single stream element - this is allocated by upper
     layers and initialized by the transport */
  size_t sizeof_stream; /* = sizeof(transport stream) */

  /* name of this transport implementation */
  const char* name;

  /* implementation of grpc_transport_init_stream */
  int (*init_stream)(grpc_transport* self, grpc_stream* stream,
                     grpc_stream_refcount* refcount, const void* server_data,
                     grpc_core::Arena* arena);

  /* Create a promise to execute one client call.
     If this is non-null, it may be used in preference to
     perform_stream_op.
     If this is used in preference to perform_stream_op, the
     following can be omitted also:
       - calling init_stream, destroy_stream, set_pollset, set_pollset_set
       - allocation of memory for call data (sizeof_stream may be ignored)
     There is an on-going migration to move all filters to providing this, and
     then to drop perform_stream_op. */
  grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> (*make_call_promise)(
      grpc_transport* self, grpc_core::ClientMetadataHandle initial_metadata);

  /* implementation of grpc_transport_set_pollset */
  void (*set_pollset)(grpc_transport* self, grpc_stream* stream,
                      grpc_pollset* pollset);

  /* implementation of grpc_transport_set_pollset */
  void (*set_pollset_set)(grpc_transport* self, grpc_stream* stream,
                          grpc_pollset_set* pollset_set);

  /* implementation of grpc_transport_perform_stream_op */
  void (*perform_stream_op)(grpc_transport* self, grpc_stream* stream,
                            grpc_transport_stream_op_batch* op);

  /* implementation of grpc_transport_perform_op */
  void (*perform_op)(grpc_transport* self, grpc_transport_op* op);

  /* implementation of grpc_transport_destroy_stream */
  void (*destroy_stream)(grpc_transport* self, grpc_stream* stream,
                         grpc_closure* then_schedule_closure);

  /* implementation of grpc_transport_destroy */
  void (*destroy)(grpc_transport* self);

  /* implementation of grpc_transport_get_endpoint */
  grpc_endpoint* (*get_endpoint)(grpc_transport* self);
} grpc_transport_vtable;

/* an instance of a grpc transport */
struct grpc_transport {
  struct RawPointerChannelArgTag {};
  static absl::string_view ChannelArgName() { return GRPC_ARG_TRANSPORT; }
  /* pointer to a vtable defining operations on this transport */
  const grpc_transport_vtable* vtable;
};

#endif /* GRPC_CORE_LIB_TRANSPORT_TRANSPORT_IMPL_H */
