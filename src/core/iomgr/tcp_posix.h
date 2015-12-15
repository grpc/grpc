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

#ifndef GRPC_INTERNAL_CORE_IOMGR_TCP_POSIX_H
#define GRPC_INTERNAL_CORE_IOMGR_TCP_POSIX_H
/*
   Low level TCP "bottom half" implementation, for use by transports built on
   top of a TCP connection.

   Note that this file does not (yet) include APIs for creating the socket in
   the first place.

   All calls passing slice transfer ownership of a slice refcount unless
   otherwise specified.
*/

#include "src/core/iomgr/endpoint.h"
#include "src/core/iomgr/fd_posix.h"

#define GRPC_TCP_DEFAULT_READ_SLICE_SIZE 8192

extern int grpc_tcp_trace;

/* Create a tcp endpoint given a file desciptor and a read slice size.
   Takes ownership of fd. */
grpc_endpoint *grpc_tcp_create(grpc_fd *fd, size_t read_slice_size,
                               const char *peer_string);

/* Destroy the tcp endpoint without closing its fd. *fd will be set and done
 * will be called when the endpoint is destroyed.
 * Requires: ep must be a tcp endpoint and fd must not be NULL. */
void grpc_tcp_destroy_and_release_fd(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                     int *fd, grpc_closure *done);

#endif /* GRPC_INTERNAL_CORE_IOMGR_TCP_POSIX_H */
