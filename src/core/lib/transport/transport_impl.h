/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_LIB_TRANSPORT_TRANSPORT_IMPL_H
#define GRPC_CORE_LIB_TRANSPORT_TRANSPORT_IMPL_H

#include "src/core/lib/transport/transport.h"

typedef struct grpc_transport_vtable {
  /* Memory required for a single stream element - this is allocated by upper
     layers and initialized by the transport */
  size_t sizeof_stream; /* = sizeof(transport stream) */

  /* name of this transport implementation */
  const char *name;

  /* implementation of grpc_transport_init_stream */
  int (*init_stream)(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                     grpc_stream *stream, grpc_stream_refcount *refcount,
                     const void *server_data);

  /* implementation of grpc_transport_set_pollset */
  void (*set_pollset)(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                      grpc_stream *stream, grpc_pollset *pollset);

  /* implementation of grpc_transport_set_pollset */
  void (*set_pollset_set)(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                          grpc_stream *stream, grpc_pollset_set *pollset_set);

  /* implementation of grpc_transport_perform_stream_op */
  void (*perform_stream_op)(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                            grpc_stream *stream, grpc_transport_stream_op *op);

  /* implementation of grpc_transport_perform_op */
  void (*perform_op)(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                     grpc_transport_op *op);

  /* implementation of grpc_transport_destroy_stream */
  void (*destroy_stream)(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                         grpc_stream *stream, void *and_free_memory);

  /* implementation of grpc_transport_destroy */
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_transport *self);

  /* implementation of grpc_transport_get_peer */
  char *(*get_peer)(grpc_exec_ctx *exec_ctx, grpc_transport *self);

  /* implementation of grpc_transport_get_endpoint */
  grpc_endpoint *(*get_endpoint)(grpc_exec_ctx *exec_ctx, grpc_transport *self);
} grpc_transport_vtable;

/* an instance of a grpc transport */
struct grpc_transport {
  /* pointer to a vtable defining operations on this transport */
  const grpc_transport_vtable *vtable;
};

#endif /* GRPC_CORE_LIB_TRANSPORT_TRANSPORT_IMPL_H */
