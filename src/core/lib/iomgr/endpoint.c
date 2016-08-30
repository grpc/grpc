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

#include "src/core/lib/iomgr/endpoint.h"

void grpc_endpoint_read(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                        gpr_slice_buffer* slices, grpc_closure* cb) {
  ep->vtable->read(exec_ctx, ep, slices, cb);
}

void grpc_endpoint_write(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                         gpr_slice_buffer* slices, grpc_closure* cb) {
  ep->vtable->write(exec_ctx, ep, slices, cb);
}

void grpc_endpoint_add_to_pollset(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                                  grpc_pollset* pollset) {
  ep->vtable->add_to_pollset(exec_ctx, ep, pollset);
}

void grpc_endpoint_add_to_pollset_set(grpc_exec_ctx* exec_ctx,
                                      grpc_endpoint* ep,
                                      grpc_pollset_set* pollset_set) {
  ep->vtable->add_to_pollset_set(exec_ctx, ep, pollset_set);
}

void grpc_endpoint_shutdown(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep) {
  ep->vtable->shutdown(exec_ctx, ep);
}

void grpc_endpoint_destroy(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep) {
  ep->vtable->destroy(exec_ctx, ep);
}

char* grpc_endpoint_get_peer(grpc_endpoint* ep) {
  return ep->vtable->get_peer(ep);
}

grpc_workqueue* grpc_endpoint_get_workqueue(grpc_endpoint* ep) {
  return ep->vtable->get_workqueue(ep);
}
