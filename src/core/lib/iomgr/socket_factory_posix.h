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

#ifndef GRPC_CORE_LIB_IOMGR_SOCKET_FACTORY_POSIX_H
#define GRPC_CORE_LIB_IOMGR_SOCKET_FACTORY_POSIX_H

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/sync.h>
#include "src/core/lib/iomgr/resolve_address.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The virtual table of grpc_socket_factory */
typedef struct {
  /** Replacement for socket(2) */
  int (*socket)(grpc_socket_factory *factory, int domain, int type,
                int protocol);
  /** Replacement for bind(2) */
  int (*bind)(grpc_socket_factory *factory, int sockfd,
              const grpc_resolved_address *addr);
  /** Compare socket factory \a a and \a b */
  int (*compare)(grpc_socket_factory *a, grpc_socket_factory *b);
  /** Destroys the socket factory instance */
  void (*destroy)(grpc_socket_factory *factory);
} grpc_socket_factory_vtable;

/** The Socket Factory interface allows changes on socket options */
struct grpc_socket_factory {
  const grpc_socket_factory_vtable *vtable;
  gpr_refcount refcount;
};

/** called by concrete implementations to initialize the base struct */
void grpc_socket_factory_init(grpc_socket_factory *factory,
                              const grpc_socket_factory_vtable *vtable);

/** Wrap \a factory as a grpc_arg */
grpc_arg grpc_socket_factory_to_arg(grpc_socket_factory *factory);

/** Perform the equivalent of a socket(2) operation using \a factory */
int grpc_socket_factory_socket(grpc_socket_factory *factory, int domain,
                               int type, int protocol);

/** Perform the equivalent of a bind(2) operation using \a factory */
int grpc_socket_factory_bind(grpc_socket_factory *factory, int sockfd,
                             const grpc_resolved_address *addr);

/** Compare if \a a and \a b are the same factory or have same settings */
int grpc_socket_factory_compare(grpc_socket_factory *a, grpc_socket_factory *b);

grpc_socket_factory *grpc_socket_factory_ref(grpc_socket_factory *factory);
void grpc_socket_factory_unref(grpc_socket_factory *factory);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_SOCKET_FACTORY_POSIX_H */
