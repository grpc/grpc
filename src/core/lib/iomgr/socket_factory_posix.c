/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/socket_factory_posix.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

void grpc_socket_factory_init(grpc_socket_factory *factory,
                              const grpc_socket_factory_vtable *vtable) {
  factory->vtable = vtable;
  gpr_ref_init(&factory->refcount, 1);
}

int grpc_socket_factory_socket(grpc_socket_factory *factory, int domain,
                               int type, int protocol) {
  return factory->vtable->socket(factory, domain, type, protocol);
}

int grpc_socket_factory_bind(grpc_socket_factory *factory, int sockfd,
                             const grpc_resolved_address *addr) {
  return factory->vtable->bind(factory, sockfd, addr);
}

int grpc_socket_factory_compare(grpc_socket_factory *a,
                                grpc_socket_factory *b) {
  int c = GPR_ICMP(a, b);
  if (c != 0) {
    grpc_socket_factory *sma = a;
    grpc_socket_factory *smb = b;
    c = GPR_ICMP(sma->vtable, smb->vtable);
    if (c == 0) {
      c = sma->vtable->compare(sma, smb);
    }
  }
  return c;
}

grpc_socket_factory *grpc_socket_factory_ref(grpc_socket_factory *factory) {
  gpr_ref(&factory->refcount);
  return factory;
}

void grpc_socket_factory_unref(grpc_socket_factory *factory) {
  if (gpr_unref(&factory->refcount)) {
    factory->vtable->destroy(factory);
  }
}

static void *socket_factory_arg_copy(void *p) {
  return grpc_socket_factory_ref(p);
}

static void socket_factory_arg_destroy(grpc_exec_ctx *exec_ctx, void *p) {
  grpc_socket_factory_unref(p);
}

static int socket_factory_cmp(void *a, void *b) {
  return grpc_socket_factory_compare((grpc_socket_factory *)a,
                                     (grpc_socket_factory *)b);
}

static const grpc_arg_pointer_vtable socket_factory_arg_vtable = {
    socket_factory_arg_copy, socket_factory_arg_destroy, socket_factory_cmp};

grpc_arg grpc_socket_factory_to_arg(grpc_socket_factory *factory) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_SOCKET_FACTORY;
  arg.value.pointer.vtable = &socket_factory_arg_vtable;
  arg.value.pointer.p = factory;
  return arg;
}

#endif
