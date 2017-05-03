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

#ifndef GRPC_CORE_LIB_IOMGR_SOCKET_MUTATOR_H
#define GRPC_CORE_LIB_IOMGR_SOCKET_MUTATOR_H

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/sync.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The virtual table of grpc_socket_mutator */
typedef struct {
  /** Mutates the socket opitons of \a fd */
  bool (*mutate_fd)(int fd, grpc_socket_mutator *mutator);
  /** Compare socket mutator \a a and \a b */
  int (*compare)(grpc_socket_mutator *a, grpc_socket_mutator *b);
  /** Destroys the socket mutator instance */
  void (*destory)(grpc_socket_mutator *mutator);
} grpc_socket_mutator_vtable;

/** The Socket Mutator interface allows changes on socket options */
struct grpc_socket_mutator {
  const grpc_socket_mutator_vtable *vtable;
  gpr_refcount refcount;
};

/** called by concrete implementations to initialize the base struct */
void grpc_socket_mutator_init(grpc_socket_mutator *mutator,
                              const grpc_socket_mutator_vtable *vtable);

/** Wrap \a mutator as a grpc_arg */
grpc_arg grpc_socket_mutator_to_arg(grpc_socket_mutator *mutator);

/** Perform the file descriptor mutation operation of \a mutator on \a fd */
bool grpc_socket_mutator_mutate_fd(grpc_socket_mutator *mutator, int fd);

/** Compare if \a a and \a b are the same mutator or have same settings */
int grpc_socket_mutator_compare(grpc_socket_mutator *a, grpc_socket_mutator *b);

grpc_socket_mutator *grpc_socket_mutator_ref(grpc_socket_mutator *mutator);
void grpc_socket_mutator_unref(grpc_socket_mutator *mutator);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_SOCKET_MUTATOR_H */
