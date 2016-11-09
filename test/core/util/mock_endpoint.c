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

#include "test/core/util/mock_endpoint.h"

#include <inttypes.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

typedef struct grpc_mock_endpoint {
  grpc_endpoint base;
  gpr_mu mu;
  void (*on_write)(grpc_slice slice);
  grpc_slice_buffer read_buffer;
  grpc_slice_buffer *on_read_out;
  grpc_closure *on_read;
  grpc_resource_user *resource_user;
} grpc_mock_endpoint;

static void me_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                    grpc_slice_buffer *slices, grpc_closure *cb) {
  grpc_mock_endpoint *m = (grpc_mock_endpoint *)ep;
  gpr_mu_lock(&m->mu);
  if (m->read_buffer.count > 0) {
    grpc_slice_buffer_swap(&m->read_buffer, slices);
    grpc_exec_ctx_sched(exec_ctx, cb, GRPC_ERROR_NONE, NULL);
  } else {
    m->on_read = cb;
    m->on_read_out = slices;
  }
  gpr_mu_unlock(&m->mu);
}

static void me_write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                     grpc_slice_buffer *slices, grpc_closure *cb) {
  grpc_mock_endpoint *m = (grpc_mock_endpoint *)ep;
  for (size_t i = 0; i < slices->count; i++) {
    m->on_write(slices->slices[i]);
  }
  grpc_exec_ctx_sched(exec_ctx, cb, GRPC_ERROR_NONE, NULL);
}

static void me_add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                              grpc_pollset *pollset) {}

static void me_add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                  grpc_pollset_set *pollset) {}

static void me_shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  grpc_mock_endpoint *m = (grpc_mock_endpoint *)ep;
  gpr_mu_lock(&m->mu);
  if (m->on_read) {
    grpc_exec_ctx_sched(exec_ctx, m->on_read,
                        GRPC_ERROR_CREATE("Endpoint Shutdown"), NULL);
    m->on_read = NULL;
  }
  gpr_mu_unlock(&m->mu);
  grpc_resource_user_shutdown(exec_ctx, m->resource_user);
}

static void me_destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  grpc_mock_endpoint *m = (grpc_mock_endpoint *)ep;
  grpc_slice_buffer_destroy(&m->read_buffer);
  grpc_resource_user_unref(exec_ctx, m->resource_user);
  gpr_free(m);
}

static char *me_get_peer(grpc_endpoint *ep) {
  return gpr_strdup("fake:mock_endpoint");
}

static grpc_resource_user *me_get_resource_user(grpc_endpoint *ep) {
  grpc_mock_endpoint *m = (grpc_mock_endpoint *)ep;
  return m->resource_user;
}

static grpc_workqueue *me_get_workqueue(grpc_endpoint *ep) { return NULL; }

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

grpc_endpoint *grpc_mock_endpoint_create(void (*on_write)(grpc_slice slice),
                                         grpc_resource_quota *resource_quota) {
  grpc_mock_endpoint *m = gpr_malloc(sizeof(*m));
  m->base.vtable = &vtable;
  char *name;
  gpr_asprintf(&name, "mock_endpoint_%" PRIxPTR, (intptr_t)m);
  m->resource_user = grpc_resource_user_create(resource_quota, name);
  gpr_free(name);
  grpc_slice_buffer_init(&m->read_buffer);
  gpr_mu_init(&m->mu);
  m->on_write = on_write;
  m->on_read = NULL;
  return &m->base;
}

void grpc_mock_endpoint_put_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                 grpc_slice slice) {
  grpc_mock_endpoint *m = (grpc_mock_endpoint *)ep;
  gpr_mu_lock(&m->mu);
  if (m->on_read != NULL) {
    grpc_slice_buffer_add(m->on_read_out, slice);
    grpc_exec_ctx_sched(exec_ctx, m->on_read, GRPC_ERROR_NONE, NULL);
    m->on_read = NULL;
  } else {
    grpc_slice_buffer_add(&m->read_buffer, slice);
  }
  gpr_mu_unlock(&m->mu);
}
