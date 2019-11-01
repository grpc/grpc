/*
 *
 * Copyright 2016 gRPC authors.
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

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"

#include "test/core/util/mock_endpoint.h"

#include <inttypes.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/sockaddr.h"

typedef struct mock_endpoint {
  grpc_endpoint base;
  gpr_mu mu;
  void (*on_write)(grpc_slice slice);
  grpc_slice_buffer read_buffer;
  grpc_slice_buffer* on_read_out;
  grpc_closure* on_read;
  grpc_resource_user* resource_user;
} mock_endpoint;

static void me_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, bool /*urgent*/) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  gpr_mu_lock(&m->mu);
  if (m->read_buffer.count > 0) {
    grpc_slice_buffer_swap(&m->read_buffer, slices);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
  } else {
    m->on_read = cb;
    m->on_read_out = slices;
  }
  gpr_mu_unlock(&m->mu);
}

static void me_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* /*arg*/) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  for (size_t i = 0; i < slices->count; i++) {
    m->on_write(slices->slices[i]);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
}

static void me_add_to_pollset(grpc_endpoint* /*ep*/,
                              grpc_pollset* /*pollset*/) {}

static void me_add_to_pollset_set(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}

static void me_delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                       grpc_pollset_set* /*pollset*/) {}

static void me_shutdown(grpc_endpoint* ep, grpc_error* why) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  gpr_mu_lock(&m->mu);
  if (m->on_read) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->on_read,
                            GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                "Endpoint Shutdown", &why, 1));
    m->on_read = nullptr;
  }
  gpr_mu_unlock(&m->mu);
  grpc_resource_user_shutdown(m->resource_user);
  GRPC_ERROR_UNREF(why);
}

static void me_destroy(grpc_endpoint* ep) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  grpc_slice_buffer_destroy(&m->read_buffer);
  grpc_resource_user_unref(m->resource_user);
  gpr_mu_destroy(&m->mu);
  gpr_free(m);
}

static char* me_get_peer(grpc_endpoint* /*ep*/) {
  return gpr_strdup("fake:mock_endpoint");
}

static grpc_resource_user* me_get_resource_user(grpc_endpoint* ep) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  return m->resource_user;
}

static int me_get_fd(grpc_endpoint* /*ep*/) { return -1; }

static bool me_can_track_err(grpc_endpoint* /*ep*/) { return false; }

static const grpc_endpoint_vtable vtable = {me_read,
                                            me_write,
                                            me_add_to_pollset,
                                            me_add_to_pollset_set,
                                            me_delete_from_pollset_set,
                                            me_shutdown,
                                            me_destroy,
                                            me_get_resource_user,
                                            me_get_peer,
                                            me_get_fd,
                                            me_can_track_err};

grpc_endpoint* grpc_mock_endpoint_create(void (*on_write)(grpc_slice slice),
                                         grpc_resource_quota* resource_quota) {
  mock_endpoint* m = static_cast<mock_endpoint*>(gpr_malloc(sizeof(*m)));
  m->base.vtable = &vtable;
  char* name;
  gpr_asprintf(&name, "mock_endpoint_%" PRIxPTR, (intptr_t)m);
  m->resource_user = grpc_resource_user_create(resource_quota, name);
  gpr_free(name);
  grpc_slice_buffer_init(&m->read_buffer);
  gpr_mu_init(&m->mu);
  m->on_write = on_write;
  m->on_read = nullptr;
  return &m->base;
}

void grpc_mock_endpoint_put_read(grpc_endpoint* ep, grpc_slice slice) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  gpr_mu_lock(&m->mu);
  if (m->on_read != nullptr) {
    grpc_slice_buffer_add(m->on_read_out, slice);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->on_read, GRPC_ERROR_NONE);
    m->on_read = nullptr;
  } else {
    grpc_slice_buffer_add(&m->read_buffer, slice);
  }
  gpr_mu_unlock(&m->mu);
}
