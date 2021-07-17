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

#include "test/core/util/passthru_endpoint.h"

#include <inttypes.h>
#include <string.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/sockaddr.h"

#include "src/core/lib/slice/slice_internal.h"

typedef struct passthru_endpoint passthru_endpoint;

typedef struct {
  grpc_endpoint base;
  passthru_endpoint* parent;
  grpc_slice_buffer read_buffer;
  grpc_slice_buffer* on_read_out;
  grpc_closure* on_read;
  grpc_resource_user* resource_user;
} half;

struct passthru_endpoint {
  gpr_mu mu;
  int halves;
  grpc_passthru_endpoint_stats* stats;
  bool shutdown;
  half client;
  half server;
};

static void me_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, bool /*urgent*/) {
  half* m = reinterpret_cast<half*>(ep);
  gpr_mu_lock(&m->parent->mu);
  if (m->parent->shutdown) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already shutdown"));
  } else if (m->read_buffer.count > 0) {
    grpc_slice_buffer_swap(&m->read_buffer, slices);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
  } else {
    m->on_read = cb;
    m->on_read_out = slices;
  }
  gpr_mu_unlock(&m->parent->mu);
}

static half* other_half(half* h) {
  if (h == &h->parent->client) return &h->parent->server;
  return &h->parent->client;
}

static void me_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* /*arg*/) {
  half* m = other_half(reinterpret_cast<half*>(ep));
  gpr_mu_lock(&m->parent->mu);
  grpc_error_handle error = GRPC_ERROR_NONE;
  gpr_atm_no_barrier_fetch_add(&m->parent->stats->num_writes, (gpr_atm)1);
  if (m->parent->shutdown) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Endpoint already shutdown");
  } else if (m->on_read != nullptr) {
    for (size_t i = 0; i < slices->count; i++) {
      grpc_slice_buffer_add(m->on_read_out, grpc_slice_copy(slices->slices[i]));
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->on_read, GRPC_ERROR_NONE);
    m->on_read = nullptr;
  } else {
    for (size_t i = 0; i < slices->count; i++) {
      grpc_slice_buffer_add(&m->read_buffer,
                            grpc_slice_copy(slices->slices[i]));
    }
  }
  gpr_mu_unlock(&m->parent->mu);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

static void me_add_to_pollset(grpc_endpoint* /*ep*/,
                              grpc_pollset* /*pollset*/) {}

static void me_add_to_pollset_set(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}

static void me_delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                       grpc_pollset_set* /*pollset*/) {}

static void me_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  half* m = reinterpret_cast<half*>(ep);
  gpr_mu_lock(&m->parent->mu);
  m->parent->shutdown = true;
  if (m->on_read) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, m->on_read,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Shutdown", &why, 1));
    m->on_read = nullptr;
  }
  m = other_half(m);
  if (m->on_read) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, m->on_read,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Shutdown", &why, 1));
    m->on_read = nullptr;
  }
  gpr_mu_unlock(&m->parent->mu);
  grpc_resource_user_shutdown(m->resource_user);
  GRPC_ERROR_UNREF(why);
}

static void me_destroy(grpc_endpoint* ep) {
  passthru_endpoint* p = (reinterpret_cast<half*>(ep))->parent;
  gpr_mu_lock(&p->mu);
  if (0 == --p->halves) {
    gpr_mu_unlock(&p->mu);
    gpr_mu_destroy(&p->mu);
    grpc_passthru_endpoint_stats_destroy(p->stats);
    grpc_slice_buffer_destroy_internal(&p->client.read_buffer);
    grpc_slice_buffer_destroy_internal(&p->server.read_buffer);
    grpc_resource_user_unref(p->client.resource_user);
    grpc_resource_user_unref(p->server.resource_user);
    gpr_free(p);
  } else {
    gpr_mu_unlock(&p->mu);
  }
}

static absl::string_view me_get_peer(grpc_endpoint* ep) {
  passthru_endpoint* p = (reinterpret_cast<half*>(ep))->parent;
  return (reinterpret_cast<half*>(ep)) == &p->client
             ? "fake:mock_client_endpoint"
             : "fake:mock_server_endpoint";
}

static absl::string_view me_get_local_address(grpc_endpoint* ep) {
  passthru_endpoint* p = (reinterpret_cast<half*>(ep))->parent;
  return (reinterpret_cast<half*>(ep)) == &p->client
             ? "fake:mock_client_endpoint"
             : "fake:mock_server_endpoint";
}

static int me_get_fd(grpc_endpoint* /*ep*/) { return -1; }

static bool me_can_track_err(grpc_endpoint* /*ep*/) { return false; }

static grpc_resource_user* me_get_resource_user(grpc_endpoint* ep) {
  half* m = reinterpret_cast<half*>(ep);
  return m->resource_user;
}

static const grpc_endpoint_vtable vtable = {
    me_read,
    me_write,
    me_add_to_pollset,
    me_add_to_pollset_set,
    me_delete_from_pollset_set,
    me_shutdown,
    me_destroy,
    me_get_resource_user,
    me_get_peer,
    me_get_local_address,
    me_get_fd,
    me_can_track_err,
};

static void half_init(half* m, passthru_endpoint* parent,
                      grpc_resource_quota* resource_quota,
                      const char* half_name) {
  m->base.vtable = &vtable;
  m->parent = parent;
  grpc_slice_buffer_init(&m->read_buffer);
  m->on_read = nullptr;
  std::string name =
      absl::StrFormat("passthru_endpoint_%s_%p", half_name, parent);
  m->resource_user = grpc_resource_user_create(resource_quota, name.c_str());
}

void grpc_passthru_endpoint_create(grpc_endpoint** client,
                                   grpc_endpoint** server,
                                   grpc_resource_quota* resource_quota,
                                   grpc_passthru_endpoint_stats* stats) {
  passthru_endpoint* m =
      static_cast<passthru_endpoint*>(gpr_malloc(sizeof(*m)));
  m->halves = 2;
  m->shutdown = false;
  if (stats == nullptr) {
    m->stats = grpc_passthru_endpoint_stats_create();
  } else {
    gpr_ref(&stats->refs);
    m->stats = stats;
  }
  half_init(&m->client, m, resource_quota, "client");
  half_init(&m->server, m, resource_quota, "server");
  gpr_mu_init(&m->mu);
  *client = &m->client.base;
  *server = &m->server.base;
}

grpc_passthru_endpoint_stats* grpc_passthru_endpoint_stats_create() {
  grpc_passthru_endpoint_stats* stats =
      static_cast<grpc_passthru_endpoint_stats*>(
          gpr_malloc(sizeof(grpc_passthru_endpoint_stats)));
  memset(stats, 0, sizeof(*stats));
  gpr_ref_init(&stats->refs, 1);
  return stats;
}

void grpc_passthru_endpoint_stats_destroy(grpc_passthru_endpoint_stats* stats) {
  if (gpr_unref(&stats->refs)) {
    gpr_free(stats);
  }
}
