/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test/core/util/passthru_endpoint.h"

#include <inttypes.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

typedef struct passthru_endpoint passthru_endpoint;

typedef struct {
  grpc_endpoint base;
  passthru_endpoint *parent;
  gpr_slice_buffer read_buffer;
  gpr_slice_buffer *on_read_out;
  grpc_closure *on_read;
  grpc_resource_user *resource_user;
} half;

struct passthru_endpoint {
  gpr_mu mu;
  int halves;
  bool shutdown;
  half client;
  half server;
};

static void me_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                    gpr_slice_buffer *slices, grpc_closure *cb) {
  half *m = (half *)ep;
  gpr_mu_lock(&m->parent->mu);
  if (m->parent->shutdown) {
    grpc_exec_ctx_sched(exec_ctx, cb, GRPC_ERROR_CREATE("Already shutdown"),
                        NULL);
  } else if (m->read_buffer.count > 0) {
    gpr_slice_buffer_swap(&m->read_buffer, slices);
    grpc_exec_ctx_sched(exec_ctx, cb, GRPC_ERROR_NONE, NULL);
  } else {
    m->on_read = cb;
    m->on_read_out = slices;
  }
  gpr_mu_unlock(&m->parent->mu);
}

static half *other_half(half *h) {
  if (h == &h->parent->client) return &h->parent->server;
  return &h->parent->client;
}

static void me_write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                     gpr_slice_buffer *slices, grpc_closure *cb) {
  half *m = other_half((half *)ep);
  gpr_mu_lock(&m->parent->mu);
  grpc_error *error = GRPC_ERROR_NONE;
  if (m->parent->shutdown) {
    error = GRPC_ERROR_CREATE("Endpoint already shutdown");
  } else if (m->on_read != NULL) {
    for (size_t i = 0; i < slices->count; i++) {
      gpr_slice_buffer_add(m->on_read_out, gpr_slice_ref(slices->slices[i]));
    }
    grpc_exec_ctx_sched(exec_ctx, m->on_read, GRPC_ERROR_NONE, NULL);
    m->on_read = NULL;
  } else {
    for (size_t i = 0; i < slices->count; i++) {
      gpr_slice_buffer_add(&m->read_buffer, gpr_slice_ref(slices->slices[i]));
    }
  }
  gpr_mu_unlock(&m->parent->mu);
  grpc_exec_ctx_sched(exec_ctx, cb, error, NULL);
}

static void me_add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                              grpc_pollset *pollset) {}

static void me_add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                  grpc_pollset_set *pollset) {}

static void me_shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  half *m = (half *)ep;
  gpr_mu_lock(&m->parent->mu);
  m->parent->shutdown = true;
  if (m->on_read) {
    grpc_exec_ctx_sched(exec_ctx, m->on_read, GRPC_ERROR_CREATE("Shutdown"),
                        NULL);
    m->on_read = NULL;
  }
  m = other_half(m);
  if (m->on_read) {
    grpc_exec_ctx_sched(exec_ctx, m->on_read, GRPC_ERROR_CREATE("Shutdown"),
                        NULL);
    m->on_read = NULL;
  }
  gpr_mu_unlock(&m->parent->mu);
  grpc_resource_user_shutdown(exec_ctx, m->resource_user);
}

static void me_destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  passthru_endpoint *p = ((half *)ep)->parent;
  gpr_mu_lock(&p->mu);
  if (0 == --p->halves) {
    gpr_mu_unlock(&p->mu);
    gpr_mu_destroy(&p->mu);
    gpr_slice_buffer_destroy(&p->client.read_buffer);
    gpr_slice_buffer_destroy(&p->server.read_buffer);
    grpc_resource_user_unref(exec_ctx, p->client.resource_user);
    grpc_resource_user_unref(exec_ctx, p->server.resource_user);
    gpr_free(p);
  } else {
    gpr_mu_unlock(&p->mu);
  }
}

static char *me_get_peer(grpc_endpoint *ep) {
  return gpr_strdup("fake:mock_endpoint");
}

static grpc_workqueue *me_get_workqueue(grpc_endpoint *ep) { return NULL; }

static grpc_resource_user *me_get_resource_user(grpc_endpoint *ep) {
  half *m = (half *)ep;
  return m->resource_user;
}

static const grpc_endpoint_vtable vtable = {
    me_read,
    me_write,
    me_get_workqueue,
    me_add_to_pollset,
    me_add_to_pollset_set,
    me_shutdown,
    me_destroy,
    me_get_resource_user,
    me_get_peer,
};

static void half_init(half *m, passthru_endpoint *parent,
                      grpc_resource_quota *resource_quota,
                      const char *half_name) {
  m->base.vtable = &vtable;
  m->parent = parent;
  gpr_slice_buffer_init(&m->read_buffer);
  m->on_read = NULL;
  char *name;
  gpr_asprintf(&name, "passthru_endpoint_%s_%" PRIxPTR, half_name,
               (intptr_t)parent);
  m->resource_user = grpc_resource_user_create(resource_quota, name);
  gpr_free(name);
}

void grpc_passthru_endpoint_create(grpc_endpoint **client,
                                   grpc_endpoint **server,
                                   grpc_resource_quota *resource_quota) {
  passthru_endpoint *m = gpr_malloc(sizeof(*m));
  m->halves = 2;
  m->shutdown = 0;
  half_init(&m->client, m, resource_quota, "client");
  half_init(&m->server, m, resource_quota, "server");
  gpr_mu_init(&m->mu);
  *client = &m->client.base;
  *server = &m->server.base;
}
