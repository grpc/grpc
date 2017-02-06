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

#include "src/core/lib/iomgr/socket_mutator.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

void grpc_socket_mutator_init(grpc_socket_mutator *mutator,
                              const grpc_socket_mutator_vtable *vtable) {
  mutator->vtable = vtable;
  gpr_ref_init(&mutator->refcount, 1);
}

grpc_socket_mutator *grpc_socket_mutator_ref(grpc_socket_mutator *mutator) {
  gpr_ref(&mutator->refcount);
  return mutator;
}

bool grpc_socket_mutator_mutate_fd(grpc_socket_mutator *mutator, int fd) {
  return mutator->vtable->mutate_fd(fd, mutator);
}

int grpc_socket_mutator_compare(grpc_socket_mutator *a,
                                grpc_socket_mutator *b) {
  int c = GPR_ICMP(a, b);
  if (c != 0) {
    grpc_socket_mutator *sma = a;
    grpc_socket_mutator *smb = b;
    c = GPR_ICMP(sma->vtable, smb->vtable);
    if (c == 0) {
      c = sma->vtable->compare(sma, smb);
    }
  }
  return c;
}

void grpc_socket_mutator_unref(grpc_socket_mutator *mutator) {
  if (gpr_unref(&mutator->refcount)) {
    mutator->vtable->destory(mutator);
  }
}

static void *socket_mutator_arg_copy(void *p) {
  return grpc_socket_mutator_ref(p);
}

static void socket_mutator_arg_destroy(grpc_exec_ctx *exec_ctx, void *p) {
  grpc_socket_mutator_unref(p);
}

static int socket_mutator_cmp(void *a, void *b) {
  return grpc_socket_mutator_compare((grpc_socket_mutator *)a,
                                     (grpc_socket_mutator *)b);
}

static const grpc_arg_pointer_vtable socket_mutator_arg_vtable = {
    socket_mutator_arg_copy, socket_mutator_arg_destroy, socket_mutator_cmp};

grpc_arg grpc_socket_mutator_to_arg(grpc_socket_mutator *mutator) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_SOCKET_MUTATOR;
  arg.value.pointer.vtable = &socket_mutator_arg_vtable;
  arg.value.pointer.p = mutator;
  return arg;
}
