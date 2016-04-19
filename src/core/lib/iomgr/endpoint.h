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

#ifndef GRPC_CORE_LIB_IOMGR_ENDPOINT_H
#define GRPC_CORE_LIB_IOMGR_ENDPOINT_H

#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"

/* An endpoint caps a streaming channel between two communicating processes.
   Examples may be: a tcp socket, <stdin+stdout>, or some shared memory. */

typedef struct grpc_endpoint grpc_endpoint;
typedef struct grpc_endpoint_vtable grpc_endpoint_vtable;

struct grpc_endpoint_vtable {
  void (*read)(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
               gpr_slice_buffer *slices, grpc_closure *cb);
  void (*write)(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                gpr_slice_buffer *slices, grpc_closure *cb);
  void (*add_to_pollset)(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                         grpc_pollset *pollset);
  void (*add_to_pollset_set)(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                             grpc_pollset_set *pollset);
  void (*shutdown)(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep);
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep);
  char *(*get_peer)(grpc_endpoint *ep);
};

/* When data is available on the connection, calls the callback with slices.
   Callback success indicates that the endpoint can accept more reads, failure
   indicates the endpoint is closed.
   Valid slices may be placed into \a slices even on callback success == 0. */
void grpc_endpoint_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                        gpr_slice_buffer *slices, grpc_closure *cb);

char *grpc_endpoint_get_peer(grpc_endpoint *ep);

/* Write slices out to the socket.

   If the connection is ready for more data after the end of the call, it
   returns GRPC_ENDPOINT_DONE.
   Otherwise it returns GRPC_ENDPOINT_PENDING and calls cb when the
   connection is ready for more data.
   \a slices may be mutated at will by the endpoint until cb is called.
   No guarantee is made to the content of slices after a write EXCEPT that
   it is a valid slice buffer.
   */
void grpc_endpoint_write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                         gpr_slice_buffer *slices, grpc_closure *cb);

/* Causes any pending read/write callbacks to run immediately with
   success==0 */
void grpc_endpoint_shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep);
void grpc_endpoint_destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep);

/* Add an endpoint to a pollset, so that when the pollset is polled, events from
   this endpoint are considered */
void grpc_endpoint_add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                  grpc_pollset *pollset);
void grpc_endpoint_add_to_pollset_set(grpc_exec_ctx *exec_ctx,
                                      grpc_endpoint *ep,
                                      grpc_pollset_set *pollset_set);

struct grpc_endpoint {
  const grpc_endpoint_vtable *vtable;
};

#endif /* GRPC_CORE_LIB_IOMGR_ENDPOINT_H */
